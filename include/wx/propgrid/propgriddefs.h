/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/propgriddefs.h
// Purpose:     wxPropertyGrid miscellaneous definitions
// Author:      Jaakko Salli
// Created:     2008-08-31
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_PROPGRID_PROPGRIDDEFS_H_
#define _WX_PROPGRID_PROPGRIDDEFS_H_

#include "wx/defs.h"

#if wxUSE_PROPGRID

#include "wx/colour.h"

class WXDLLIMPEXP_FWD_CORE wxPoint;
class WXDLLIMPEXP_FWD_CORE wxSize;
class WXDLLIMPEXP_FWD_CORE wxFont;

#include <limits>
#include <unordered_map>

// -----------------------------------------------------------------------

// Set 1 to include advanced properties (wxFontProperty, wxColourProperty, etc.)
#ifndef wxPG_INCLUDE_ADVPROPS
    #define wxPG_INCLUDE_ADVPROPS           1
#endif

// Set 1 to include checkbox editor class
#define wxPG_INCLUDE_CHECKBOX           1

// -----------------------------------------------------------------------


class WXDLLIMPEXP_FWD_PROPGRID wxPGEditor;
class WXDLLIMPEXP_FWD_PROPGRID wxPGProperty;
class WXDLLIMPEXP_FWD_PROPGRID wxPropertyCategory;
class WXDLLIMPEXP_FWD_PROPGRID wxPGChoices;
class WXDLLIMPEXP_FWD_PROPGRID wxPropertyGridPageState;
class WXDLLIMPEXP_FWD_PROPGRID wxPGCell;
class WXDLLIMPEXP_FWD_PROPGRID wxPGCellRenderer;
class WXDLLIMPEXP_FWD_PROPGRID wxPGChoiceEntry;
class WXDLLIMPEXP_FWD_PROPGRID wxPGPropArgCls;
class WXDLLIMPEXP_FWD_PROPGRID wxPropertyGridInterface;
class WXDLLIMPEXP_FWD_PROPGRID wxPropertyGrid;
class WXDLLIMPEXP_FWD_PROPGRID wxPropertyGridEvent;
class wxPropertyGridManager;
class WXDLLIMPEXP_FWD_PROPGRID wxPGEditorDialogAdapter;
class WXDLLIMPEXP_FWD_PROPGRID wxPGValidationInfo;


// -----------------------------------------------------------------------

// Some miscellaneous values, types and macros.

// Used to tell wxPGProperty to use label as name as well
#define wxPG_LABEL              (wxPGProperty::sm_labelItem)

#if WXWIN_COMPATIBILITY_3_2
// This is the value placed in wxPGProperty::sm_LabelItem
#ifdef wxPG_MUST_DEPRECATE_MACRO_NAME
#pragma deprecated(wxPG_LABEL_STRING)
#endif
#define wxPG_LABEL_STRING wxPG_DEPRECATED_MACRO_VALUE("@!",\
    "wxPG_LABEL_STRING is deprecated. Use \"@!\" instead.")
#endif // WXWIN_COMPATIBILITY_3_2
#if WXWIN_COMPATIBILITY_3_0
#ifdef wxPG_MUST_DEPRECATE_MACRO_NAME
#pragma deprecated(wxPG_NULL_BITMAP)
#endif
#define wxPG_NULL_BITMAP wxPG_DEPRECATED_MACRO_VALUE(wxNullBitmap,\
    "wxPG_NULL_BITMAP is deprecated. Use wxNullBitmap instead.")
#endif // WXWIN_COMPATIBILITY_3_0
#if WXWIN_COMPATIBILITY_3_2
#ifdef wxPG_MUST_DEPRECATE_MACRO_NAME
#pragma deprecated(wxPG_COLOUR_BLACK)
#endif
#define wxPG_COLOUR_BLACK wxPG_DEPRECATED_MACRO_VALUE((*wxBLACK),\
    "wxPG_COLOUR_BLACK is deprecated. Use *wxBLACK instead.")
