/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/filename.cpp
// Purpose:     wxFileName - encapsulates a file path
// Author:      Robert Roebling, Vadim Zeitlin
// Created:     28.12.2000
// Copyright:   (c) 2000 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

/*
   Here are brief descriptions of the filename formats supported by this class:

   wxPATH_UNIX: standard Unix format, used under Darwin as well, absolute file
                names have the form:
                /dir1/dir2/.../dirN/filename, "." and ".." stand for the
                current and parent directory respectively, "~" is parsed as the
                user HOME and "~username" as the HOME of that user

   wxPATH_DOS:  DOS/Windows format, absolute file names have the form:
                drive:\dir1\dir2\...\dirN\filename.ext where drive is a single
                letter. "." and ".." as for Unix but no "~".

                There are also UNC names of the form \\share\fullpath and
                MSW unique volume names of the form \\?\Volume{GUID}\fullpath.

                The latter provide a uniform way to access a volume regardless of
                its current mount point, i.e. you can change a volume's mount
                point from D: to E:, or even remove it, and still be able to
                access it through its unique volume name. More on the subject can
                be found in Microsoft's "Naming a Volume" documentation at
                https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-volume


   wxPATH_MAC:  Mac OS 8/9 only, not used any longer, absolute file
                names have the form
                    volume:dir1:...:dirN:filename
                and the relative file names are either
                    :dir1:...:dirN:filename
                or just
                    filename
                (although :filename works as well).
                Since the volume is just part of the file path, it is not
                treated like a separate entity as it is done under DOS and
                VMS, it is just treated as another dir.

   wxPATH_VMS:  VMS native format, absolute file names have the form
                    <device>:[dir1.dir2.dir3]file.txt
                or
                    <device>:[000000.dir1.dir2.dir3]file.txt

                the <device> is the physical device (i.e. disk). 000000 is the
                root directory on the device which can be omitted.

                Note that VMS uses different separators unlike Unix:
                 : always after the device. If the path does not contain : than
                   the default (the device of the current directory) is assumed.
                 [ start of directory specification
                 . separator between directory and subdirectory
                 ] between directory and file
 */

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #ifdef __WINDOWS__
        #include "wx/msw/wrapwin.h" // For GetShort/LongPathName
    #endif
    #include "wx/dynarray.h"
    #include "wx/intl.h"
    #include "wx/log.h"
    #include "wx/utils.h"
    #include "wx/crt.h"
#endif

#include "wx/filename.h"
#include "wx/private/filename.h"
#include "wx/tokenzr.h"
#include "wx/config.h"          // for wxExpandEnvVars
#include "wx/dynlib.h"
#include "wx/dir.h"
#include "wx/longlong.h"
#include "wx/uri.h"

#if defined(wxHAS_NATIVE_READLINK)
    #include "wx/vector.h"
#endif

#if defined(__WINDOWS__) && defined(__MINGW32__)
    #include "wx/msw/gccpriv.h"
#endif

#ifdef __WINDOWS__
    #include "wx/msw/private.h"
    #include "wx/msw/wrapshl.h"         // for CLSID_ShellLink
    #include "wx/msw/missing.h"
    #include "wx/msw/ole/oleutils.h"
    #include "wx/msw/private/comptr.h"
#endif

#if defined(__WXMAC__)
  #include  "wx/osx/private.h"  // includes mac headers
#endif

// utime() is POSIX so should normally be available on all Unices
#ifdef __UNIX_LIKE__
#include <sys/types.h>
#include <utime.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef S_ISREG
    #define S_ISREG(mode) ((mode) & S_IFREG)
#endif
#ifndef S_ISDIR
    #define S_ISDIR(mode) ((mode) & S_IFDIR)
#endif

extern const wxULongLong wxInvalidSize = (unsigned)-1;

namespace
{

// ----------------------------------------------------------------------------
// private constants
// ----------------------------------------------------------------------------

// Prefix of MSW extended-length paths.
static constexpr char wxMSW_EXTENDED_PATH_PREFIX[] = R"(\\?\)";
static constexpr size_t
    wxMSW_EXTENDED_PATH_PREFIX_LEN = WXSIZEOF(wxMSW_EXTENDED_PATH_PREFIX) - 1;

// ----------------------------------------------------------------------------
// private classes
// ----------------------------------------------------------------------------

// small helper class which opens and closes the file - we use it just to get
// a file handle for the given file name to pass it to some Win32 API function
#if defined(__WINDOWS__)

class wxFileHandle
{
public:
    enum OpenMode
    {
        ReadAttr,
        WriteAttr
    };

    wxFileHandle(const wxString& filename, OpenMode mode, int flags = 0)
    {
        // be careful and use FILE_{READ,WRITE}_ATTRIBUTES here instead of the
        // usual GENERIC_{READ,WRITE} as we don't want the file access time to
        // be changed when we open it because this class is used for setting
        // access time (see #10567)
        m_hFile = ::CreateFile
                    (
                     filename.t_str(),             // name
                     mode == ReadAttr ? FILE_READ_ATTRIBUTES    // access mask
                                      : FILE_WRITE_ATTRIBUTES,
                     FILE_SHARE_READ |              // sharing mode
                     FILE_SHARE_WRITE,              // (allow everything)
                     nullptr,                       // no security attr
                     OPEN_EXISTING,                 // creation disposition
                     flags,                         // flags
                     nullptr                        // no template file
                    );

        if ( m_hFile == INVALID_HANDLE_VALUE )
        {
            if ( mode == ReadAttr )
            {
                wxLogSysError(_("Failed to open '%s' for reading"),
                              filename);
            }
            else
            {
                wxLogSysError(_("Failed to open '%s' for writing"),
                              filename);
            }
        }
    }

    ~wxFileHandle()
    {
        if ( m_hFile != INVALID_HANDLE_VALUE )
        {
            if ( !::CloseHandle(m_hFile) )
            {
                wxLogSysError(_("Failed to close file handle"));
            }
        }
    }

    // return true only if the file could be opened successfully
    bool IsOk() const { return m_hFile != INVALID_HANDLE_VALUE; }

    // get the handle
    operator HANDLE() const { return m_hFile; }

private:
    HANDLE m_hFile;
};

#endif // __WINDOWS__

// ----------------------------------------------------------------------------
// private functions
// ----------------------------------------------------------------------------

#if wxUSE_DATETIME && defined(__WINDOWS__)

// Convert between wxDateTime and FILETIME which is a 64-bit value representing
// the number of 100-nanosecond intervals since January 1, 1601 UTC.
//
// This is the offset between FILETIME epoch and the Unix/wxDateTime Epoch.
static wxInt64 EPOCH_OFFSET_IN_MSEC = wxLL(11644473600000);

static void ConvertFileTimeToWx(wxDateTime *dt, const FILETIME &ft)
{
    wxLongLong t(ft.dwHighDateTime, ft.dwLowDateTime);
    t /= 10000; // Convert hundreds of nanoseconds to milliseconds.
    t -= EPOCH_OFFSET_IN_MSEC;

    *dt = wxDateTime(t);
}

static void ConvertWxToFileTime(FILETIME *ft, const wxDateTime& dt)
{
    // Undo the conversions above.
    wxLongLong t(dt.GetValue());
    t += EPOCH_OFFSET_IN_MSEC;
    t *= 10000;

    ft->dwHighDateTime = t.GetHi();
    ft->dwLowDateTime = t.GetLo();
}

#endif // wxUSE_DATETIME && __WINDOWS__

// return a string with the volume par
static wxString wxGetVolumeString(const wxString& volume, wxPathFormat format)
{
    wxString path;

    if ( !volume.empty() )
    {
        format = wxFileName::GetFormat(format);

        switch ( format )
        {
            case wxPATH_DOS:
                path = volume;

                // We shouldn't use a colon after the volume in UNC and volume
                // GUID paths, so append it only if it's just a drive letter.
                if ( volume.length() == 1 )
                    path += wxFileName::GetVolumeSeparator(format);
                break;

            case wxPATH_VMS:
                path << volume << wxFileName::GetVolumeSeparator(format);
                break;

            case wxPATH_MAC:
            case wxPATH_UNIX:
                // Volumes are not used in paths in this format.
                break;

            case wxPATH_NATIVE:
            case wxPATH_MAX:
                wxFAIL_MSG( wxS("unreachable") );
                break;
        }
    }

    return path;
}

// return true if the character is a DOS path separator i.e. either a slash or
// a backslash
inline bool IsDOSPathSep(wxUniChar ch)
{
    return ch == wxFILE_SEP_PATH_DOS || ch == wxFILE_SEP_PATH_UNIX;
}

// return true if the string looks like a UNC path
static bool IsUNCPath(const wxString& path)
{
    return path.length() >= 3 && // "\\a" is the shortest UNC path
                    IsDOSPathSep(path[0u]) &&
                        IsDOSPathSep(path[1u]) &&
                            !IsDOSPathSep(path[2u]);
}

// Under Unix-ish systems (basically everything except Windows but we can't
// just test for non-__WINDOWS__ because Cygwin defines it, yet we want to use
// lstat() under it, so test for all the rest explicitly) we may work either
// with the file itself or its target if it's a symbolic link and we should
// dereference it, as determined by wxFileName::ShouldFollowLink() and the
// absence of the wxFILE_EXISTS_NO_FOLLOW flag. StatAny() can be used to stat
// the appropriate file with an extra twist that it also works when there is no
// wxFileName object at all, as is the case in static methods.

#if defined(__UNIX_LIKE__) || defined(__WXMAC__)
    #define wxHAVE_LSTAT
#endif

#ifdef wxHAVE_LSTAT

// Private implementation, don't call directly, use one of the overloads below.
bool DoStatAny(wxStructStat& st, wxString path, bool dereference)
{
    // We need to remove any trailing slashes from the path because they could
    // interfere with the symlink following decision: even if we use lstat(),
    // it would still follow the symlink if we pass it a path with a slash at
    // the end because the symlink resolution would happen while following the
    // path and not for the last path element itself.

    while ( wxEndsWithPathSeparator(path) )
    {
        const size_t posLast = path.length() - 1;
        if ( !posLast )
        {
            // Don't turn "/" into empty string.
            break;
        }

        path.erase(posLast);
    }

    int ret = dereference ? wxStat(path, &st) : wxLstat(path, &st);
    return ret == 0;
}

// Overloads to use for a case when we don't have wxFileName object and when we
// do have one.
inline
bool StatAny(wxStructStat& st, const wxString& path, int flags)
{
    return DoStatAny(st, path, !(flags & wxFILE_EXISTS_NO_FOLLOW));
}

inline
bool StatAny(wxStructStat& st, const wxFileName& fn)
{
    return DoStatAny(st, fn.GetFullPath(), fn.ShouldFollowLink());
}

#endif // wxHAVE_LSTAT

} // anonymous namespace

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxFileName construction
// ----------------------------------------------------------------------------

