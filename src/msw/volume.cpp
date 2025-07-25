///////////////////////////////////////////////////////////////////////////////
// Name:        src/msw/volume.cpp
// Purpose:     wxFSVolume - encapsulates system volume information
// Author:      George Policello
// Created:     28 Jan 02
// Copyright:   (c) 2002 George Policello
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/wxprec.h"


#if wxUSE_FSVOLUME

#include "wx/volume.h"

#ifndef WX_PRECOMP
    #if wxUSE_GUI
        #include "wx/icon.h"
    #endif
    #include "wx/intl.h"
    #include "wx/log.h"
    #include "wx/filefn.h"
#endif // WX_PRECOMP

#include "wx/dir.h"
#include "wx/dynlib.h"

// some compilers require including <windows.h> before <shellapi.h> so do it
// even if this is not necessary with most of them
#include "wx/msw/wrapwin.h"
#include <shellapi.h>
#include "wx/msw/wrapshl.h"
#include "wx/msw/missing.h"

#if wxUSE_BASE

#include <unordered_map>

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Dynamic library function defs.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

static wxDynamicLibrary s_mprLib;

typedef DWORD (WINAPI* WNetOpenEnumPtr)(DWORD, DWORD, DWORD, LPNETRESOURCE, LPHANDLE);
typedef DWORD (WINAPI* WNetEnumResourcePtr)(HANDLE, LPDWORD, LPVOID, LPDWORD);
typedef DWORD (WINAPI* WNetCloseEnumPtr)(HANDLE);

static WNetOpenEnumPtr s_pWNetOpenEnum;
static WNetEnumResourcePtr s_pWNetEnumResource;
static WNetCloseEnumPtr s_pWNetCloseEnum;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Globals/Statics
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#if defined(__CYGWIN__) && defined(__LP64__)
    // We can't use long in 64 bit Cygwin build because Cygwin uses LP64 model
    // (unlike all the other MSW compilers) and long is 64 bits, while
    // InterlockedExchange(), with which this variable is used, requires a 32
    // bit-sized value, so use Cygwin-specific type with the right size.
    typedef __LONG32 wxInterlockedArg_t;
#else
    typedef long wxInterlockedArg_t;
#endif

static wxInterlockedArg_t s_cancelSearch = FALSE;

namespace
{

struct FileInfo
{
    FileInfo(unsigned flag=0, wxFSVolumeKind type=wxFS_VOL_OTHER) :
        m_flags(flag), m_type(type) {}

    FileInfo(const FileInfo& other) { *this = other; }
    FileInfo& operator=(const FileInfo& other)
    {
        m_flags = other.m_flags;
        m_type = other.m_type;
        return *this;
    }