#endif // WXWIN_COMPATIBILITY_3_2

// Convert Red, Green and Blue to a single 32-bit value.
#define wxPG_COLOUR(R,G,B) ((wxUint32)((R)+((G)<<8)+((B)<<16)))


// If property is supposed to have custom-painted image, then returning
// this in OnMeasureImage() will usually be enough.
#define wxPG_DEFAULT_IMAGE_SIZE  wxDefaultSize


// This callback function is used for sorting properties.
// Call wxPropertyGrid::SetSortFunction() to set it.
// Sort function should return a value greater than 0 if position of p1 is
// after p2. So, for instance, when comparing property names, you can use
// following implementation:
//   int MyPropertySortFunction(wxPropertyGrid* propGrid,
//                              wxPGProperty* p1,
//                              wxPGProperty* p2)
//   {
//      return p1->GetBaseName().compare( p2->GetBaseName() );
//   }
typedef int (*wxPGSortCallback)(wxPropertyGrid* propGrid,
                                wxPGProperty* p1,
                                wxPGProperty* p2);


#if WXWIN_COMPATIBILITY_3_0
typedef wxString wxPGCachedString;
#endif

// -----------------------------------------------------------------------

// Used to indicate wxPGChoices::Add etc. that the value is actually not given
// by the caller.
constexpr int wxPG_INVALID_VALUE = std::numeric_limits<int>::max();

// -----------------------------------------------------------------------

WX_DEFINE_TYPEARRAY_WITH_DECL_PTR(wxPGProperty*, wxArrayPGProperty,
                                  wxBaseArrayPtrVoid,
                                  class WXDLLIMPEXP_PROPGRID);

using wxPGHashMapS2S = std::unordered_map<wxString, wxString>;

#if WXWIN_COMPATIBILITY_3_0
WX_DEFINE_TYPEARRAY_WITH_DECL_PTR(wxObject*, wxArrayPGObject,
                                  wxBaseArrayPtrVoid,
                                  class WXDLLIMPEXP_PROPGRID);
#endif // WXWIN_COMPATIBILITY_3_0

// -----------------------------------------------------------------------

enum class wxPGPropertyValuesFlags : int
{
    // Flag for wxPropertyGridInterface::SetProperty* functions,
    // wxPropertyGridInterface::HideProperty(), etc.
    // Apply changes only for the property in question.
    DontRecurse      = 0x00000000,

    // Flag for wxPropertyGridInterface::GetPropertyValues().
    // Use this flag to retain category structure; each sub-category
    // will be its own wxVariantList of wxVariant.
    KeepStructure    = 0x00000010,

    // Flag for wxPropertyGridInterface::SetProperty* functions,
    // wxPropertyGridInterface::HideProperty(), etc.
    // Apply changes recursively for the property and all its children.
    Recurse          = 0x00000020,

    // Flag for wxPropertyGridInterface::GetPropertyValues().
    // Use this flag to include property attributes as well.
    IncAttributes    = 0x00000040,

    // Used when first starting recursion.
    RecurseStarts    = 0x00000080,

    // Force value change.
    Force            = 0x00000100,

    // Only sort categories and their immediate children.
    // Sorting done by wxPG_AUTO_SORT option uses this.
    SortTopLevelOnly = 0x00000200
};