void wxFileName::Assign( const wxFileName &filepath )
{
    m_volume = filepath.GetVolume();
    m_dirs = filepath.GetDirs();
    m_name = filepath.GetName();
    m_ext = filepath.GetExt();
    m_relative = filepath.m_relative;
    m_hasExt = filepath.m_hasExt;
    m_dontFollowLinks = filepath.m_dontFollowLinks;
}

void wxFileName::Assign(const wxString& volume,
                        const wxString& path,
                        const wxString& name,
                        const wxString& ext,
                        bool hasExt,
                        wxPathFormat format)
{
    // we should ignore paths which look like UNC shares because we already
    // have the volume here and the UNC notation (\\server\path) is only valid
    // for paths which don't start with a volume, so prevent SetPath() from
    // recognizing "\\foo\bar" in "c:\\foo\bar" as an UNC path
    DoSetPath(path, format, SetPath_PathOnly);

    m_volume = volume;
    m_ext = ext;
    m_name = name;

    m_hasExt = hasExt;
}

void wxFileName::SetPath( const wxString& path, wxPathFormat format )
{
    DoSetPath(path, format, SetPath_MayHaveVolume);
}

void
wxFileName::DoSetPath(const wxString& pathOrig, wxPathFormat format, int flags)
{
    m_dirs.Clear();

    if ( pathOrig.empty() )
    {
        // no path at all
        m_relative = true;

        return;
    }

    format = GetFormat( format );

    // 0) deal with possible volume part first
    wxString path;
    if ( flags & SetPath_MayHaveVolume )
    {
        wxString volume;
        SplitVolume(pathOrig, &volume, &path, format);
        if ( !volume.empty() )
        {
            m_relative = false;

            SetVolume(volume);
        }

        if ( path.empty() )
        {
            // we had only the volume
            return;
        }
    }
    else
    {
        path = pathOrig;
    }

    // 1) Determine if the path is relative or absolute.

    const wxUniChar leadingChar = path[0u];

    switch (format)
    {
        case wxPATH_MAC:
            m_relative = leadingChar == wxT(':');

            // We then remove a leading ":". The reason is in our
            // storage form for relative paths:
            // ":dir:file.txt" actually means "./dir/file.txt" in
            // DOS notation and should get stored as
            // (relative) (dir) (file.txt)
            // "::dir:file.txt" actually means "../dir/file.txt"
            // stored as (relative) (..) (dir) (file.txt)
            // This is important only for the Mac as an empty dir
            // actually means <UP>, whereas under DOS, double
            // slashes can be ignored: "\\\\" is the same as "\\".
            if (m_relative)
                path.erase( 0, 1 );
            break;

        case wxPATH_VMS:
            // TODO: what is the relative path format here?
            m_relative = false;
            break;

        default:
            wxFAIL_MSG( wxT("Unknown path format") );
            wxFALLTHROUGH;

        case wxPATH_UNIX:
            m_relative = leadingChar != wxT('/');
            break;

        case wxPATH_DOS:
            m_relative = !IsPathSeparator(leadingChar, format);
            break;

    }

    // 2) Break up the path into its members. If the original path
    //    was just "/" or "\\", m_dirs will be empty. We know from
    //    the m_relative field, if this means "nothing" or "root dir".

    wxStringTokenizer tn( path, GetPathSeparators(format) );

    while ( tn.HasMoreTokens() )
    {
        wxString token = tn.GetNextToken();

        // Remove empty token under DOS and Unix, interpret them
        // as .. under Mac.
        if (token.empty())
        {
            if (format == wxPATH_MAC)
                m_dirs.Add( wxT("..") );
            // else ignore
        }
        else
        {
           m_dirs.Add( token );
        }
    }
}

void wxFileName::Assign(const wxString& fullpath,
                        wxPathFormat format)
{
    wxString volume, path, name, ext;
    bool hasExt;
    SplitPath(fullpath, &volume, &path, &name, &ext, &hasExt, format);

    Assign(volume, path, name, ext, hasExt, format);
}

void wxFileName::Assign(const wxString& fullpathOrig,
                        const wxString& fullname,
                        wxPathFormat format)
{
    // always recognize fullpath as directory, even if it doesn't end with a
    // slash
    wxString fullpath = fullpathOrig;
    if ( !fullpath.empty() && !wxEndsWithPathSeparator(fullpath) )
    {
        fullpath += GetPathSeparator(format);
    }

    wxString volume, path, name, ext;
    bool hasExt;

    // do some consistency checks: the name should be really just the filename
    // and the path should be really just a path
    wxString volDummy, pathDummy, nameDummy, extDummy;

    SplitPath(fullname, &volDummy, &pathDummy, &name, &ext, &hasExt, format);

    wxASSERT_MSG( volDummy.empty() && pathDummy.empty(),
                  wxT("the file name shouldn't contain the path") );

    SplitPath(fullpath, &volume, &path, &nameDummy, &extDummy, format);

#ifndef __VMS
   // This test makes no sense on an OpenVMS system.
   wxASSERT_MSG( nameDummy.empty() && extDummy.empty(),
                  wxT("the path shouldn't contain file name nor extension") );
#endif
    Assign(volume, path, name, ext, hasExt, format);
}

void wxFileName::Assign(const wxString& pathOrig,
                        const wxString& name,
                        const wxString& ext,
                        wxPathFormat format)
{
    wxString volume,
             path;
    SplitVolume(pathOrig, &volume, &path, format);

    Assign(volume, path, name, ext, format);
}

void wxFileName::AssignDir(const wxString& dir, wxPathFormat format)
{
    Assign(dir, wxEmptyString, format);
}

void wxFileName::Clear()
{
    m_dirs.clear();
    m_volume.clear();
    m_name.clear();
    m_ext.clear();

    // we don't have any absolute path for now
    m_relative = true;

    // nor any extension
    m_hasExt = false;

    // follow symlinks by default
    m_dontFollowLinks = false;
}

/* static */
wxFileName wxFileName::FileName(const wxString& file, wxPathFormat format)
{
    return wxFileName(file, format);
}

/* static */
wxFileName wxFileName::DirName(const wxString& dir, wxPathFormat format)
{
    wxFileName fn;
    fn.AssignDir(dir, format);
    return fn;
}

// ----------------------------------------------------------------------------
// existence tests
// ----------------------------------------------------------------------------

namespace
{

#if defined(__WINDOWS__)

void RemoveTrailingSeparatorsFromPath(wxString& strPath)
{
    // We should never have empty paths here, but skip them if we ever do.
    if ( strPath.empty() )
        return;

    // Windows fails to find directory named "c:\dir\" even if "c:\dir" exists,
    // so remove all trailing backslashes from the path - but don't do this if
    // it is the last slash in the path to avoid turning "d:\" into "d:" (which
    // is a different path), turning "\" into nothing or making extended length
    // paths invalid.
    const auto lastNonSeparator = strPath.find_last_not_of(R"(\/)");

    const auto firstTrailingSeparator =
        lastNonSeparator == wxString::npos ? 0 : lastNonSeparator + 1;

    if ( firstTrailingSeparator == strPath.length() )
    {
        // The path doesn't end with a separator, nothing to do.
        return;
    }

    // Check if there any separators would remain if we removed all trailing
    // ones, ignoring those that are part of fixed wxMSW_EXTENDED_PATH_PREFIX
    // or UNC path prefix.
    const auto lastButOneSeparator =
        strPath.find_last_of(R"(\/)", lastNonSeparator);
    if ( lastButOneSeparator == wxString::npos ||
            (lastButOneSeparator == wxMSW_EXTENDED_PATH_PREFIX_LEN - 1 &&
             strPath.StartsWith(wxMSW_EXTENDED_PATH_PREFIX)) ||
                (lastButOneSeparator == 1 && IsUNCPath(strPath)) )
    {
        // The path doesn't contain any other separators, so don't remove all
        // of them.
        strPath.erase(firstTrailingSeparator + 1);
        return;
    }

    // Remove all trailing separators.
    strPath.erase(firstTrailingSeparator);
}

#endif // __WINDOWS_

bool
wxFileSystemObjectExists(const wxString& path, int flags)
{

    // Should the existence of file/directory with this name be accepted, i.e.
    // result in the true return value from this function?
    const bool acceptFile = (flags & wxFILE_EXISTS_REGULAR) != 0;
    const bool acceptDir  = (flags & wxFILE_EXISTS_DIR)  != 0;

    wxString strPath(path);

#if defined(__WINDOWS__)
    if ( acceptDir )
    {
        // Ensure that the path doesn't have any trailing separators when
        // checking for directories.
        RemoveTrailingSeparatorsFromPath(strPath);
    }

    // we must use GetFileAttributes() instead of the ANSI C functions because
    // it can cope with network (UNC) paths unlike them
    DWORD ret = ::GetFileAttributes(strPath.t_str());

    if ( ret == INVALID_FILE_ATTRIBUTES )
        return false;

    if ( ret & FILE_ATTRIBUTE_DIRECTORY )
        return acceptDir;

    // Anything else must be a file (perhaps we should check for
    // FILE_ATTRIBUTE_REPARSE_POINT?)
    return acceptFile;
#else // Non-MSW
    wxStructStat st;
    if ( !StatAny(st, strPath, flags) )
        return false;

    if ( S_ISREG(st.st_mode) )
        return acceptFile;
    if ( S_ISDIR(st.st_mode) )
        return acceptDir;
    if ( S_ISLNK(st.st_mode) )
    {
        // Take care to not test for "!= 0" here as this would erroneously
        // return true if only wxFILE_EXISTS_NO_FOLLOW, which is part of
        // wxFILE_EXISTS_SYMLINK, is set too.
        return (flags & wxFILE_EXISTS_SYMLINK) == wxFILE_EXISTS_SYMLINK;
    }
    if ( S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode) )
        return (flags & wxFILE_EXISTS_DEVICE) != 0;
    if ( S_ISFIFO(st.st_mode) )
        return (flags & wxFILE_EXISTS_FIFO) != 0;
    if ( S_ISSOCK(st.st_mode) )
        return (flags & wxFILE_EXISTS_SOCKET) != 0;

    return flags & wxFILE_EXISTS_ANY;
#endif // Platforms
}

} // anonymous namespace

bool wxFileName::FileExists() const
{
    int flags = wxFILE_EXISTS_REGULAR;
    if ( !ShouldFollowLink() )
        flags |= wxFILE_EXISTS_NO_FOLLOW;

    return wxFileSystemObjectExists(GetFullPath(), flags);
}

/* static */
bool wxFileName::FileExists( const wxString &filePath )
{
    return wxFileSystemObjectExists(filePath, wxFILE_EXISTS_REGULAR);
}

bool wxFileName::DirExists() const
{
    int flags = wxFILE_EXISTS_DIR;
    if ( !ShouldFollowLink() )
        flags |= wxFILE_EXISTS_NO_FOLLOW;

    return Exists(GetPath(), flags);
}