    unsigned m_flags;
    wxFSVolumeKind m_type;
};

using FileInfoMap = std::unordered_map<wxString, FileInfo>;

static FileInfoMap& GetFileInfoMap()
{
    static FileInfoMap s_fileInfo(25);

    return s_fileInfo;
}

} // anonymous namespace

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Local helper functions.
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//=============================================================================
// Function: GetBasicFlags
// Purpose: Set basic flags, primarily wxFS_VOL_REMOTE and wxFS_VOL_REMOVABLE.
// Notes: - Local and mapped drives are mounted by definition.  We have no
//          way to determine mounted status of network drives, so assume that
//          all drives are mounted, and let the caller decide otherwise.
//        - Other flags are 'best guess' from type of drive.  The system will
//          not report the file attributes with any degree of accuracy.
//=============================================================================
static unsigned GetBasicFlags(const wxChar* filename)
{
    unsigned flags = wxFS_VOL_MOUNTED;

    //----------------------------------
    // 'Best Guess' based on drive type.
    //----------------------------------
    wxFSVolumeKind type;
    switch(GetDriveType(filename))
    {
    case DRIVE_FIXED:
        type = wxFS_VOL_DISK;
        break;

    case DRIVE_REMOVABLE:
        flags |= wxFS_VOL_REMOVABLE;
        type = wxFS_VOL_FLOPPY;
        break;

    case DRIVE_CDROM:
        flags |= wxFS_VOL_REMOVABLE | wxFS_VOL_READONLY;
        type = wxFS_VOL_CDROM;
        break;

    case DRIVE_REMOTE:
        flags |= wxFS_VOL_REMOTE;
        type = wxFS_VOL_NETWORK;
        break;

    case DRIVE_NO_ROOT_DIR:
        flags &= ~wxFS_VOL_MOUNTED;
        type = wxFS_VOL_OTHER;
        break;

    default:
        type = wxFS_VOL_OTHER;
        break;
    }

    //-----------------------------------------------------------------------
    // The following most likely will not modify anything not set above,
    // and will not work at all for network shares or empty CD ROM drives.
    // But it is a good check if the Win API ever gets better about reporting
    // this information.
    //-----------------------------------------------------------------------
    SHFILEINFO fi;
    long rc = SHGetFileInfo(filename, 0, &fi, sizeof(fi), SHGFI_ATTRIBUTES);
    if (!rc)
    {
        // this error is not fatal, so don't show a message to the user about
        // it, otherwise it would appear every time a generic directory picker
        // dialog is used and there is a connected network drive
        wxLogLastError(wxT("SHGetFileInfo"));
    }
    else
    {
        if (fi.dwAttributes & SFGAO_READONLY)
            flags |= wxFS_VOL_READONLY;
        if (fi.dwAttributes & SFGAO_REMOVABLE)
            flags |= wxFS_VOL_REMOVABLE;
    }

    //------------------
    // Flags are cached.
    //------------------
    GetFileInfoMap()[filename] = FileInfo(flags, type);

    return flags;
} // GetBasicFlags

//=============================================================================
// Function: FilteredAdd
// Purpose: Add a file to the list if it meets the filter requirement.
// Notes: - See GetBasicFlags for remarks about the Mounted flag.
//=============================================================================
static bool FilteredAdd(wxArrayString& list, const wxChar* filename,
                        unsigned flagsSet, unsigned flagsUnset)
{
    bool accept = true;
    unsigned flags = GetBasicFlags(filename);

    if (flagsSet & wxFS_VOL_MOUNTED && !(flags & wxFS_VOL_MOUNTED))
        accept = false;
    else if (flagsUnset & wxFS_VOL_MOUNTED && (flags & wxFS_VOL_MOUNTED))
        accept = false;
    else if (flagsSet & wxFS_VOL_REMOVABLE && !(flags & wxFS_VOL_REMOVABLE))
        accept = false;
    else if (flagsUnset & wxFS_VOL_REMOVABLE && (flags & wxFS_VOL_REMOVABLE))
        accept = false;
    else if (flagsSet & wxFS_VOL_READONLY && !(flags & wxFS_VOL_READONLY))
        accept = false;
    else if (flagsUnset & wxFS_VOL_READONLY && (flags & wxFS_VOL_READONLY))
        accept = false;
    else if (flagsSet & wxFS_VOL_REMOTE && !(flags & wxFS_VOL_REMOTE))
        accept = false;
    else if (flagsUnset & wxFS_VOL_REMOTE && (flags & wxFS_VOL_REMOTE))
        accept = false;

    // Add to the list if passed the filter.
    if (accept)
        list.Add(filename);

    return accept;
} // FilteredAdd