constexpr wxPGPropertyValuesFlags operator|(wxPGPropertyValuesFlags a, wxPGPropertyValuesFlags b)
{
    return static_cast<wxPGPropertyValuesFlags>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr wxPGPropertyValuesFlags operator&(wxPGPropertyValuesFlags a, wxPGPropertyValuesFlags b)
{
    return static_cast<wxPGPropertyValuesFlags>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr bool operator!(wxPGPropertyValuesFlags a)
{
    return static_cast<int>(a) == 0;
}

#if WXWIN_COMPATIBILITY_3_2
// These constants are deprecated but intentionally don't use wxDEPRECATED_MSG()
// because one will be given whenever they are used with any function now
// taking wxPGPropertyValuesFlags anyhow and giving multiple deprecation
// warnings for the same line of code is more annoying than helpful.
enum wxPG_PROPERTYVALUES_FLAGS
{
    wxPG_DONT_RECURSE = static_cast<int>(wxPGPropertyValuesFlags::DontRecurse),
    wxPG_KEEP_STRUCTURE = static_cast<int>(wxPGPropertyValuesFlags::KeepStructure),
    wxPG_RECURSE = static_cast<int>(wxPGPropertyValuesFlags::Recurse),
    wxPG_INC_ATTRIBUTES = static_cast<int>(wxPGPropertyValuesFlags::IncAttributes),
    wxPG_RECURSE_STARTS = static_cast<int>(wxPGPropertyValuesFlags::RecurseStarts),
    wxPG_FORCE = static_cast<int>(wxPGPropertyValuesFlags::Force),
    wxPG_SORT_TOP_LEVEL_ONLY = static_cast<int>(wxPGPropertyValuesFlags::SortTopLevelOnly),
};

wxDEPRECATED_MSG("use wxPGPropertyValuesFlags instead")
constexpr bool operator==(wxPGPropertyValuesFlags a, int b)
{
    return static_cast<int>(a) == b;
}

wxDEPRECATED_MSG("use wxPGPropertyValuesFlags instead")
constexpr bool operator!=(wxPGPropertyValuesFlags a, int b)
{
    return static_cast<int>(a) != b;
}
#endif // WXWIN_COMPATIBILITY_3_2

// -----------------------------------------------------------------------

// Miscellaneous property value format flags
enum class wxPGPropValFormatFlags : int
{
    // No flags.
    Null = 0,

    // Get/Store full value instead of displayed value.
    FullValue                   = 0x00000001,

    // Perform special action in case of unsuccessful conversion.
    ReportError                 = 0x00000002,

    PropertySpecific            = 0x00000004,

    // Get/Store editable value instead of displayed one (should only be
    // different in the case of common values)
    EditableValue               = 0x00000008,

    // Used when dealing with fragments of composite string value
    CompositeFragment           = 0x00000010,

    // Means property for which final string value is for cannot really be
    // edited.
    UneditableCompositeFragment = 0x00000020,

    // ValueToString() called from GetValueAsString()
    // (guarantees that input wxVariant value is current own value)
    ValueIsCurrent              = 0x00000040,

    // Value is being set programmatically (i.e. not by user)
    ProgrammaticValue           = 0x00000080
};

constexpr wxPGPropValFormatFlags operator&(wxPGPropValFormatFlags a, wxPGPropValFormatFlags b)
{
    return static_cast<wxPGPropValFormatFlags>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr wxPGPropValFormatFlags operator|(wxPGPropValFormatFlags a, wxPGPropValFormatFlags b)
{
    return static_cast<wxPGPropValFormatFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline wxPGPropValFormatFlags operator|=(wxPGPropValFormatFlags& a, wxPGPropValFormatFlags b)
{
    return a = a | b;
}

constexpr bool operator!(wxPGPropValFormatFlags a)
{
    return static_cast<int>(a) == 0;
}

#if WXWIN_COMPATIBILITY_3_2
constexpr int operator&(int a, wxPGPropValFormatFlags b)
{
    return a & static_cast<int>(b);
}

constexpr int operator|(int a, wxPGPropValFormatFlags b)
{
    return a | static_cast<int>(b);
}

inline int operator|=(int& a, wxPGPropValFormatFlags b)
{
    return a = a | static_cast<int>(b);
}

// See comment before wxPG_RECURSE above.
enum wxPG_MISC_ARG_FLAGS
{
    wxPG_FULL_VALUE = static_cast<int>(wxPGPropValFormatFlags::FullValue),
    wxPG_REPORT_ERROR = static_cast<int>(wxPGPropValFormatFlags::ReportError),
    wxPG_PROPERTY_SPECIFIC = static_cast<int>(wxPGPropValFormatFlags::PropertySpecific),
    wxPG_EDITABLE_VALUE = static_cast<int>(wxPGPropValFormatFlags::EditableValue),
    wxPG_COMPOSITE_FRAGMENT = static_cast<int>(wxPGPropValFormatFlags::CompositeFragment),
    wxPG_UNEDITABLE_COMPOSITE_FRAGMENT = static_cast<int>(wxPGPropValFormatFlags::UneditableCompositeFragment),
    wxPG_VALUE_IS_CURRENT = static_cast<int>(wxPGPropValFormatFlags::ValueIsCurrent),
    wxPG_PROGRAMMATIC_VALUE = static_cast<int>(wxPGPropValFormatFlags::ProgrammaticValue),
};
#endif // WXWIN_COMPATIBILITY_3_2

// -----------------------------------------------------------------------

// wxPGProperty::SetValue() flags
enum class wxPGSetValueFlags : int
{
    RefreshEditor = 0x0001,
    Aggregated    = 0x0002,
    FromParent    = 0x0004,
    ByUser        = 0x0008  // Set if value changed by user
};

constexpr wxPGSetValueFlags operator|(wxPGSetValueFlags a, wxPGSetValueFlags b)
{
    return static_cast<wxPGSetValueFlags>(static_cast<int>(a) | static_cast<int>(b));
}

inline wxPGSetValueFlags operator|=(wxPGSetValueFlags& a, wxPGSetValueFlags b)
{
    return a = a | b;
}

constexpr wxPGSetValueFlags operator&(wxPGSetValueFlags a, wxPGSetValueFlags b)
{
    return static_cast<wxPGSetValueFlags>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr bool operator!(wxPGSetValueFlags a)
{
    return static_cast<int>(a) == 0;
}

#if WXWIN_COMPATIBILITY_3_2
enum wxPG_SETVALUE_FLAGS
{
    wxPG_SETVAL_REFRESH_EDITOR = static_cast<int>(wxPGSetValueFlags::RefreshEditor),
    wxPG_SETVAL_AGGREGATED = static_cast<int>(wxPGSetValueFlags::Aggregated),
    wxPG_SETVAL_FROM_PARENT = static_cast<int>(wxPGSetValueFlags::FromParent),
    wxPG_SETVAL_BY_USER = static_cast<int>(wxPGSetValueFlags::ByUser),
};

wxDEPRECATED_MSG("use wxPGSetValueFlags instead")
constexpr bool operator==(wxPGSetValueFlags a, int b)
{
    return static_cast<int>(a) == b;
}

wxDEPRECATED_MSG("use wxPGSetValueFlags instead")
constexpr bool operator!=(wxPGSetValueFlags a, int b)
{
    return static_cast<int>(a) != b;
}
#endif // WXWIN_COMPATIBILITY_3_2

// -----------------------------------------------------------------------

//
// Valid constants for wxPG_UINT_BASE attribute
// (long because of wxVariant constructor)
constexpr long wxPG_BASE_OCT  =  8L;
constexpr long wxPG_BASE_DEC  = 10L;
constexpr long wxPG_BASE_HEX  = 16L;
constexpr long wxPG_BASE_HEXL = 32L;

//
// Valid constants for wxPG_UINT_PREFIX attribute
constexpr long wxPG_PREFIX_NONE        = 0L;
constexpr long wxPG_PREFIX_0x          = 1L;
constexpr long wxPG_PREFIX_DOLLAR_SIGN = 2L;

// -----------------------------------------------------------------------
// Editor class.

// Editor accessor (for backwards compatibility use only).
#define wxPG_EDITOR(T)          wxPGEditor_##T

// Macro for declaring editor class, with optional impexpdecl part.
#ifndef WX_PG_DECLARE_EDITOR_WITH_DECL

    #define WX_PG_DECLARE_EDITOR_WITH_DECL(EDITOR,DECL) \
    extern DECL wxPGEditor* wxPGEditor_##EDITOR; \
    extern DECL wxPGEditor* wxPGConstruct##EDITOR##EditorClass();

#endif

// Declare editor class.
#define WX_PG_DECLARE_EDITOR(EDITOR) \
extern wxPGEditor* wxPGEditor_##EDITOR; \
extern wxPGEditor* wxPGConstruct##EDITOR##EditorClass();

// Declare built-in editor classes.
WX_PG_DECLARE_EDITOR_WITH_DECL(TextCtrl,WXDLLIMPEXP_PROPGRID)
WX_PG_DECLARE_EDITOR_WITH_DECL(Choice,WXDLLIMPEXP_PROPGRID)
WX_PG_DECLARE_EDITOR_WITH_DECL(ComboBox,WXDLLIMPEXP_PROPGRID)
WX_PG_DECLARE_EDITOR_WITH_DECL(TextCtrlAndButton,WXDLLIMPEXP_PROPGRID)
#if wxPG_INCLUDE_CHECKBOX
WX_PG_DECLARE_EDITOR_WITH_DECL(CheckBox,WXDLLIMPEXP_PROPGRID)
#endif
WX_PG_DECLARE_EDITOR_WITH_DECL(ChoiceAndButton,WXDLLIMPEXP_PROPGRID)

// -----------------------------------------------------------------------

#ifndef SWIG

//
// Macro WXVARIANT allows creation of wxVariant from any type supported by
// wxWidgets internally, and of all types created using
// WX_PG_DECLARE_VARIANT_DATA.
template<class T>
wxVariant WXVARIANT( const T& WXUNUSED(value) )
{
    wxFAIL_MSG(wxS("Code should always call specializations of this template"));
    return wxVariant();
}

template<> inline wxVariant WXVARIANT( const int& value )
    { return wxVariant((long)value); }
template<> inline wxVariant WXVARIANT( const long& value )
    { return wxVariant(value); }
template<> inline wxVariant WXVARIANT( const bool& value )
    { return wxVariant(value); }
template<> inline wxVariant WXVARIANT( const double& value )
    { return wxVariant(value); }
template<> inline wxVariant WXVARIANT( const wxArrayString& value )
    { return wxVariant(value); }
template<> inline wxVariant WXVARIANT( const wxString& value )
    { return wxVariant(value); }
template<> inline wxVariant WXVARIANT( const wxLongLong& value )
    { return wxVariant(value); }
template<> inline wxVariant WXVARIANT( const wxULongLong& value )
    { return wxVariant(value); }
#if wxUSE_DATETIME
template<> inline wxVariant WXVARIANT( const wxDateTime& value )
    { return wxVariant(value); }
#endif


//
// These are modified versions of DECLARE/WX_PG_IMPLEMENT_VARIANT_DATA
// macros found in variant.h. Differences are as follows:
//   * These support non-wxObject data
//   * These implement classname##RefFromVariant function which returns
//     reference to data within.
//   * const char* classname##_VariantType which equals classname.
//   * WXVARIANT
//
#define WX_PG_DECLARE_VARIANT_DATA(classname) \
    WX_PG_DECLARE_VARIANT_DATA_EXPORTED(classname, wxEMPTY_PARAMETER_VALUE)

#define WX_PG_DECLARE_VARIANT_DATA_EXPORTED(classname,expdecl) \
expdecl classname& operator << ( classname &object, const wxVariant &variant ); \
expdecl wxVariant& operator << ( wxVariant &variant, const classname &object ); \
expdecl const classname& classname##RefFromVariant( const wxVariant& variant ); \
expdecl classname& classname##RefFromVariant( wxVariant& variant ); \
template<> inline wxVariant WXVARIANT( const classname& value ) \
{ \
    wxVariant variant; \
    variant << value; \
    return variant; \
} \
extern expdecl const char* classname##_VariantType;


#define WX_PG_IMPLEMENT_VARIANT_DATA(classname) \
    WX_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(classname, wxEMPTY_PARAMETER_VALUE)

// Add getter (i.e. classname << variant) separately to allow
// custom implementations.
#define WX_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_NO_EQ_NO_GETTER(classname,expdecl) \
const char* classname##_VariantType = #classname; \
class classname##VariantData: public wxVariantData \
{ \
public:\
    classname##VariantData() = default; \
    classname##VariantData( const classname &value ) : m_value(value) { } \
\
    classname &GetValue() { return m_value; } \
\
    const classname &GetValue() const { return m_value; } \
\
    virtual bool Eq(wxVariantData& data) const override; \
\
    virtual wxString GetType() const override; \
\
    wxNODISCARD virtual wxVariantData* Clone() const override { return new classname##VariantData(m_value); } \
\
    DECLARE_WXANY_CONVERSION() \
protected:\
    classname m_value; \
};\
\
IMPLEMENT_TRIVIAL_WXANY_CONVERSION(classname, classname##VariantData) \
\
wxString classname##VariantData::GetType() const\
{\
    return wxS(#classname);\
}\
\
expdecl wxVariant& operator << ( wxVariant &variant, const classname &value )\
{\
    classname##VariantData *data = new classname##VariantData( value );\
    variant.SetData( data );\
    return variant;\
} \
expdecl classname& classname##RefFromVariant( wxVariant& variant ) \
{ \
    wxASSERT_MSG( variant.GetType() == wxS(#classname), \
                  wxString::Format(wxS("Variant type should have been '%s'") \
                                   wxS("instead of '%s'"), \
                                   wxS(#classname), \
                                   variant.GetType())); \
    classname##VariantData *data = \
        (classname##VariantData*) variant.GetData(); \
    return data->GetValue();\
} \
expdecl const classname& classname##RefFromVariant( const wxVariant& variant ) \
{ \
    wxASSERT_MSG( variant.GetType() == wxS(#classname), \
                  wxString::Format(wxS("Variant type should have been '%s'") \
                                   wxS("instead of '%s'"), \
                                   wxS(#classname), \
                                   variant.GetType())); \
    classname##VariantData *data = \
        (classname##VariantData*) variant.GetData(); \
    return data->GetValue();\
}

#define WX_PG_IMPLEMENT_VARIANT_DATA_GETTER(classname, expdecl) \
expdecl classname& operator << ( classname &value, const wxVariant &variant )\
{\
    wxASSERT( variant.GetType() == #classname );\
    \
    classname##VariantData *data = (classname##VariantData*) variant.GetData();\
    value = data->GetValue();\
    return value;\
}

#define WX_PG_IMPLEMENT_VARIANT_DATA_EQ(classname, expdecl) \
bool classname##VariantData::Eq(wxVariantData& data) const \
{\
    wxASSERT( GetType() == data.GetType() );\
\
    classname##VariantData & otherData = (classname##VariantData &) data;\
\
    return otherData.m_value == m_value;\
}

// implements a wxVariantData-derived class using for the Eq() method the
// operator== which must have been provided by "classname"
#define WX_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(classname,expdecl) \
WX_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_NO_EQ_NO_GETTER(classname,wxEMPTY_PARAMETER_VALUE expdecl) \
WX_PG_IMPLEMENT_VARIANT_DATA_GETTER(classname,wxEMPTY_PARAMETER_VALUE expdecl) \
WX_PG_IMPLEMENT_VARIANT_DATA_EQ(classname,wxEMPTY_PARAMETER_VALUE expdecl)

#define WX_PG_IMPLEMENT_VARIANT_DATA(classname) \
WX_PG_IMPLEMENT_VARIANT_DATA_EXPORTED(classname, wxEMPTY_PARAMETER_VALUE)

// with Eq() implementation that always returns false
#define WX_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_DUMMY_EQ(classname,expdecl) \
WX_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_NO_EQ_NO_GETTER(classname,wxEMPTY_PARAMETER_VALUE expdecl) \
WX_PG_IMPLEMENT_VARIANT_DATA_GETTER(classname,wxEMPTY_PARAMETER_VALUE expdecl) \
\
bool classname##VariantData::Eq(wxVariantData& WXUNUSED(data)) const \
{\
    return false; \
}

#define WX_PG_IMPLEMENT_VARIANT_DATA_DUMMY_EQ(classname) \
WX_PG_IMPLEMENT_VARIANT_DATA_EXPORTED_DUMMY_EQ(classname, wxEMPTY_PARAMETER_VALUE)

WX_PG_DECLARE_VARIANT_DATA_EXPORTED(wxPoint, WXDLLIMPEXP_PROPGRID)
WX_PG_DECLARE_VARIANT_DATA_EXPORTED(wxSize, WXDLLIMPEXP_PROPGRID)
WX_PG_DECLARE_VARIANT_DATA_EXPORTED(wxArrayInt, WXDLLIMPEXP_PROPGRID)
DECLARE_VARIANT_OBJECT_EXPORTED(wxFont, WXDLLIMPEXP_PROPGRID)
template<> inline wxVariant WXVARIANT( const wxFont& value )
{
    wxVariant variant;
    variant << value;
    return variant;
}

template<> inline wxVariant WXVARIANT( const wxColour& value )
{
    wxVariant variant;
    variant << value;
    return variant;
}

// Define constants for common wxVariant type strings

#define wxPG_VARIANT_TYPE_STRING        wxS("string")
#define wxPG_VARIANT_TYPE_LONG          wxS("long")
#define wxPG_VARIANT_TYPE_BOOL          wxS("bool")
#define wxPG_VARIANT_TYPE_LIST          wxS("list")
#define wxPG_VARIANT_TYPE_DOUBLE        wxS("double")
#define wxPG_VARIANT_TYPE_ARRSTRING     wxS("arrstring")
#if wxUSE_DATETIME
#define wxPG_VARIANT_TYPE_DATETIME      wxS("datetime")
#endif
#define wxPG_VARIANT_TYPE_LONGLONG      wxS("longlong")
#define wxPG_VARIANT_TYPE_ULONGLONG     wxS("ulonglong")
#endif // !SWIG

// -----------------------------------------------------------------------

//
// Tokenizer macros.
// NOTE: I have made two versions - worse ones (performance and consistency
//   wise) use wxStringTokenizer and better ones (may have unfound bugs)
//   use custom code.
//

#include "wx/tokenzr.h"

// TOKENIZER1 can be done with wxStringTokenizer
#define WX_PG_TOKENIZER1_BEGIN(WXSTRING,DELIMITER) \
    wxStringTokenizer tkz(WXSTRING,DELIMITER,wxTOKEN_RET_EMPTY); \
    while ( tkz.HasMoreTokens() ) \
    { \
        wxString token = tkz.GetNextToken(); \
        token.Trim(true); \
        token.Trim(false);

#define WX_PG_TOKENIZER1_END() \
    }


//
// 2nd version: tokens are surrounded by DELIMITERs (for example, C-style
// strings). TOKENIZER2 must use custom code (a class) for full compliance with
// " surrounded strings with \" inside.
//
// class implementation is in propgrid.cpp
//

class WXDLLIMPEXP_PROPGRID wxPGStringTokenizer
{
public:
    wxPGStringTokenizer( const wxString& str, wxChar delimiter );
    ~wxPGStringTokenizer() = default;

    bool HasMoreTokens(); // not const so we can do some stuff in it
    wxString GetNextToken();

protected:
    const wxString&             m_str;
    wxString::const_iterator    m_curPos;
    wxString                    m_readyToken;
    wxUniChar                   m_delimiter;
};

#define WX_PG_TOKENIZER2_BEGIN(WXSTRING,DELIMITER) \
    wxPGStringTokenizer tkz(WXSTRING,DELIMITER); \
    while ( tkz.HasMoreTokens() ) \
    { \
        wxString token = tkz.GetNextToken();

#define WX_PG_TOKENIZER2_END() \
    }

#endif // wxUSE_PROPGRID

#endif // _WX_PROPGRID_PROPGRIDDEFS_H_