/* static */
bool wxFileName::DirExists( const wxString &dirPath )
{
    return wxFileSystemObjectExists(dirPath, wxFILE_EXISTS_DIR);
}

bool wxFileName::Exists(int flags) const
{
    // Notice that wxFILE_EXISTS_NO_FOLLOW may be specified in the flags even
    // if our DontFollowLink() hadn't been called and we do honour it then. But
    // if the user took the care of calling DontFollowLink(), it is always
    // taken into account.
    if ( !ShouldFollowLink() )
        flags |= wxFILE_EXISTS_NO_FOLLOW;

    return wxFileSystemObjectExists(GetFullPath(), flags);
}

/* static */
bool wxFileName::Exists(const wxString& path, int flags)
{
    return wxFileSystemObjectExists(path, flags);
}

// ----------------------------------------------------------------------------
// CWD and HOME stuff
// ----------------------------------------------------------------------------

void wxFileName::AssignCwd(const wxString& volume)
{
    AssignDir(wxFileName::GetCwd(volume));
}

/* static */
wxString wxFileName::GetCwd(const wxString& volume)
{
    // if we have the volume, we must get the current directory on this drive
    // and to do this we have to chdir to this volume - at least under Windows,
    // I don't know how to get the current drive on another volume elsewhere
    // (TODO)
    wxString cwdOld;
    if ( !volume.empty() )
    {
        cwdOld = wxGetCwd();
        SetCwd(volume + GetVolumeSeparator());
    }

    wxString cwd = ::wxGetCwd();

    if ( !volume.empty() )
    {
        SetCwd(cwdOld);
    }

    return cwd;
}

bool wxFileName::SetCwd() const
{
    return wxFileName::SetCwd( GetPath() );
}

bool wxFileName::SetCwd( const wxString &cwd )
{
    return ::wxSetWorkingDirectory( cwd );
}

void wxFileName::AssignHomeDir()
{
    AssignDir(wxFileName::GetHomeDir());
}

wxString wxFileName::GetHomeDir()
{
    return ::wxGetHomeDir();
}


// ----------------------------------------------------------------------------
// CreateTempFileName
// ----------------------------------------------------------------------------

#if wxUSE_FILE || wxUSE_FFILE


#if !defined wx_fdopen && defined HAVE_FDOPEN
    #define wx_fdopen fdopen
#endif

// NB: GetTempFileName() under Windows creates the file, so using
//     O_EXCL there would fail
#ifdef __WINDOWS__
    #define wxOPEN_EXCL 0
#else
    #define wxOPEN_EXCL O_EXCL
#endif


#ifdef wxOpenOSFHandle
#define WX_HAVE_DELETE_ON_CLOSE
// On Windows create a file with the FILE_FLAGS_DELETE_ON_CLOSE flags.
//
static int wxOpenWithDeleteOnClose(const wxString& filename)
{
    DWORD access = GENERIC_READ | GENERIC_WRITE;

    DWORD disposition = OPEN_ALWAYS;

    DWORD attributes = FILE_ATTRIBUTE_TEMPORARY |
                       FILE_FLAG_DELETE_ON_CLOSE;

    HANDLE h = ::CreateFile(filename.t_str(), access, 0, nullptr,
                            disposition, attributes, nullptr);

    return wxOpenOSFHandle(h, wxO_BINARY);
}
#endif // wxOpenOSFHandle


// Helper to open the file
//
static int wxTempOpen(const wxString& path, bool *deleteOnClose)
{
#ifdef WX_HAVE_DELETE_ON_CLOSE
    if (*deleteOnClose)
        return wxOpenWithDeleteOnClose(path);
#endif

    *deleteOnClose = false;

    return wxOpen(path, wxO_BINARY | O_RDWR | O_CREAT | wxOPEN_EXCL, 0600);
}


#if wxUSE_FFILE
// Helper to open the file and attach it to the wxFFile
//
static bool wxTempOpen(wxFFile *file, const wxString& path, bool *deleteOnClose)
{
#ifndef wx_fdopen
    *deleteOnClose = false;
    return file->Open(path, wxT("w+b"));
#else // wx_fdopen
    int fd = wxTempOpen(path, deleteOnClose);
    if (fd == -1)
        return false;
    file->Attach(wx_fdopen(fd, "w+b"), path);
    return file->IsOpened();
#endif // wx_fdopen
}
#endif // wxUSE_FFILE


#if !wxUSE_FILE
    #define WXFILEARGS(x, y) y
#elif !wxUSE_FFILE
    #define WXFILEARGS(x, y) x
#else
    #define WXFILEARGS(x, y) x, y
#endif


// Implementation of wxFileName::CreateTempFileName().
//
static wxString wxCreateTempImpl(
        const wxString& prefix,
        WXFILEARGS(wxFile *fileTemp, wxFFile *ffileTemp),
        bool *deleteOnClose = nullptr)
{
#if wxUSE_FILE && wxUSE_FFILE
    wxASSERT(fileTemp == nullptr || ffileTemp == nullptr);
#endif
    wxString path, dir, name;
    bool wantDeleteOnClose = false;

    if (deleteOnClose)
    {
        // set the result to false initially
        wantDeleteOnClose = *deleteOnClose;
        *deleteOnClose = false;
    }
    else
    {
        // easier if it always points to something
        deleteOnClose = &wantDeleteOnClose;
    }

    // use the directory specified by the prefix
    wxFileName::SplitPath(prefix, &dir, &name, nullptr /* extension */);

    if (dir.empty())
    {
        dir = wxFileName::GetTempDir();
    }

#if defined(__WINDOWS__)
    if (!::GetTempFileName(dir.t_str(), name.t_str(), 0,
                           wxStringBuffer(path, MAX_PATH + 1)))
    {
        wxLogLastError(wxT("GetTempFileName"));

        path.clear();
    }

#else // !Windows
    path = dir;

    if ( !wxEndsWithPathSeparator(dir) &&
            (name.empty() || !wxIsPathSeparator(name[0u])) )
    {
        path += wxFILE_SEP_PATH;
    }

    path += name;

#if defined(HAVE_MKSTEMP)
    // scratch space for mkstemp()
    path += wxT("XXXXXX");

    // we need to copy the path to the buffer in which mkstemp() can modify it
    wxCharBuffer buf(path.fn_str());

    // cast is safe because the string length doesn't change
    int fdTemp = mkstemp(const_cast<char*>(static_cast<const char*>(buf)));
    if ( fdTemp == -1 )
    {
        // this might be not necessary as mkstemp() on most systems should have
        // already done it but it doesn't hurt either...
        path.clear();
    }
    else // mkstemp() succeeded
    {
        path = wxConvFile.cMB2WX( (const char*) buf );

    #if wxUSE_FILE
        // avoid leaking the fd
        if ( fileTemp )
        {
            fileTemp->Attach(fdTemp);
        }
        else
    #endif

    #if wxUSE_FFILE
        if ( ffileTemp )
        {
        #ifdef wx_fdopen
            ffileTemp->Attach(wx_fdopen(fdTemp, "r+b"), path);
        #else
            ffileTemp->Open(path, wxT("r+b"));
            close(fdTemp);
        #endif
        }
        else
    #endif

        {
            close(fdTemp);
        }
    }
#else // !HAVE_MKSTEMP

#ifdef HAVE_MKTEMP
    // same as above
    path += wxT("XXXXXX");

    wxCharBuffer buf = wxConvFile.cWX2MB( path );
    if ( !mktemp( (char*)(const char*) buf ) )
    {
        path.clear();
    }
    else
    {
        path = wxConvFile.cMB2WX( (const char*) buf );
    }
#else // !HAVE_MKTEMP
    // generate the unique file name ourselves
    path << (unsigned int)getpid();

    wxString pathTry;

    static const size_t numTries = 1000;
    for ( size_t n = 0; n < numTries; n++ )
    {
        // 3 hex digits is enough for numTries == 1000 < 4096
        pathTry = path + wxString::Format(wxT("%.03x"), (unsigned int) n);
        if ( !wxFileName::FileExists(pathTry) )
        {
            break;
        }

        pathTry.clear();
    }

    path = pathTry;
#endif // HAVE_MKTEMP/!HAVE_MKTEMP

#endif // HAVE_MKSTEMP/!HAVE_MKSTEMP

#endif // Windows/!Windows

    if ( path.empty() )
    {
        wxLogSysError(_("Failed to create a temporary file name"));
    }
    else
    {
        bool ok = true;

        // open the file - of course, there is a race condition here, this is
        // why we always prefer using mkstemp()...
    #if wxUSE_FILE
        if ( fileTemp && !fileTemp->IsOpened() )
        {
            *deleteOnClose = wantDeleteOnClose;
            int fd = wxTempOpen(path, deleteOnClose);
            if (fd != -1)
                fileTemp->Attach(fd);
            else
                ok = false;
        }
    #endif

    #if wxUSE_FFILE
        if ( ffileTemp && !ffileTemp->IsOpened() )
        {
            *deleteOnClose = wantDeleteOnClose;
            ok = wxTempOpen(ffileTemp, path, deleteOnClose);
        }
    #endif

        if ( !ok )
        {
            // FIXME: If !ok here should we loop and try again with another
            //        file name?  That is the standard recourse if open(O_EXCL)
            //        fails, though of course it should be protected against
            //        possible infinite looping too.

            wxLogError(_("Failed to open temporary file."));

            path.clear();
        }
    }

    return path;
}


static bool wxCreateTempImpl(
        const wxString& prefix,
        WXFILEARGS(wxFile *fileTemp, wxFFile *ffileTemp),
        wxString *name)
{
    bool deleteOnClose = true;

    *name = wxCreateTempImpl(prefix,
                             WXFILEARGS(fileTemp, ffileTemp),
                             &deleteOnClose);

    bool ok = !name->empty();

    if (deleteOnClose)
        name->clear();
#ifdef __UNIX__
    else if (ok && wxRemoveFile(*name))
        name->clear();
#endif

    return ok;
}


static void wxAssignTempImpl(
        wxFileName *fn,
        const wxString& prefix,
        WXFILEARGS(wxFile *fileTemp, wxFFile *ffileTemp))
{
    wxString tempname;
    tempname = wxCreateTempImpl(prefix, WXFILEARGS(fileTemp, ffileTemp));

    if ( tempname.empty() )
    {
        // error, failed to get temp file name
        fn->Clear();
    }
    else // ok
    {
        fn->Assign(tempname);
    }
}


void wxFileName::AssignTempFileName(const wxString& prefix)
{
    wxAssignTempImpl(this, prefix, WXFILEARGS(nullptr, nullptr));
}

/* static */
wxString wxFileName::CreateTempFileName(const wxString& prefix)
{
    return wxCreateTempImpl(prefix, WXFILEARGS(nullptr, nullptr));
}

#endif // wxUSE_FILE || wxUSE_FFILE


#if wxUSE_FILE

wxString wxCreateTempFileName(const wxString& prefix,
                              wxFile *fileTemp,
                              bool *deleteOnClose)
{
    return wxCreateTempImpl(prefix, WXFILEARGS(fileTemp, nullptr), deleteOnClose);
}