//=============================================================================
// Function: BuildListFromNN
// Purpose: Append or remove items from the list
// Notes: - There is no way to find all disconnected NN items, or even to find
//          all items while determining which are connected and not.  So this
//          function will find either all items or connected items.
//=============================================================================
static void BuildListFromNN(wxArrayString& list, NETRESOURCE* pResSrc,
                            unsigned flagsSet, unsigned flagsUnset)
{
    HANDLE hEnum;

    //-----------------------------------------------
    // Scope may be all drives or all mounted drives.
    //-----------------------------------------------
    unsigned scope = RESOURCE_GLOBALNET;
    if (flagsSet & wxFS_VOL_MOUNTED)
        scope = RESOURCE_CONNECTED;

    //----------------------------------------------------------------------
    // Enumerate all items, adding only non-containers (ie. network shares).
    // Containers cause a recursive call to this function for their own
    // enumeration.
    //----------------------------------------------------------------------
    if (s_pWNetOpenEnum(scope, RESOURCETYPE_DISK, 0, pResSrc, &hEnum) == NO_ERROR)
    {
        DWORD count = 1;
        DWORD size = 256;
        NETRESOURCE* pRes = (NETRESOURCE*)malloc(size);
        memset(pRes, 0, sizeof(NETRESOURCE));
        int rc;
        while ((rc = s_pWNetEnumResource(hEnum, &count, pRes, &size)) == NO_ERROR || rc == ERROR_MORE_DATA)
        {
            if (s_cancelSearch)
                break;

            if (rc == ERROR_MORE_DATA)
            {
                pRes = (NETRESOURCE*)realloc(pRes, size);
                count = 1;
            }
            else if (count == 1)
            {
                // Enumerate the container.
                if (pRes->dwUsage & RESOURCEUSAGE_CONTAINER)
                {
                    BuildListFromNN(list, pRes, flagsSet, flagsUnset);
                }

                // Add the network share.
                else
                {
                    wxString filename(pRes->lpRemoteName);

                    // if the drive is unavailable, FilteredAdd() can hang for
                    // a long time and, moreover, its failure appears to be not
                    // cached so this will happen every time we use it, so try
                    // a much quicker wxDirExists() test (which still hangs but
                    // for much shorter time) for locally mapped drives first
                    // to try to avoid this
                    if ( pRes->lpLocalName &&
                            *pRes->lpLocalName &&
                                !wxDirExists(pRes->lpLocalName) )
                        continue;

                    if (!filename.empty())
                    {
                        if (filename.Last() != '\\')
                            filename.Append('\\');

                        // The filter function will not know mounted from unmounted, and neither do we unless
                        // we are iterating using RESOURCE_CONNECTED, in which case they all are mounted.
                        // Volumes on disconnected servers, however, will correctly show as unmounted.
                        FilteredAdd(list, filename.t_str(), flagsSet, flagsUnset&~wxFS_VOL_MOUNTED);
                        if (scope == RESOURCE_GLOBALNET)
                            GetFileInfoMap()[filename].m_flags &= ~wxFS_VOL_MOUNTED;
                    }
                }
            }
            else if (count == 0)
                break;
        }
        free(pRes);
        s_pWNetCloseEnum(hEnum);
    }
} // BuildListFromNN

//=============================================================================
// Function: CompareFcn
// Purpose: Used to sort the NN list alphabetically, case insensitive.
//=============================================================================
static int CompareFcn(const wxString& first, const wxString& second)
{
    return wxStricmp(first.c_str(), second.c_str());
} // CompareFcn