bool wxCreateTempFile(const wxString& prefix,
                      wxFile *fileTemp,
                      wxString *name)
{
    return wxCreateTempImpl(prefix, WXFILEARGS(fileTemp, nullptr), name);
}

void wxFileName::AssignTempFileName(const wxString& prefix, wxFile *fileTemp)
{
    wxAssignTempImpl(this, prefix, WXFILEARGS(fileTemp, nullptr));
}

/* static */
wxString
wxFileName::CreateTempFileName(const wxString& prefix, wxFile *fileTemp)
{
    return wxCreateTempFileName(prefix, fileTemp);
}

#endif // wxUSE_FILE


#if wxUSE_FFILE

wxString wxCreateTempFileName(const wxString& prefix,
                              wxFFile *fileTemp,
                              bool *deleteOnClose)
{
    return wxCreateTempImpl(prefix, WXFILEARGS(nullptr, fileTemp), deleteOnClose);
}

bool wxCreateTempFile(const wxString& prefix,
                      wxFFile *fileTemp,
                      wxString *name)
{
    return wxCreateTempImpl(prefix, WXFILEARGS(nullptr, fileTemp), name);

}

void wxFileName::AssignTempFileName(const wxString& prefix, wxFFile *fileTemp)
{
    wxAssignTempImpl(this, prefix, WXFILEARGS(nullptr, fileTemp));
}

/* static */
wxString
wxFileName::CreateTempFileName(const wxString& prefix, wxFFile *fileTemp)
{
    return wxCreateTempFileName(prefix, fileTemp);
}

#endif // wxUSE_FFILE


// ----------------------------------------------------------------------------
// directory operations
// ----------------------------------------------------------------------------

// helper of GetTempDir(): check if the given directory exists and return it if
// it does or an empty string otherwise
namespace
{

wxString CheckIfDirExists(const wxString& dir)
{
    return wxFileName::DirExists(dir) ? dir : wxString();
}

} // anonymous namespace

wxString wxFileName::GetTempDir()
{
    // first try getting it from environment: this allows overriding the values
    // used by default if the user wants to create temporary files in another
    // directory
    wxString dir = CheckIfDirExists(wxGetenv("TMPDIR"));
    if ( dir.empty() )
    {
        dir = CheckIfDirExists(wxGetenv("TMP"));
        if ( dir.empty() )
            dir = CheckIfDirExists(wxGetenv("TEMP"));
    }

    // if no environment variables are set, use the system default
    if ( dir.empty() )
    {
#if defined(__WINDOWS__)
        if ( !::GetTempPath(MAX_PATH, wxStringBuffer(dir, MAX_PATH + 1)) )
        {
            wxLogLastError(wxT("GetTempPath"));
        }
#endif // systems with native way
    }

    if ( !dir.empty() )
    {
        // remove any trailing path separators, we don't want to ever return
        // them from this function for consistency
        const size_t lastNonSep = dir.find_last_not_of(GetPathSeparators());
        if ( lastNonSep == wxString::npos )
        {
            // the string consists entirely of separators, leave only one
            dir = GetPathSeparator();
        }
        else
        {
            dir.erase(lastNonSep + 1);
        }
    }

    // fall back to hard coded value
    else
    {
#ifdef __UNIX_LIKE__
        dir = CheckIfDirExists("/tmp");
        if ( dir.empty() )
#endif // __UNIX_LIKE__
            dir = ".";
    }

    return dir;
}

bool wxFileName::Mkdir( int perm, int flags ) const
{
    return wxFileName::Mkdir(GetPath(), perm, flags);
}

bool wxFileName::Mkdir( const wxString& dir, int perm, int flags )
{
    if ( flags & wxPATH_MKDIR_FULL )
    {
        // split the path in components
        wxFileName filename;
        filename.AssignDir(dir);

        // create the directories one by one
        wxFileName currPath(filename);
        currPath.m_dirs.Clear();

        for ( const auto& pathComponent : filename.GetDirs() )
        {
            currPath.AppendDir(pathComponent);

            if (!currPath.DirExists())
            {
                if (!wxMkdir(currPath.GetPath(), perm))
                {
                    // no need to try creating further directories
                    return false;
                }
            }
        }

        return true;

    }

    return ::wxMkdir( dir, perm );
}

bool wxFileName::Rmdir(int flags) const
{
    return wxFileName::Rmdir( GetPath(), flags );
}

bool wxFileName::Rmdir(const wxString& dir, int flags)
{
#ifdef __WINDOWS__
    if ( flags & wxPATH_RMDIR_RECURSIVE )
    {
        // SHFileOperation needs double null termination string
        // but without separator at the end of the path
        wxString path(dir);
        if ( path.Last() == wxFILE_SEP_PATH )
            path.RemoveLast();
        path += wxT('\0');

        SHFILEOPSTRUCT fileop;
        wxZeroMemory(fileop);
        fileop.wFunc = FO_DELETE;
        fileop.pFrom = path.t_str();
        fileop.fFlags = FOF_SILENT | FOF_NOCONFIRMATION;
        fileop.fFlags |= FOF_NOERRORUI;

        int ret = SHFileOperation(&fileop);
        if ( ret != 0 )
        {
            // SHFileOperation may return non-Win32 error codes, so don't use
            // wxLogApiError() as the error message used by it could be wrong.
            wxLogDebug(wxS("SHFileOperation(FO_DELETE) failed: error 0x%08x"),
                       ret);
            return false;
        }

        return true;
    }
    else if ( flags & wxPATH_RMDIR_FULL )
#else // !__WINDOWS__
    if ( flags != 0 )   // wxPATH_RMDIR_FULL or wxPATH_RMDIR_RECURSIVE
#endif // !__WINDOWS__
    {
#ifndef __WINDOWS__
        if ( flags & wxPATH_RMDIR_RECURSIVE )
        {
            // When deleting the tree recursively, we are supposed to delete
            // this directory itself even when it is a symlink -- but without
            // following it. Do it here as wxRmdir() would simply follow if
            // called for a symlink.
            if ( wxFileName::Exists(dir, wxFILE_EXISTS_SYMLINK) )
            {
                return wxRemoveFile(dir);
            }
        }
#endif // !__WINDOWS__

        wxString path(dir);
        if ( path.Last() != wxFILE_SEP_PATH )
            path += wxFILE_SEP_PATH;

        wxDir d(path);

        if ( !d.IsOpened() )
            return false;

        wxString filename;

        // First delete all subdirectories: notice that we don't follow
        // symbolic links, potentially leading outside this directory, to avoid
        // unpleasant surprises.
        bool cont = d.GetFirst(&filename, wxString(),
                               wxDIR_DIRS | wxDIR_HIDDEN | wxDIR_NO_FOLLOW);
        while ( cont )
        {
            wxFileName::Rmdir(path + filename, flags);
            cont = d.GetNext(&filename);
        }

#ifndef __WINDOWS__
        if ( flags & wxPATH_RMDIR_RECURSIVE )
        {
            // Delete all files too and, for the same reasons as above, don't
            // follow symlinks which could refer to the files outside of this
            // directory and just delete the symlinks themselves.
            cont = d.GetFirst(&filename, wxString(),
                              wxDIR_FILES | wxDIR_HIDDEN | wxDIR_NO_FOLLOW);
            while ( cont )
            {
                ::wxRemoveFile(path + filename);
                cont = d.GetNext(&filename);
            }
        }
#endif // !__WINDOWS__
    }

    return ::wxRmdir(dir);
}

// ----------------------------------------------------------------------------
// path normalization
// ----------------------------------------------------------------------------

bool wxFileName::Normalize(int flags,
                           const wxString& cwd,
                           wxPathFormat format)
{
    // deal with env vars renaming first as this may seriously change the path
    if ( flags & wxPATH_NORM_ENV_VARS )
    {
        wxString pathOrig = GetFullPath(format);
        wxString path = wxExpandEnvVars(pathOrig);
        if ( path != pathOrig )
        {
            Assign(path);
        }
    }

    // the existing path components
    wxArrayString dirs = GetDirs();

    // the path to prepend in front to make the path absolute
    wxFileName curDir;

    format = GetFormat(format);

    // set up the directory to use for making the path absolute later
    if ( (flags & wxPATH_NORM_ABSOLUTE) && !IsAbsolute(format) )
    {
        if ( cwd.empty() )
        {
            curDir.AssignCwd(GetVolume());
        }
        else // cwd provided
        {
            curDir.AssignDir(cwd);
        }
    }

    // handle ~ stuff under Unix only
    if ( (format == wxPATH_UNIX) && (flags & wxPATH_NORM_TILDE) && m_relative )
    {
        if ( !dirs.IsEmpty() )
        {
            wxString dir = dirs[0u];
            if ( !dir.empty() && dir[0u] == wxT('~') )
            {
                // to make the path absolute use the home directory
                curDir.AssignDir(wxGetUserHome(dir.c_str() + 1));
                dirs.RemoveAt(0u);
            }
        }
    }

    // transform relative path into abs one
    if ( curDir.IsOk() )
    {
        // this path may be relative because it doesn't have the volume name
        // and still have m_relative=true; in this case we shouldn't modify
        // our directory components but just set the current volume
        if ( !HasVolume() && curDir.HasVolume() )
        {
            SetVolume(curDir.GetVolume());

            if ( !m_relative )
        {
                // yes, it was the case - we don't need curDir then
                curDir.Clear();
            }
        }

        // finally, prepend curDir to the dirs array
        wxArrayString dirsNew = curDir.GetDirs();
        WX_PREPEND_ARRAY(dirs, dirsNew);

        // if we used e.g. tilde expansion previously and wxGetUserHome didn't
        // return for some reason an absolute path, then curDir maybe not be absolute!
        if ( !curDir.m_relative )
        {
            // we have prepended an absolute path and thus we are now an absolute
            // file name too
            m_relative = false;
        }
        // else if (flags & wxPATH_NORM_ABSOLUTE):
        //   should we warn the user that we didn't manage to make the path absolute?
    }

    // now deal with ".", ".." and the rest
    m_dirs.Empty();
    size_t count = dirs.GetCount();
    for ( size_t n = 0; n < count; n++ )
    {
        wxString dir = dirs[n];

        if ( flags & wxPATH_NORM_DOTS )
        {
            if ( dir == wxT(".") )
            {
                // just ignore
                continue;
            }

            if ( dir == wxT("..") )
            {
                if ( m_dirs.empty() )
                {
                    // We have more ".." than directory components so far.
                    // Don't treat this as an error as the path could have been
                    // entered by user so try to handle it reasonably: if the
                    // path is absolute, just ignore the extra ".." because
                    // "/.." is the same as "/". Otherwise, i.e. for relative
                    // paths, keep ".." unchanged because removing it would
                    // modify the file a relative path refers to.
                    if ( !m_relative )
                        continue;

                }
                else // Normal case, go one step up unless it's .. as well.
                {
                    if (m_dirs.back() != wxT("..") )
                    {
                        m_dirs.pop_back();
                        continue;
                    }
                }
            }
        }

        m_dirs.Add(dir);
    }

#if defined(__WINDOWS__) && wxUSE_OLE
    if ( (flags & wxPATH_NORM_SHORTCUT) )
    {
        wxString filename;
        if (GetShortcutTarget(GetFullPath(format), filename))
        {
            m_relative = false;
            Assign(filename);
        }
    }
#endif

#if defined(__WINDOWS__)
    if ( (flags & wxPATH_NORM_LONG) && (format == wxPATH_DOS) )
    {
        Assign(GetLongPath());
    }
#endif // Win32

    // Change case  (this should be kept at the end of the function, to ensure
    // that the path doesn't change any more after we normalize its case)
    if ( (flags & wxPATH_NORM_CASE) && !IsCaseSensitive(format) )
    {
        m_volume.MakeLower();
        m_name.MakeLower();
        m_ext.MakeLower();

        // directory entries must be made lower case as well
        count = m_dirs.GetCount();
        for ( size_t i = 0; i < count; i++ )
        {
            m_dirs[i].MakeLower();
        }
    }

    return true;
}

bool wxFileName::ReplaceEnvVariable(const wxString& envname,
                                    const wxString& replacementFmtString,
                                    wxPathFormat format)
{
    // look into stringForm for the contents of the given environment variable
    wxString val;
    if (envname.empty() ||
        !wxGetEnv(envname, &val))
        return false;
    if (val.empty())
        return false;

    wxString stringForm = GetPath(wxPATH_GET_VOLUME, format);
        // do not touch the file name and the extension

    wxString replacement = wxString::Format(replacementFmtString, envname);
    stringForm.Replace(val, replacement);

    // Now assign ourselves the modified path:
    Assign(stringForm, GetFullName(), format);

    return true;
}

bool wxFileName::ReplaceHomeDir(wxPathFormat format)
{
    wxString homedir = wxGetHomeDir();
    if (homedir.empty())
        return false;

    // Avoid replacing the leading "/" with "~", this would result in an
    // invalid path, if nothing else
    if (homedir == '/')
        return true; // but it is not an error, so don't return false

    wxString stringForm = GetPath(wxPATH_GET_VOLUME, format);
        // do not touch the file name and the extension

    wxString rest;
    if ( stringForm.StartsWith(homedir, &rest) )
    {
        stringForm = "~";
        stringForm += rest;
    }

    // Now assign ourselves the modified path:
    Assign(stringForm, GetFullName(), format);

    return true;
}

// ----------------------------------------------------------------------------
// get the shortcut target
// ----------------------------------------------------------------------------

#if defined(__WINDOWS__) && wxUSE_OLE

bool wxFileName::GetShortcutTarget(const wxString& shortcutPath,
                                   wxString& targetFilename,
                                   wxString* arguments) const
{
    wxString path, file, ext;
    wxFileName::SplitPath(shortcutPath, & path, & file, & ext);

    HRESULT hres;
    wxCOMPtr<IShellLink> psl;
    bool success = false;

    // Assume it's not a shortcut if it doesn't end with lnk
    if (ext.CmpNoCase(wxT("lnk"))!=0)
        return false;

    // Ensure OLE is initialized.
    wxOleInitializer oleInit;

    // create a ShellLink object
    hres = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                            IID_IShellLink, (LPVOID*) &psl);

    if (SUCCEEDED(hres))
    {
        wxCOMPtr<IPersistFile> ppf;
        hres = psl->QueryInterface( IID_IPersistFile, (LPVOID *) &ppf);
        if (SUCCEEDED(hres))
        {
            WCHAR wsz[MAX_PATH];

            MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, shortcutPath.mb_str(), -1, wsz,
                                MAX_PATH);

            hres = ppf->Load(wsz, 0);
            if (SUCCEEDED(hres))
            {
                wxChar buf[2048];
                psl->GetPath(buf, 2048, nullptr, SLGP_UNCPRIORITY);
                targetFilename = wxString(buf);
                success = (shortcutPath != targetFilename);

                psl->GetArguments(buf, 2048);
                wxString args(buf);
                if (!args.empty() && arguments)
                {
                    *arguments = args;
                }
            }
        }
    }
    return success;
}

#endif // __WINDOWS__

// ----------------------------------------------------------------------------
// Resolve links
// ----------------------------------------------------------------------------

wxFileName wxFileName::ResolveLink()
{
    wxFileName linkTarget( *this );

// Only resolve links on platforms with readlink (e.g. Unix-like platforms)
#if defined(wxHAS_NATIVE_READLINK)
    const wxString link = GetFullPath();
    wxStructStat st;

    // This means the link itself doesn't exist, so return an empty filename
    if ( !StatAny(st, link, wxFILE_EXISTS_NO_FOLLOW) )
    {
        linkTarget.Clear();
        return linkTarget;
    }

    // If it isn't an actual link, bail out just and return the link as the result
    if ( !S_ISLNK(st.st_mode) )
        return linkTarget;

    // Dynamically compute the buffer size from the stat call, but fallback if it isn't valid
    int bufSize = 4096;
    if( st.st_size != 0 )
        bufSize = st.st_size + 1;

    wxVector<char> bufData(bufSize);
    char* const buf = &bufData[0];
    ssize_t result = wxReadlink(link, buf, bufSize - 1);

    if ( result != -1 )
    {
        buf[result] = '\0'; // readlink() doesn't NUL-terminate the buffer
        linkTarget.Assign( wxString(buf, wxConvLibc) );

        // Ensure the resulting path is absolute since readlink can return paths relative to the link
        if ( !linkTarget.IsAbsolute() )
            linkTarget.MakeAbsolute(GetPath());
    }
    else
    {
        // This means the lookup failed for some reason
        linkTarget.Clear();
    }
#endif

    return linkTarget;
}

// ----------------------------------------------------------------------------
// absolute/relative paths
// ----------------------------------------------------------------------------

bool wxFileName::IsAbsolute(wxPathFormat format) const
{
    // unix paths beginning with ~ are reported as being absolute
    if ( format == wxPATH_UNIX )
    {
        if ( !m_dirs.IsEmpty() )
        {
            wxString dir = m_dirs[0u];

            if (!dir.empty() && dir[0u] == wxT('~'))
                return true;
        }
    }

    // if our path doesn't start with a path separator, it's not an absolute
    // path
    if ( m_relative )
        return false;

    if ( !GetVolumeSeparator(format).empty() )
    {
        // this format has volumes and an absolute path must have one, it's not
        // enough to have the full path to be an absolute file under Windows
        if ( GetVolume().empty() )
            return false;
    }

    return true;
}

bool wxFileName::MakeRelativeTo(const wxString& pathBase, wxPathFormat format)
{
    wxFileName fnBase = wxFileName::DirName(pathBase, format);

    // get cwd only once - small time saving
    wxString cwd = wxGetCwd();

    // Bring both paths to canonical form.
    MakeAbsolute(cwd, format);
    fnBase.MakeAbsolute(cwd, format);

    // Do this here for compatibility, as we used to do it before.
    Normalize(wxPATH_NORM_LONG, cwd, format);
    fnBase.Normalize(wxPATH_NORM_LONG, cwd, format);


    bool withCase = IsCaseSensitive(format);

    // we can't do anything if the files live on different volumes
    if ( !GetVolume().IsSameAs(fnBase.GetVolume(), withCase) )
    {
        // nothing done
        return false;
    }

    // same drive, so we don't need our volume
    m_volume.clear();

    // remove common directories starting at the top
    while ( !m_dirs.IsEmpty() && !fnBase.m_dirs.IsEmpty() &&
                m_dirs[0u].IsSameAs(fnBase.m_dirs[0u], withCase) )
    {
        m_dirs.RemoveAt(0);
        fnBase.m_dirs.RemoveAt(0);
    }

    // add as many ".." as needed
    size_t count = fnBase.m_dirs.GetCount();
    for ( size_t i = 0; i < count; i++ )
    {
        m_dirs.Insert(wxT(".."), 0u);
    }

    switch ( GetFormat(format) )
    {
        case wxPATH_NATIVE:
        case wxPATH_MAX:
            wxFAIL_MSG( wxS("unreachable") );
            wxFALLTHROUGH;

        case wxPATH_UNIX:
        case wxPATH_DOS:
            // a directory made relative with respect to itself is '.' under
            // Unix and DOS, by definition (but we don't have to insert "./"
            // for the files)
            if ( m_dirs.IsEmpty() && IsDir() )
            {
                m_dirs.Add(wxT('.'));
            }
            break;

        case wxPATH_MAC:
        case wxPATH_VMS:
            break;
    }

    m_relative = true;

    // we were modified
    return true;
}

// ----------------------------------------------------------------------------
// filename kind tests
// ----------------------------------------------------------------------------

bool wxFileName::SameAs(const wxFileName& filepath, wxPathFormat format) const
{
    wxFileName fn1 = *this,
               fn2 = filepath;

    // get cwd only once - small time saving
    wxString cwd = wxGetCwd();

    // apply really all normalizations here
    const int normAll =
        wxPATH_NORM_ENV_VARS |
        wxPATH_NORM_DOTS     |
        wxPATH_NORM_TILDE    |
        wxPATH_NORM_CASE     |
        wxPATH_NORM_ABSOLUTE |
        wxPATH_NORM_LONG     |
        wxPATH_NORM_SHORTCUT
        ;

    fn1.Normalize(normAll, cwd, format);
    fn2.Normalize(normAll, cwd, format);

    if ( fn1.GetFullPath() == fn2.GetFullPath() )
        return true;

#ifdef wxHAVE_LSTAT
    wxStructStat st1, st2;
    if ( StatAny(st1, fn1) && StatAny(st2, fn2) )
    {
        if ( st1.st_ino == st2.st_ino && st1.st_dev == st2.st_dev )
            return true;
    }
    //else: It's not an error if one or both files don't exist.
#endif // wxHAVE_LSTAT

    return false;
}

/* static */
bool wxFileName::IsCaseSensitive( wxPathFormat format )
{
    // only Unix filenames are truly case-sensitive
    return GetFormat(format) == wxPATH_UNIX;
}

/* static */
wxString wxFileName::GetForbiddenChars(wxPathFormat format)
{
    // Inits to forbidden characters that are common to (almost) all platforms.
    wxString strForbiddenChars = wxT("*?");

    // If asserts, wxPathFormat has been changed. In case of a new path format
    // addition, the following code might have to be updated.
    wxCOMPILE_TIME_ASSERT(wxPATH_MAX == 5, wxPathFormatChanged);
    switch ( GetFormat(format) )
    {
        default :
            wxFAIL_MSG( wxT("Unknown path format") );
            wxFALLTHROUGH;

        case wxPATH_UNIX:
            break;

        case wxPATH_MAC:
            // On a Mac even names with * and ? are allowed (Tested with OS
            // 9.2.1 and OS X 10.2.5)
            strForbiddenChars.clear();
            break;

        case wxPATH_DOS:
            strForbiddenChars += wxT("\\/:\"<>|");
            break;

        case wxPATH_VMS:
            break;
    }

    return strForbiddenChars;
}