//=============================================================================
// Function: BuildRemoteList
// Purpose: Append Network Neighborhood items to the list.
// Notes: - Mounted gets translated into Connected.  FilteredAdd is told
//          to ignore the Mounted flag since we need to handle it in a weird
//          way manually.
//        - The resulting list is sorted alphabetically.
//=============================================================================
static bool BuildRemoteList(wxArrayString& list, NETRESOURCE* pResSrc,
                            unsigned flagsSet, unsigned flagsUnset)
{
    // NN query depends on dynamically loaded library.
    if (!s_pWNetOpenEnum || !s_pWNetEnumResource || !s_pWNetCloseEnum)
    {
        wxLogError(_("Failed to load mpr.dll."));
        return false;
    }

    // Don't waste time doing the work if the flags conflict.
    if (flagsSet & wxFS_VOL_MOUNTED && flagsUnset & wxFS_VOL_MOUNTED)
        return false;

    //----------------------------------------------
    // Generate the list according to the flags set.
    //----------------------------------------------
    BuildListFromNN(list, pResSrc, flagsSet, flagsUnset);
    list.Sort(CompareFcn);

    //-------------------------------------------------------------------------
    // If mounted only is requested, then we only need one simple pass.
    // Otherwise, we need to build a list of all NN volumes and then apply the
    // list of mounted drives to it.
    //-------------------------------------------------------------------------
    if (!(flagsSet & wxFS_VOL_MOUNTED))
    {
        // generate.
        wxArrayString mounted;
        BuildListFromNN(mounted, pResSrc, flagsSet | wxFS_VOL_MOUNTED, flagsUnset & ~wxFS_VOL_MOUNTED);
        mounted.Sort(CompareFcn);

        // apply list from bottom to top to preserve indexes if removing items.
        ssize_t iList = list.GetCount()-1;
        for (ssize_t iMounted = mounted.GetCount()-1; iMounted >= 0 && iList >= 0; iMounted--)
        {
            int compare;

            while ((compare = wxStricmp(list[iList].c_str(), mounted[iMounted].c_str())) > 0
                   && iList >= 0)
            {
                iList--;
            }


            if (compare == 0)
            {
                // Found the element.  Remove it or mark it mounted.
                if (flagsUnset & wxFS_VOL_MOUNTED)
                    list.RemoveAt(iList);
                else
                    GetFileInfoMap()[list[iList]].m_flags |= wxFS_VOL_MOUNTED;

            }

            iList--;
        }
    }

    return true;
} // BuildRemoteList

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// wxFSVolume
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//=============================================================================
// Function: GetVolumes
// Purpose: Generate and return a list of all volumes (drives) available.
// Notes:
//=============================================================================
wxArrayString wxFSVolumeBase::GetVolumes(int flagsSet, int flagsUnset)
{
    ::InterlockedExchange(&s_cancelSearch, FALSE);     // reset

    if (!s_mprLib.IsLoaded() && s_mprLib.Load(wxT("mpr.dll")))
    {
        s_pWNetOpenEnum = (WNetOpenEnumPtr)s_mprLib.GetSymbol(wxT("WNetOpenEnumW"));
        s_pWNetEnumResource = (WNetEnumResourcePtr)s_mprLib.GetSymbol(wxT("WNetEnumResourceW"));
        s_pWNetCloseEnum = (WNetCloseEnumPtr)s_mprLib.GetSymbol(wxT("WNetCloseEnum"));
    }

    wxArrayString list;

    //-------------------------------
    // Local and mapped drives first.
    //-------------------------------
    // Allocate the required space for the API call.
    const DWORD chars = GetLogicalDriveStrings(0, nullptr);
    TCHAR* buf = new TCHAR[chars+1];

    // Get the list of drives.
    GetLogicalDriveStrings(chars, buf);

    // Parse the list into an array, applying appropriate filters.
    TCHAR *pVol;
    pVol = buf;
    while (*pVol)
    {
        FilteredAdd(list, pVol, flagsSet, flagsUnset);
        pVol = pVol + wxStrlen(pVol) + 1;
    }

    // Cleanup.
    delete[] buf;

    //---------------------------
    // Network Neighborhood next.
    //---------------------------

    // not exclude remote and not removable
    if (!(flagsUnset & wxFS_VOL_REMOTE) &&
        !(flagsSet & wxFS_VOL_REMOVABLE)
       )
    {
        // The returned list will be sorted alphabetically.  We don't pass
        // our in since we don't want to change to order of the local drives.
        wxArrayString nn;
        if (BuildRemoteList(nn, nullptr, flagsSet, flagsUnset))
        {
            for (size_t idx = 0; idx < nn.GetCount(); idx++)
                list.Add(nn[idx]);
        }
    }

    return list;
} // GetVolumes

//=============================================================================
// Function: CancelSearch
// Purpose: Instruct an active search to stop.
// Notes: - This will only sensibly be called by a thread other than the one
//          performing the search.  This is the only thread-safe function
//          provided by the class.
//=============================================================================
void wxFSVolumeBase::CancelSearch()
{
    ::InterlockedExchange(&s_cancelSearch, TRUE);
} // CancelSearch

//=============================================================================
// Function: constructor
// Purpose: default constructor
//=============================================================================
wxFSVolumeBase::wxFSVolumeBase()
{
    m_isOk = false;
} // wxVolume

//=============================================================================
// Function: constructor
// Purpose: constructor that calls Create
//=============================================================================
wxFSVolumeBase::wxFSVolumeBase(const wxString& name)
{
    Create(name);
} // wxVolume