/* static */
wxString wxFileName::GetVolumeSeparator(wxPathFormat format)
{
    wxString sepVol;

    if ( (GetFormat(format) == wxPATH_DOS) ||
         (GetFormat(format) == wxPATH_VMS) )
    {
        sepVol = wxFILE_SEP_DSK;
    }
    //else: leave empty

    return sepVol;
}

/* static */
wxString wxFileName::GetPathSeparators(wxPathFormat format)
{
    wxString seps;
    switch ( GetFormat(format) )
    {
        case wxPATH_DOS:
            // accept both as native APIs do but put the native one first as
            // this is the one we use in GetFullPath()
            seps << wxFILE_SEP_PATH_DOS << wxFILE_SEP_PATH_UNIX;
            break;

        default:
            wxFAIL_MSG( wxT("Unknown wxPATH_XXX style") );
            wxFALLTHROUGH;

        case wxPATH_UNIX:
            seps = wxFILE_SEP_PATH_UNIX;
            break;

        case wxPATH_MAC:
            seps = wxFILE_SEP_PATH_MAC;
            break;

        case wxPATH_VMS:
            seps = wxFILE_SEP_PATH_VMS;
            break;
    }

    return seps;
}

/* static */
wxString wxFileName::GetPathTerminators(wxPathFormat format)
{
    format = GetFormat(format);

    // under VMS the end of the path is ']', not the path separator used to
    // separate the components
    return format == wxPATH_VMS ? wxString(wxT(']')) : GetPathSeparators(format);
}

/* static */
bool wxFileName::IsPathSeparator(wxChar ch, wxPathFormat format)
{
    // wxString::Find() doesn't work as expected with NUL - it will always find
    // it, so test for it separately
    return ch != wxT('\0') && GetPathSeparators(format).Find(ch) != wxNOT_FOUND;
}

/* static */
bool
wxFileName::IsMSWExtendedLengthPath(const wxString& path, wxPathFormat format)
{
    return GetFormat(format) == wxPATH_DOS &&
            path.StartsWith(wxMSW_EXTENDED_PATH_PREFIX);
}

/* static */
bool
wxFileName::IsMSWUniqueVolumeNamePath(const wxString& path, wxPathFormat format)
{
    // length of \\?\Volume{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}\ string
    constexpr size_t wxMSWUniqueVolumePrefixLength = 49;

    // return true if the format used is the DOS/Windows one and the string begins
    // with a Windows unique volume name ("\\?\Volume{guid}\")
    return GetFormat(format) == wxPATH_DOS &&
            path.length() >= wxMSWUniqueVolumePrefixLength &&
             path.StartsWith(wxS("\\\\?\\Volume{")) &&
              path[wxMSWUniqueVolumePrefixLength - 1] == wxFILE_SEP_PATH_DOS;

}

// ----------------------------------------------------------------------------
// path components manipulation
// ----------------------------------------------------------------------------

/* static */ bool wxFileName::IsValidDirComponent(const wxString& dir)
{
    if ( dir.empty() )
    {
        wxFAIL_MSG( wxT("empty directory passed to wxFileName::InsertDir()") );

        return false;
    }

    const size_t len = dir.length();
    for ( size_t n = 0; n < len; n++ )
    {
        if ( dir[n] == GetVolumeSeparator() || IsPathSeparator(dir[n]) )
        {
            wxFAIL_MSG( wxT("invalid directory component in wxFileName") );

            return false;
        }
    }

    return true;
}

bool wxFileName::AppendDir( const wxString& dir )
{
    if (!IsValidDirComponent(dir))
        return false;
    m_dirs.Add(dir);
    return true;
}

void wxFileName::PrependDir( const wxString& dir )
{
    InsertDir(0, dir);
}

bool wxFileName::InsertDir(size_t before, const wxString& dir)
{
    if (!IsValidDirComponent(dir))
        return false;
    m_dirs.Insert(dir, before);
    return true;
}

void wxFileName::RemoveDir(size_t pos)
{
    m_dirs.RemoveAt(pos);
}

// ----------------------------------------------------------------------------
// accessors
// ----------------------------------------------------------------------------

void wxFileName::SetFullName(const wxString& fullname)
{
    SplitPath(fullname, nullptr /* no volume */, nullptr /* no path */,
                        &m_name, &m_ext, &m_hasExt);
}

wxString wxFileName::GetFullName() const
{
    wxString fullname = m_name;
    if ( m_hasExt )
    {
        fullname << wxFILE_SEP_EXT << m_ext;
    }

    return fullname;
}

wxString wxFileName::DoGetPath( int flags, wxPathFormat format ) const
{
    format = GetFormat( format );

    wxString fullpath;

    // return the volume with the path as well if requested
    if ( flags & wxPATH_GET_VOLUME )
    {
        fullpath += wxGetVolumeString(GetVolume(), format);
    }

    // the leading character
    switch ( format )
    {
        case wxPATH_MAC:
            if ( m_relative )
                fullpath += wxFILE_SEP_PATH_MAC;
            break;

        case wxPATH_DOS:
            if ( !m_relative )
                fullpath += wxFILE_SEP_PATH_DOS;
            break;

        default:
            wxFAIL_MSG( wxT("Unknown path format") );
            wxFALLTHROUGH;

        case wxPATH_UNIX:
            if ( !m_relative )
            {
                fullpath += wxFILE_SEP_PATH_UNIX;
            }
            break;

        case wxPATH_VMS:
            // no leading character here but use this place to unset
            // wxPATH_GET_SEPARATOR flag: under VMS it doesn't make sense
            // as, if I understand correctly, there should never be a dot
            // before the closing bracket
            flags &= ~wxPATH_GET_SEPARATOR;
    }

    if ( m_dirs.empty() )
    {
        // there is nothing more
        return fullpath;
    }

    // then concatenate all the path components using the path separator
    if ( format == wxPATH_VMS )
    {
        fullpath += wxT('[');
    }

    const size_t dirCount = m_dirs.GetCount();
    for ( size_t i = 0; i < dirCount; i++ )
    {
        switch (format)
        {
            case wxPATH_MAC:
                if ( m_dirs[i] == wxT(".") )
                {
                    // skip appending ':', this shouldn't be done in this
                    // case as "::" is interpreted as ".." under Unix
                    continue;
                }

                // convert back from ".." to nothing
                if ( !m_dirs[i].IsSameAs(wxT("..")) )
                     fullpath += m_dirs[i];
                break;

            default:
                wxFAIL_MSG( wxT("Unexpected path format") );
                wxFALLTHROUGH;

            case wxPATH_DOS:
            case wxPATH_UNIX:
                fullpath += m_dirs[i];
                break;

            case wxPATH_VMS:
                // TODO: What to do with ".." under VMS

                // convert back from ".." to nothing
                if ( !m_dirs[i].IsSameAs(wxT("..")) )
                    fullpath += m_dirs[i];
                break;
        }

        if ( (flags & wxPATH_GET_SEPARATOR) || (i != dirCount - 1) )
            fullpath += GetPathSeparator(format);
    }

    if ( format == wxPATH_VMS )
    {
        fullpath += wxT(']');
    }

    return fullpath;
}

wxString wxFileName::GetPath( int flags, wxPathFormat format ) const
{
    wxString fullpath = DoGetPath(flags, format);

#ifdef __WINDOWS__
    // Paths have to use "extended length" form to be longer than MAX_PATH
    // under Windows, check if we need to use it.
    if ( GetFormat(format) == wxPATH_DOS && (flags & wxPATH_GET_VOLUME) )
    {
        // Extended length paths can't be relative and can't contain any
        // periods etc, so normalize the path first.
        wxFileName fnAbs(*this);
        fnAbs.MakeAbsolute();
        const wxString absPath = fnAbs.DoGetPath(flags, format);

        // No need to do anything if it fits: note that normally paths up to
        // MAX_PATH should work but in practice the limit is lower than that
        // depending on whether it's a file or a directory, whether it's in the
        // root directory or a subdirectory, Windows version and probably the
        // phase of the moon as well, so keep things simple and use the lowest
        // known limit which is 248 (which is MAX_PATH minus 12, where 12 is,
        // apparently, the length of a 8.3 filename) characters for a directory:
        // it does no real harm to use extended length limit paths for shorter
        // paths while not using them would result in a "file not found" error.
        if ( absPath.length() < 248 )
            return fullpath;

        // But if it doesn't, we have to switch to using absolute path and
        // modify it to use the extended length form.
        fullpath = absPath;

        // We can switch to extended length paths for paths using normal driver
        // letters or UNC paths.
        if ( fullpath[1] == GetVolumeSeparator() )
        {
            // Turn C: into \\?\C:
            fullpath.insert(0, wxMSW_EXTENDED_PATH_PREFIX);
        }
        else if ( fullpath.StartsWith(R"(\\)") && fullpath[2] != '?' )
        {
            // Turn \\share into \\?\UNC\share
            fullpath.insert(1, R"(\?\UNC)");
        }
    }
#endif // __WINDOWS__

    return fullpath;
}

wxString wxFileName::GetFullPath( wxPathFormat format ) const
{
    // we already have a function to get the path
    wxString fullpath = GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR,
                                format);

    // now just add the file name and extension to it
    fullpath += GetFullName();

    return fullpath;
}

// Return the short form of the path (returns identity on non-Windows platforms)
wxString wxFileName::GetShortPath() const
{
    wxString path(GetFullPath());

#if defined(__WINDOWS__)
    DWORD sz = ::GetShortPathName(path.t_str(), nullptr, 0);
    if ( sz != 0 )
    {
        wxString pathOut;
        if ( ::GetShortPathName
               (
                path.t_str(),
                wxStringBuffer(pathOut, sz),
                sz
               ) != 0 )
        {
            return pathOut;
        }
    }
#endif // Windows

    return path;
}

// Return the long form of the path (returns identity on non-Windows platforms)
wxString wxFileName::GetLongPath() const
{
    wxString pathOut,
             path = GetFullPath();

#if defined(__WINDOWS__)

    DWORD dwSize = ::GetLongPathName(path.t_str(), nullptr, 0);
    if ( dwSize > 0 )
    {
        if ( ::GetLongPathName
                (
                path.t_str(),
                wxStringBuffer(pathOut, dwSize),
                dwSize
                ) != 0 )
        {
            return pathOut;
        }
    }
    else // GetLongPathName() failed.
    {
        // The error returned for non-existent UNC paths is different, to make
        // things more interesting.
        const DWORD err = ::GetLastError();
        if ( err == ERROR_FILE_NOT_FOUND || err == ERROR_BAD_NETPATH )
        {
            // No need to try to do anything else, we're not going to be able
            // to find a long path form of a non-existent path anyhow.
            return path;
        }
    }

    // File exists, but some other error occurred.
    // We need to call FindFirstFile on each component in turn.

    WIN32_FIND_DATA findFileData;
    HANDLE hFind;

    if ( HasVolume() )
        pathOut = wxGetVolumeString(GetVolume(), wxPATH_DOS) +
                  GetPathSeparator(wxPATH_DOS);
    else
        pathOut.clear();

    wxArrayString dirs = GetDirs();
    dirs.Add(GetFullName());

    wxString tmpPath;

    size_t count = dirs.GetCount();
    for ( size_t i = 0; i < count; i++ )
    {
        const wxString& dir = dirs[i];

        // We're using pathOut to collect the long-name path, but using a
        // temporary for appending the last path component which may be
        // short-name
        tmpPath = pathOut + dir;

        // We must not process "." or ".." here as they would be (unexpectedly)
        // replaced by the corresponding directory names so just leave them
        // alone
        //
        // And we can't pass a drive and root dir to FindFirstFile (VZ: why?)
        if ( tmpPath.empty() || dir == '.' || dir == ".." ||
                tmpPath.Last() == GetVolumeSeparator(wxPATH_DOS) )
        {
            tmpPath += wxFILE_SEP_PATH;
            pathOut = tmpPath;
            continue;
        }

        hFind = ::FindFirstFile(tmpPath.t_str(), &findFileData);
        if (hFind == INVALID_HANDLE_VALUE)
        {
            // Error: most likely reason is that path doesn't exist, so
            // append any unprocessed parts and return
            for ( i += 1; i < count; i++ )
                tmpPath += wxFILE_SEP_PATH + dirs[i];

            return tmpPath;
        }

        pathOut += findFileData.cFileName;
        if ( (i < (count-1)) )
            pathOut += wxFILE_SEP_PATH;

        ::FindClose(hFind);
    }
#else // !Win32
    pathOut = path;
#endif // Win32/!Win32

    return pathOut;
}

wxPathFormat wxFileName::GetFormat( wxPathFormat format )
{
    if (format == wxPATH_NATIVE)
    {
#if defined(__WINDOWS__)
        format = wxPATH_DOS;
#elif defined(__VMS)
        format = wxPATH_VMS;
#else
        format = wxPATH_UNIX;
#endif
    }
    return format;
}

#ifdef wxHAS_FILESYSTEM_VOLUMES

/* static */
wxString wxFileName::GetVolumeString(char drive, int flags)
{
    wxASSERT_MSG( !(flags & ~wxPATH_GET_SEPARATOR), "invalid flag specified" );

    wxString vol(drive);
    vol += wxFILE_SEP_DSK;
    if ( flags & wxPATH_GET_SEPARATOR )
        vol += wxFILE_SEP_PATH;

    return vol;
}

#endif // wxHAS_FILESYSTEM_VOLUMES

// ----------------------------------------------------------------------------
// path splitting function
// ----------------------------------------------------------------------------

/* static */
void
wxFileName::SplitVolume(const wxString& fullpath,
                        wxString *pstrVolume,
                        wxString *pstrPath,
                        wxPathFormat format)
{
    format = GetFormat(format);

    wxString pathOnly;

    switch ( format )
    {
        case wxPATH_DOS:
            // Deal with MSW complications first: first, the special case of
            // extended-length paths.
            if ( fullpath.StartsWith(wxMSW_EXTENDED_PATH_PREFIX) )
            {
                // Find the next path separator after this prefix.
                //
                // Note that such paths contain only backslashes, never slashes.
                const auto posNextSep =
                    fullpath.find(wxFILE_SEP_PATH_DOS,
                                  wxMSW_EXTENDED_PATH_PREFIX_LEN);

                // Note that this works even if posNextSep is npos.
                if ( pstrVolume )
                    *pstrVolume = fullpath(0, posNextSep);

                // Extended-length paths must have a backslash after the volume
                // but if they ever don't, still pretend that there is one at
                // the end because this is not going to be a normal path
                // anyhow, so this seems like the least useless thing we can do.
                if ( posNextSep != wxString::npos )
                    pathOnly = fullpath.substr(posNextSep);
                else
                    pathOnly = wxFILE_SEP_PATH_DOS;
                break;
            }

            // Next check for UNC \\share\path syntax.
            if ( IsUNCPath(fullpath) )
            {
                // Note that IsUNCPath() checks that 3rd character is not a
                // (back)slash.
                size_t posFirstSlash =
                    fullpath.find_first_of(GetPathTerminators(format), 3);
                if ( posFirstSlash != wxString::npos )
                {
                    if ( pstrVolume )
                        *pstrVolume = fullpath.Left(posFirstSlash);
                    pathOnly = fullpath.Mid(posFirstSlash);
                }
                else // UNC path to the root of the share (just "\\share")
                {
                    if ( pstrVolume )
                        *pstrVolume = fullpath;
                }

                // In any case, normalize slashes to backslashes, which are canonical
                // separators for the UNC paths.
                if ( pstrVolume )
                {
                    (*pstrVolume)[0] =
                    (*pstrVolume)[1] = '\\';
                }

                break;
            }

            wxFALLTHROUGH;

        case wxPATH_VMS:
            {
                wxString sepVol = GetVolumeSeparator(format);

                // we have to exclude the case of a colon in the very beginning of the
                // string as it can't be a volume separator (nor can this be a valid
                // DOS file name at all but we'll leave dealing with this to our caller)
                size_t posFirstColon = fullpath.find_first_of(sepVol);
                if ( posFirstColon && posFirstColon != wxString::npos )
                {
                    if ( pstrVolume )
                    {
                        *pstrVolume = fullpath.Left(posFirstColon);
                    }

                    // remove the volume name and the separator from the full path
                    pathOnly = fullpath.Mid(posFirstColon + 1);
                }
                else
                {
                    pathOnly = fullpath;
                }
            }
            break;

        case wxPATH_MAC:
        case wxPATH_UNIX:
            // Volumes are not used in paths in this format.
            pathOnly = fullpath;
            break;

        case wxPATH_NATIVE:
        case wxPATH_MAX:
            wxFAIL_MSG( wxS("unreachable") );
            break;
    }

    if ( pstrPath )
        *pstrPath = pathOnly;
}

/* static */
void wxFileName::SplitPath(const wxString& fullpathWithVolume,
                           wxString *pstrVolume,
                           wxString *pstrPath,
                           wxString *pstrName,
                           wxString *pstrExt,
                           bool *hasExt,
                           wxPathFormat format)
{
    format = GetFormat(format);

    wxString fullpath;
    SplitVolume(fullpathWithVolume, pstrVolume, &fullpath, format);

    // find the positions of the last dot and last path separator in the path
    size_t posLastDot = fullpath.find_last_of(wxFILE_SEP_EXT);
    size_t posLastSlash = fullpath.find_last_of(GetPathTerminators(format));

    // check whether this dot occurs at the very beginning of a path component
    if ( (posLastDot != wxString::npos) &&
         (posLastDot == 0 ||
            IsPathSeparator(fullpath[posLastDot - 1]) ||
            (format == wxPATH_VMS && fullpath[posLastDot - 1] == wxT(']'))) )
    {
        // dot may be (and commonly -- at least under Unix -- is) the first
        // character of the filename, don't treat the entire filename as
        // extension in this case
        posLastDot = wxString::npos;
    }

    // if we do have a dot and a slash, check that the dot is in the name part
    if ( (posLastDot != wxString::npos) &&
         (posLastSlash != wxString::npos) &&
         (posLastDot < posLastSlash) )
    {
        // the dot is part of the path, not the start of the extension
        posLastDot = wxString::npos;
    }

    // now fill in the variables provided by user
    if ( pstrPath )
    {
        if ( posLastSlash == wxString::npos )
        {
            // no path at all
            pstrPath->Empty();
        }
        else
        {
            // take everything up to the path separator but take care to make
            // the path equal to something like '/', not empty, for the files
            // immediately under root directory
            size_t len = posLastSlash;

            // this rule does not apply to mac since we do not start with colons (sep)
            // except for relative paths
            if ( !len && format != wxPATH_MAC)
                len++;

            *pstrPath = fullpath.Left(len);

            // special VMS hack: remove the initial bracket
            if ( format == wxPATH_VMS )
            {
                if ( (*pstrPath)[0u] == wxT('[') )
                    pstrPath->erase(0, 1);
            }
        }
    }

    if ( pstrName )
    {
        // take all characters starting from the one after the last slash and
        // up to, but excluding, the last dot
        size_t nStart = posLastSlash == wxString::npos ? 0 : posLastSlash + 1;
        size_t count;
        if ( posLastDot == wxString::npos )
        {
            // take all until the end
            count = wxString::npos;
        }
        else if ( posLastSlash == wxString::npos )
        {
            count = posLastDot;
        }
        else // have both dot and slash
        {
            count = posLastDot - posLastSlash - 1;
        }

        *pstrName = fullpath.Mid(nStart, count);
    }

    // finally deal with the extension here: we have an added complication that
    // extension may be empty (but present) as in "foo." where trailing dot
    // indicates the empty extension at the end -- and hence we must remember
    // that we have it independently of pstrExt
    if ( posLastDot == wxString::npos )
    {
        // no extension
        if ( pstrExt )
            pstrExt->clear();
        if ( hasExt )
            *hasExt = false;
    }
    else
    {
        // take everything after the dot
        if ( pstrExt )
            *pstrExt = fullpath.Mid(posLastDot + 1);
        if ( hasExt )
            *hasExt = true;
    }
}

/* static */
void wxFileName::SplitPath(const wxString& fullpath,
                           wxString *path,
                           wxString *name,
                           wxString *ext,
                           wxPathFormat format)
{
    wxString volume;
    SplitPath(fullpath, &volume, path, name, ext, format);

    if ( path )
    {
        path->Prepend(wxGetVolumeString(volume, format));
    }
}

/* static */
wxString wxFileName::StripExtension(const wxString& fullpath)
{
    wxFileName fn(fullpath);
    fn.SetExt(wxString());
    return fn.GetFullPath();
}

// ----------------------------------------------------------------------------
// file permissions functions
// ----------------------------------------------------------------------------

bool wxFileName::SetPermissions(int permissions)
{
    // Don't do anything for a symlink but first make sure it is one.
    if ( m_dontFollowLinks &&
            Exists(GetFullPath(), wxFILE_EXISTS_SYMLINK|wxFILE_EXISTS_NO_FOLLOW) )
    {
        // Looks like changing permissions for a symlink is only supported
        // on BSD where lchmod is present and correctly implemented.
        // https://lists.gnu.org/archive/html/bug-coreutils/2009-09/msg00268.html
        return false;
    }

#ifdef __WINDOWS__
    int accMode = 0;

    if ( permissions & (wxS_IRUSR|wxS_IRGRP|wxS_IROTH) )
        accMode = _S_IREAD;

    if ( permissions & (wxS_IWUSR|wxS_IWGRP|wxS_IWOTH) )
        accMode |= _S_IWRITE;

    permissions = accMode;
#endif // __WINDOWS__

    return wxChmod(GetFullPath(), permissions) == 0;
}