//=============================================================================
// Function: Create
// Purpose: Finds, logs in, etc. to the request volume.
//=============================================================================
bool wxFSVolumeBase::Create(const wxString& name)
{
    // assume fail.
    m_isOk = false;

    // supplied.
    m_volName = name;

    // Display name.
    SHFILEINFO fi;
    long rc = SHGetFileInfo(m_volName.t_str(), 0, &fi, sizeof(fi), SHGFI_DISPLAYNAME);
    if (!rc)
    {
        wxLogError(_("Cannot read typename from '%s'!"), m_volName);
        return false;
    }
    m_dispName = fi.szDisplayName;

    // all tests passed.
    m_isOk = true;
    return true;
} // Create

//=============================================================================
// Function: IsOk
// Purpose: returns true if the volume is legal.
// Notes: For fixed disks, it must exist.  For removable disks, it must also
//        be present.  For Network Shares, it must also be logged in, etc.
//=============================================================================
bool wxFSVolumeBase::IsOk() const
{
    return m_isOk;
} // IsOk

//=============================================================================
// Function: GetKind
// Purpose: Return the type of the volume.
//=============================================================================
wxFSVolumeKind wxFSVolumeBase::GetKind() const
{
    if (!m_isOk)
        return wxFS_VOL_OTHER;

    FileInfoMap::iterator itr = GetFileInfoMap().find(m_volName);
    if (itr == GetFileInfoMap().end())
        return wxFS_VOL_OTHER;

    return itr->second.m_type;
}

//=============================================================================
// Function: GetFlags
// Purpose: Return the caches flags for this volume.
// Notes: - Returns -1 if no flags were cached.
//=============================================================================
int wxFSVolumeBase::GetFlags() const
{
    if (!m_isOk)
        return -1;

    FileInfoMap::iterator itr = GetFileInfoMap().find(m_volName);
    if (itr == GetFileInfoMap().end())
        return -1;

    return itr->second.m_flags;
} // GetFlags

#endif // wxUSE_BASE

// ============================================================================
// wxFSVolume
// ============================================================================

#if wxUSE_GUI

//=============================================================================
// Function: GetIcon
// Purpose: return the requested icon.
//=============================================================================

wxIcon wxFSVolume::GetIcon(wxFSIconType type) const
{
    auto* const self = const_cast<wxFSVolume*>(this);

    if ( m_icons.empty() )
    {
        // Allocate on first access.
        self->m_icons.resize(wxFS_VOL_ICO_MAX);
    }

    wxCHECK_MSG( type >= 0 && (size_t)type < m_icons.size(), wxNullIcon,
                 wxT("wxFSIconType::GetIcon(): invalid icon index") );

#ifdef __WXMSW__
    // Load on demand.
    if (m_icons[type].IsNull())
    {
        UINT flags = SHGFI_ICON;
        switch (type)
        {
        case wxFS_VOL_ICO_SMALL:
            flags |= SHGFI_SMALLICON;
            break;

        case wxFS_VOL_ICO_LARGE:
            flags |= SHGFI_SHELLICONSIZE;
            break;

        case wxFS_VOL_ICO_SEL_SMALL:
            flags |= SHGFI_SMALLICON | SHGFI_OPENICON;
            break;

        case wxFS_VOL_ICO_SEL_LARGE:
            flags |= SHGFI_SHELLICONSIZE | SHGFI_OPENICON;
            break;

        case wxFS_VOL_ICO_MAX:
            wxFAIL_MSG(wxT("wxFS_VOL_ICO_MAX is not valid icon type"));
            break;
        }

        SHFILEINFO fi;
        long rc = SHGetFileInfo(m_volName.t_str(), 0, &fi, sizeof(fi), flags);
        if (!rc || !fi.hIcon)
        {
            wxLogError(_("Cannot load icon from '%s'."), m_volName);
        }
        else
        {
            self->m_icons[type].CreateFromHICON((WXHICON)fi.hIcon);
        }
    }

    return m_icons[type];
#else
    wxFAIL_MSG(wxS("Can't convert HICON to wxIcon in this port."));
    return wxNullIcon;
#endif
} // GetIcon

#endif // wxUSE_GUI

#endif // wxUSE_FSVOLUME