// Returns the native path for a file URL
wxFileName wxFileName::URLToFileName(const wxString& url)
{
    wxString path;
    if ( !url.StartsWith(wxS("file://"), &path) &&
            !url.StartsWith(wxS("file:"), &path) )
    {
        // Consider it's just the path without any schema.
        path = url;
    }

    path = wxURI::Unescape(path);

#ifdef __WINDOWS__
    // file urls either start with a forward slash (local harddisk),
    // otherwise they have a servername/sharename notation,
    // which only exists on msw and corresponds to a unc
    if ( path.length() > 1 && (path[0u] == wxT('/') && path [1u] != wxT('/')) )
    {
        path = path.Mid(1);
    }
    else if ( url.StartsWith(wxS("file://")) &&
              (path.Find(wxT('/')) != wxNOT_FOUND) &&
              (path.length() > 1) && (path[1u] != wxT(':')) )
    {
        path = wxT("//") + path;
    }
#endif

    path.Replace(wxS('/'), wxFILE_SEP_PATH);

    return wxFileName(path, wxPATH_NATIVE);
}

// Escapes non-ASCII and others characters in file: URL to be valid URLs
static wxString EscapeFileNameCharsInURL(const char *in)
{
    wxString s;

    for ( const unsigned char *p = (const unsigned char*)in; *p; ++p )
    {
        const unsigned char c = *p;

        // https://tools.ietf.org/html/rfc1738#section-5
        if ( (c >= '0' && c <= '9') ||
             (c >= 'a' && c <= 'z') ||
             (c >= 'A' && c <= 'Z') ||
             strchr("/:$-_.+!*'(),", c) ) // Plus '/' and ':'
        {
            s << c;
        }
        else
        {
            s << wxString::Format("%%%02x", c);
        }
    }

    return s;
}

// Returns the file URL for a native path
wxString wxFileName::FileNameToURL(const wxFileName& filename)
{
    wxString url = filename.GetAbsolutePath(wxString(), wxPATH_NATIVE);

#ifndef __UNIX__
    // unc notation, wxMSW
    if ( url.Find(wxT("\\\\")) == 0 )
    {
        url = url.Mid(2);
    }
    else
    {
        url = wxT("/") + url;
    }
#endif

    url.Replace(wxFILE_SEP_PATH, wxS('/'));

    // Do wxURI- and common practice-compatible escaping: encode the string
    // into UTF-8, then escape anything non-ASCII:
    return wxT("file://") + EscapeFileNameCharsInURL(url.utf8_str());
}

// ----------------------------------------------------------------------------
// time functions
// ----------------------------------------------------------------------------

#if wxUSE_DATETIME

bool wxFileName::SetTimes(const wxDateTime *dtAccess,
                          const wxDateTime *dtMod,
                          const wxDateTime *dtCreate) const
{
#if defined(__WINDOWS__)
    FILETIME ftAccess, ftCreate, ftWrite;

    if ( dtCreate )
        ConvertWxToFileTime(&ftCreate, *dtCreate);
    if ( dtAccess )
        ConvertWxToFileTime(&ftAccess, *dtAccess);
    if ( dtMod )
        ConvertWxToFileTime(&ftWrite, *dtMod);

    wxString path;
    int flags;
    if ( IsDir() )
    {
        path = GetPath();
        flags = FILE_FLAG_BACKUP_SEMANTICS;
    }
    else // file
    {
        path = GetFullPath();
        flags = 0;
    }

    wxFileHandle fh(path, wxFileHandle::WriteAttr, flags);
    if ( fh.IsOk() )
    {
        if ( ::SetFileTime(fh,
                           dtCreate ? &ftCreate : nullptr,
                           dtAccess ? &ftAccess : nullptr,
                           dtMod ? &ftWrite : nullptr) )
        {
            return true;
        }
    }
#elif defined(__UNIX_LIKE__)
    wxUnusedVar(dtCreate);

    if ( !dtAccess && !dtMod )
    {
        // can't modify the creation time anyhow, don't try
        return true;
    }

    // if dtAccess or dtMod is not specified, use the other one (which must be
    // non null because of the test above) for both times
    utimbuf utm;
    utm.actime = dtAccess ? dtAccess->GetTicks() : dtMod->GetTicks();
    utm.modtime = dtMod ? dtMod->GetTicks() : dtAccess->GetTicks();
    if ( utime(GetFullPath().fn_str(), &utm) == 0 )
    {
        return true;
    }
#else // other platform
    wxUnusedVar(dtAccess);
    wxUnusedVar(dtMod);
    wxUnusedVar(dtCreate);
#endif // platforms

    wxLogSysError(_("Failed to modify file times for '%s'"),
                  GetFullPath());

    return false;
}

bool wxFileName::Touch() const
{
#if defined(__UNIX_LIKE__)
    // under Unix touching file is simple: just pass nullptr to utime()
    if ( utime(GetFullPath().fn_str(), nullptr) == 0 )
    {
        return true;
    }

    wxLogSysError(_("Failed to touch the file '%s'"), GetFullPath());

    return false;
#else // other platform
    wxDateTime dtNow = wxDateTime::Now();

    return SetTimes(&dtNow, &dtNow, nullptr /* don't change create time */);
#endif // platforms
}

bool wxFileName::GetTimes(wxDateTime *dtAccess,
                          wxDateTime *dtMod,
                          wxDateTime *dtCreate) const
{
#if defined(__WINDOWS__)
    // we must use different methods for the files and directories under
    // Windows as CreateFile(GENERIC_READ) doesn't work for the directories and
    // CreateFile(FILE_FLAG_BACKUP_SEMANTICS) works -- but only under NT and
    // not 9x
    bool ok;
    FILETIME ftAccess, ftCreate, ftWrite;
    if ( IsDir() )
    {
        // implemented in msw/dir.cpp
        extern bool wxGetDirectoryTimes(const wxString& dirname,
                                        FILETIME *, FILETIME *, FILETIME *);

        // we should pass the path without the trailing separator to
        // wxGetDirectoryTimes()
        ok = wxGetDirectoryTimes(GetPath(wxPATH_GET_VOLUME),
                                 &ftAccess, &ftCreate, &ftWrite);
    }
    else // file
    {
        wxFileHandle fh(GetFullPath(), wxFileHandle::ReadAttr);
        if ( fh.IsOk() )
        {
            ok = ::GetFileTime(fh,
                               dtCreate ? &ftCreate : nullptr,
                               dtAccess ? &ftAccess : nullptr,
                               dtMod ? &ftWrite : nullptr) != 0;
        }
        else
        {
            ok = false;
        }
    }

    if ( ok )
    {
        if ( dtCreate )
            ConvertFileTimeToWx(dtCreate, ftCreate);
        if ( dtAccess )
            ConvertFileTimeToWx(dtAccess, ftAccess);
        if ( dtMod )
            ConvertFileTimeToWx(dtMod, ftWrite);

        return true;
    }
#elif defined(wxHAVE_LSTAT)
    // no need to test for IsDir() here
    wxStructStat stBuf;
    if ( StatAny(stBuf, *this) )
    {
        // Android defines st_*time fields as unsigned long, but time_t as long,
        // hence the static_casts.
        if ( dtAccess )
            dtAccess->Set(static_cast<time_t>(stBuf.st_atime));
        if ( dtMod )
            dtMod->Set(static_cast<time_t>(stBuf.st_mtime));
        if ( dtCreate )
            dtCreate->Set(static_cast<time_t>(stBuf.st_ctime));

        return true;
    }
#else // other platform
    wxUnusedVar(dtAccess);
    wxUnusedVar(dtMod);
    wxUnusedVar(dtCreate);
#endif // platforms

    wxLogSysError(_("Failed to retrieve file times for '%s'"),
                  GetFullPath());

    return false;
}

#endif // wxUSE_DATETIME


// ----------------------------------------------------------------------------
// file size functions
// ----------------------------------------------------------------------------

/* static */
wxULongLong wxFileName::GetSize(const wxString &filename)
{
    if (!wxFileExists(filename))
        return wxInvalidSize;

#if defined(__WINDOWS__)
    wxFileHandle f(filename, wxFileHandle::ReadAttr);
    if (!f.IsOk())
        return wxInvalidSize;

    DWORD lpFileSizeHigh;
    DWORD ret = GetFileSize(f, &lpFileSizeHigh);
    if ( ret == INVALID_FILE_SIZE && ::GetLastError() != NO_ERROR )
        return wxInvalidSize;

    return wxULongLong(lpFileSizeHigh, ret);
#else // ! __WINDOWS__
    wxStructStat st;
    if (wxStat( filename, &st) != 0)
        return wxInvalidSize;
    return wxULongLong(st.st_size);
#endif
}

/* static */
wxString wxFileName::GetHumanReadableSize(const wxULongLong &bs,
                                          const wxString &nullsize,
                                          int precision,
                                          wxSizeConvention conv)
{
    // deal with trivial case first
    if ( bs == 0 || bs == wxInvalidSize )
        return nullsize;

    // depending on the convention used the multiplier may be either 1000 or
    // 1024 and the binary infix may be empty (for "KB") or "i" (for "KiB")
    double multiplier = 1024.;
    wxString biInfix;

    switch ( conv )
    {
        case wxSIZE_CONV_TRADITIONAL:
            // nothing to do, this corresponds to the default values of both
            // the multiplier and infix string
            break;

        case wxSIZE_CONV_IEC:
            biInfix = "i";
            break;

        case wxSIZE_CONV_SI:
            multiplier = 1000;
            break;
    }

    const double kiloByteSize = multiplier;
    const double megaByteSize = multiplier * kiloByteSize;
    const double gigaByteSize = multiplier * megaByteSize;
    const double teraByteSize = multiplier * gigaByteSize;

    const double bytesize = bs.ToDouble();

    wxString result;
    if ( bytesize < kiloByteSize )
        result.Printf("%s B", bs.ToString());
    else if ( bytesize < megaByteSize )
        result.Printf("%.*f K%sB", precision, bytesize/kiloByteSize, biInfix);
    else if (bytesize < gigaByteSize)
        result.Printf("%.*f M%sB", precision, bytesize/megaByteSize, biInfix);
    else if (bytesize < teraByteSize)
        result.Printf("%.*f G%sB", precision, bytesize/gigaByteSize, biInfix);
    else
        result.Printf("%.*f T%sB", precision, bytesize/teraByteSize, biInfix);

    return result;
}

wxULongLong wxFileName::GetSize() const
{
    return GetSize(GetFullPath());
}

wxString wxFileName::GetHumanReadableSize(const wxString& failmsg,
                                          int precision,
                                          wxSizeConvention conv) const
{
    return GetHumanReadableSize(GetSize(), failmsg, precision, conv);
}
