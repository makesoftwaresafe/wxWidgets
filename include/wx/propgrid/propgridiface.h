/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/propgridiface.h
// Purpose:     wxPropertyGridInterface class
// Author:      Jaakko Salli
// Created:     2008-08-24
// Copyright:   (c) Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_PROPGRID_PROPGRIDIFACE_H__
#define __WX_PROPGRID_PROPGRIDIFACE_H__

#include "wx/defs.h"

#if wxUSE_PROPGRID

#include "wx/propgrid/property.h"
#include "wx/propgrid/propgridpagestate.h"

// -----------------------------------------------------------------------
// wxPropertyGrid Validation Failure behaviour Flags
enum class wxPGVFBFlags : int
{
    // No flags
    Null                   = 0,
    // Prevents user from leaving property unless value is valid. If this
    // behaviour flag is not used, then value change is instead cancelled.
    StayInProperty         = 0x0001,
    // Calls wxBell() on validation failure.
    Beep                   = 0x0002,
    // Cell with invalid value will be marked (with red colour).
    MarkCell               = 0x0004,
    // Display a text message explaining the situation.
    // To customize the way the message is displayed, you need to
    // reimplement wxPropertyGrid::DoShowPropertyError() in a
    // derived class. Default behaviour is to display the text on
    // the top-level frame's status bar, if present, and otherwise
    // using wxMessageBox.
    ShowMessage            = 0x0008,
    // Similar to SHOW_MESSAGE, except always displays the
    // message using wxMessageBox.
    ShowMessageBox         = 0x0010,
    // Similar to SHOW_MESSAGE, except always displays the
    // message on the status bar (when present - you can reimplement
    // wxPropertyGrid::GetStatusBar() in a derived class to specify
    // this yourself).
    ShowMessageOnStatusBar = 0x0020,
    // Defaults.
    Default = MarkCell | ShowMessageBox,
    // Only used internally.
    Undefined              = 0x0040
};

constexpr wxPGVFBFlags operator|(wxPGVFBFlags a, wxPGVFBFlags b)
{
    return static_cast<wxPGVFBFlags>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr wxPGVFBFlags operator&(wxPGVFBFlags a, wxPGVFBFlags b)
{
    return static_cast<wxPGVFBFlags>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr wxPGVFBFlags operator~(wxPGVFBFlags a)
{
    return static_cast<wxPGVFBFlags>(~static_cast<int>(a));
}

constexpr bool operator!(wxPGVFBFlags a)
{
    return static_cast<int>(a) == 0;
}

#if WXWIN_COMPATIBILITY_3_2
// These constants themselves intentionally don't use wxDEPRECATED_MSG()
// because one will be given whenever they are used with any function now
// taking wxPGVFBFlags anyhow and giving multiple deprecation warnings for the
// same line of code is more annoying than helpful.
enum wxPG_VALIDATION_FAILURE_BEHAVIOR_FLAGS
{
    wxPG_VFB_NULL = static_cast<int>(wxPGVFBFlags::Null),
    wxPG_VFB_STAY_IN_PROPERTY = static_cast<int>(wxPGVFBFlags::StayInProperty),
    wxPG_VFB_BEEP = static_cast<int>(wxPGVFBFlags::Beep),
    wxPG_VFB_MARK_CELL = static_cast<int>(wxPGVFBFlags::MarkCell),
    wxPG_VFB_SHOW_MESSAGE = static_cast<int>(wxPGVFBFlags::ShowMessage),
    wxPG_VFB_SHOW_MESSAGEBOX = static_cast<int>(wxPGVFBFlags::ShowMessageBox),
    wxPG_VFB_SHOW_MESSAGE_ON_STATUSBAR = static_cast<int>(wxPGVFBFlags::ShowMessageOnStatusBar),
    wxPG_VFB_DEFAULT = static_cast<int>(wxPGVFBFlags::Default),
    wxPG_VFB_UNDEFINED = static_cast<int>(wxPGVFBFlags::Undefined),
};

wxDEPRECATED_MSG("use wxPGVFBFlags instead")
constexpr bool operator==(wxPGVFBFlags a, int b)
{
    return static_cast<int>(a) == b;
}

wxDEPRECATED_MSG("use wxPGVFBFlags instead")
constexpr bool operator!=(wxPGVFBFlags a, int b)
{
    return static_cast<int>(a) != b;
}
#endif // WXWIN_COMPATIBILITY_3_2

// -----------------------------------------------------------------------

// Most property grid functions have this type as their argument, as it can
// convey a property by either a pointer or name.
class WXDLLIMPEXP_PROPGRID wxPGPropArgCls
{
public:
    wxPGPropArgCls( const wxPGProperty* property )
    {
        m_property = const_cast<wxPGProperty*>(property);
        m_isProperty = true;
    }
    wxPGPropArgCls( const wxString& str )
    {
        m_name = str;
        m_property = nullptr;
        m_isProperty = false;
    }
    wxPGPropArgCls( const wxPGPropArgCls& id )
    {
        m_isProperty = id.m_isProperty;
        m_property = id.m_property;
        m_name = id.m_name;
    }
    // This is only needed for wxPython bindings.
    wxPGPropArgCls( wxString* str, bool WXUNUSED(deallocPtr) )
    {
        m_name = *str;
        delete str; // we own this string
        m_property = nullptr;
        m_isProperty = false;
    }
    wxPGProperty* GetPtr() const
    {
        wxCHECK( m_isProperty, nullptr );
        return m_property;
    }
#ifndef wxNO_IMPLICIT_WXSTRING_ENCODING
    wxPGPropArgCls( const char* str )
    {
        m_name = str;
        m_property = nullptr;
        m_isProperty = false;
    }
#endif // !wxNO_IMPLICIT_WXSTRING_ENCODING
    wxPGPropArgCls( const wchar_t* str )
    {
        m_name = str;
        m_property = nullptr;
        m_isProperty = false;
    }
    // This constructor is required for null.
    wxPGPropArgCls( int )
    {
        m_property = nullptr;
        m_isProperty = true;
    }
    wxPGProperty* GetPtr( wxPropertyGridInterface* iface ) const;
    wxPGProperty* GetPtr( const wxPropertyGridInterface* iface ) const
    {
        return GetPtr(const_cast<wxPropertyGridInterface*>(iface));
    }
    wxPGProperty* GetPtr0() const { return m_property; }
    bool HasName() const { return !m_isProperty; }
    const wxString& GetName() const { return m_name; }

private:
    bool m_isProperty;
    wxPGProperty* m_property;
    wxString m_name;
};

typedef const wxPGPropArgCls& wxPGPropArg;

// -----------------------------------------------------------------------

WXDLLIMPEXP_PROPGRID
void wxPGTypeOperationFailed( const wxPGProperty* p,
                              const wxString& typestr,
                              const wxString& op );
WXDLLIMPEXP_PROPGRID
void wxPGGetFailed( const wxPGProperty* p, const wxString& typestr );

// -----------------------------------------------------------------------

// Helper macro that does necessary preparations when calling
// some wxPGProperty's member function.
#define wxPG_PROP_ARG_CALL_PROLOG_0(PROPERTY) \
    PROPERTY *p = (PROPERTY*)id.GetPtr(this); \
    if ( !p ) return;

#define wxPG_PROP_ARG_CALL_PROLOG_RETVAL_0(PROPERTY, RETVAL) \
    PROPERTY *p = (PROPERTY*)id.GetPtr(this); \
    if ( !p ) return RETVAL;

#define wxPG_PROP_ARG_CALL_PROLOG() \
    wxPG_PROP_ARG_CALL_PROLOG_0(wxPGProperty)

#define wxPG_PROP_ARG_CALL_PROLOG_RETVAL(RVAL) \
    wxPG_PROP_ARG_CALL_PROLOG_RETVAL_0(wxPGProperty, RVAL)

#define wxPG_PROP_ID_CONST_CALL_PROLOG() \
    wxPG_PROP_ARG_CALL_PROLOG_0(const wxPGProperty)

#define wxPG_PROP_ID_CONST_CALL_PROLOG_RETVAL(RVAL) \
    wxPG_PROP_ARG_CALL_PROLOG_RETVAL_0(const wxPGProperty, RVAL)

// -----------------------------------------------------------------------


// Most of the shared property manipulation interface shared by wxPropertyGrid,
// wxPropertyGridPage, and wxPropertyGridManager is defined in this class.
// In separate wxPropertyGrid component this class was known as
// wxPropertyContainerMethods.
// wxPropertyGridInterface's property operation member functions all accept
// a special wxPGPropArg id argument, using which you can refer to properties
// either by their pointer (for performance) or by their name (for conveniency).
class WXDLLIMPEXP_PROPGRID wxPropertyGridInterface
{
public:

    // Destructor.
    virtual ~wxPropertyGridInterface() = default;

    // Appends property to the list.
    // wxPropertyGrid assumes ownership of the object.
    // Becomes child of most recently added category.
    // wxPropertyGrid takes the ownership of the property pointer.
    // If appending a category with name identical to a category already in
    // the wxPropertyGrid, then newly created category is deleted, and most
    // recently added category (under which properties are appended) is set
    // to the one with same name. This allows easier adding of items to same
    // categories in multiple passes.
    // Does not automatically redraw the control, so you may need to call
    // Refresh when calling this function after control has been shown for
    // the first time.
    // This functions deselects selected property, if any. Validation
    // failure option wxPGVFBFlags::StayInProperty is not respected, ie.
    // selection is cleared even if editor had invalid value.
    wxPGProperty* Append( wxPGProperty* property );

    // Same as Append(), but appends under given parent property.
    wxPGProperty* AppendIn( wxPGPropArg id, wxPGProperty* newproperty );

    // In order to add new items into a property with fixed children (for
    // instance, wxFlagsProperty), you need to call this method. After
    // populating has been finished, you need to call EndAddChildren.
    void BeginAddChildren( wxPGPropArg id );

    // Deletes all properties.
    virtual void Clear() = 0;

    // Clears current selection, if any.
    // validation - If set to false, deselecting the property will always work,
    //   even if its editor had invalid value in it.
    // Returns true if successful or if there was no selection. May
    // fail if validation was enabled and active editor had invalid
    // value.
    bool ClearSelection( bool validation = false );

    // Resets modified status of all properties.
    void ClearModifiedStatus();

    // Collapses given category or property with children.
    // Returns true if actually collapses.
    bool Collapse( wxPGPropArg id );

    // Collapses all items that can be collapsed.
    // Return false if failed (may fail if editor value cannot be validated).
    bool CollapseAll() { return ExpandAll(false); }

    // Changes value of a property, as if from an editor.
    // Use this instead of SetPropertyValue() if you need the value to run
    // through validation process, and also send the property change event.
    // Returns true if value was successfully changed.
    bool ChangePropertyValue( wxPGPropArg id, wxVariant newValue );

    // Removes and deletes a property and any children.
    // id - Pointer or name of a property.
    // If you delete a property in a wxPropertyGrid event
    // handler, the actual deletion is postponed until the next
    // idle event.
    // This functions deselects selected property, if any.
    // Validation failure option wxPGVFBFlags::StayInProperty is not
    // respected, ie. selection is cleared even if editor had
    // invalid value.
    void DeleteProperty( wxPGPropArg id );

    // Removes a property. Does not delete the property object, but
    // instead returns it.
    // id - Pointer or name of a property.
    // Removed property cannot have any children.
    // Also, if you remove property in a wxPropertyGrid event
    // handler, the actual removal is postponed until the next
    // idle event.
    wxPGProperty* RemoveProperty( wxPGPropArg id );

    // Disables a property.
    bool DisableProperty( wxPGPropArg id ) { return EnableProperty(id,false); }

    // Returns true if all property grid data changes have been committed.
    // Usually only returns false if value in active editor has been
    // invalidated by a wxValidator.
    bool EditorValidate();

    // Enables or disables property, depending on whether enable is true or
    // false. Disabled property usually appears as having grey text.
    // id - Name or pointer to a property.
    // enable - If false, property is disabled instead.
    bool EnableProperty( wxPGPropArg id, bool enable = true );

    // Called after population of property with fixed children has finished.
    void EndAddChildren( wxPGPropArg id );

    // Expands given category or property with children.
    // Returns true if actually expands.
    bool Expand( wxPGPropArg id );

    // Expands all items that can be expanded.
    bool ExpandAll( bool expand = true );

    // Returns id of first child of given property.
    // Does not return sub-properties!
    wxPGProperty* GetFirstChild( wxPGPropArg id )
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(wxNullProperty)

        if ( !p->HasAnyChild() || p->HasFlag(wxPGFlags::Aggregate) )
            return wxNullProperty;

        return p->Item(0);
    }

    // Returns iterator class instance.
    // flags - See wxPG_ITERATOR_FLAGS. Value wxPG_ITERATE_DEFAULT causes
    //   iteration over everything except private child properties.
    // firstProp - Property to start iteration from. If nullptr, then first
    //   child of root is used.
    wxPropertyGridIterator GetIterator( int flags = wxPG_ITERATE_DEFAULT,
                                        wxPGProperty* firstProp = nullptr )
    {
        return wxPropertyGridIterator( m_pState, flags, firstProp );
    }

    wxPropertyGridConstIterator
    GetIterator( int flags = wxPG_ITERATE_DEFAULT,
                 wxPGProperty* firstProp = nullptr ) const
    {
        return wxPropertyGridConstIterator( m_pState, flags, firstProp );
    }

    // Returns iterator class instance.
    // flags - See wxPG_ITERATOR_FLAGS. Value wxPG_ITERATE_DEFAULT causes
    //   iteration over everything except private child properties.
    // startPos - Either wxTOP or wxBOTTOM. wxTOP will indicate that iterations start
    //   from the first property from the top, and wxBOTTOM means that the
    //   iteration will instead begin from bottommost valid item.
    wxPropertyGridIterator GetIterator( int flags, int startPos )
    {
        return wxPropertyGridIterator( m_pState, flags, startPos );
    }

    wxPropertyGridConstIterator GetIterator( int flags, int startPos ) const
    {
        return wxPropertyGridConstIterator( m_pState, flags, startPos );
    }

    // Returns id of first item that matches given criteria.
    wxPGProperty* GetFirst( int flags = wxPG_ITERATE_ALL )
    {
        wxPropertyGridIterator it( m_pState, flags, wxNullProperty, 1 );
        return *it;
    }

    const wxPGProperty* GetFirst( int flags = wxPG_ITERATE_ALL ) const
    {
        return const_cast<wxPropertyGridInterface*>(this)->GetFirst(flags);
    }

    // Returns pointer to a property with given name (case-sensitive).
    // If there is no property with such name, null pointer is returned.
    // Properties which have non-category, non-root parent
    // cannot be accessed globally by their name. Instead, use
    // "<property>.<subproperty>" instead of "<subproperty>".
    wxPGProperty* GetProperty( const wxString& name ) const
    {
        return GetPropertyByName(name);
    }

    // Returns map-like storage of property's attributes.
    // Note that if extra style wxPG_EX_WRITEONLY_BUILTIN_ATTRIBUTES is set,
    // then builtin-attributes are not included in the storage.
    const wxPGAttributeStorage& GetPropertyAttributes( wxPGPropArg id ) const
    {
        // If 'id' refers to invalid property, then we will return dummy
        // attributes (i.e. root property's attributes, which contents
        // should always be empty and of no consequence).
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(m_pState->DoGetRoot()->GetAttributes())
        return p->GetAttributes();
    }

    // Adds to 'targetArr' pointers to properties that have given
    // flags 'flags' set. However, if 'inverse' is set to true, then
    // only properties without given flags are stored.
    // flags - Property flags to use.
    // iterFlags - Iterator flags to use. Default is everything expect private children.
#if WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use GetPropertiesWithFlag() with 'flags' argument as wxPGFlags")
    void GetPropertiesWithFlag( wxArrayPGProperty* targetArr,
                                int flags,
                                bool inverse = false,
                                int iterFlags = wxPG_ITERATE_PROPERTIES |
                                                wxPG_ITERATE_HIDDEN |
                                                wxPG_ITERATE_CATEGORIES) const
    {
        GetPropertiesWithFlag(targetArr, static_cast<wxPGFlags>(flags), inverse, iterFlags);
    }
#endif // WXWIN_COMPATIBILITY_3_2
    void GetPropertiesWithFlag(wxArrayPGProperty* targetArr,
                               wxPGFlags flags,
                               bool inverse = false,
                               int iterFlags = wxPG_ITERATE_PROPERTIES |
                                               wxPG_ITERATE_HIDDEN |
                                               wxPG_ITERATE_CATEGORIES) const;

    // Returns value of given attribute. If none found, returns null wxVariant.
    wxVariant GetPropertyAttribute( wxPGPropArg id,
                                    const wxString& attrName ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(wxVariant())
        return p->GetAttribute(attrName);
    }

    // Returns pointer of property's nearest parent category. If no category
    // found, returns nullptr.
    wxPropertyCategory* GetPropertyCategory( wxPGPropArg id ) const
    {
        wxPG_PROP_ID_CONST_CALL_PROLOG_RETVAL(nullptr)
        return m_pState->GetPropertyCategory(p);
    }

    // Returns client data (void*) of a property.
    void* GetPropertyClientData( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(nullptr)
        return p->GetClientData();
    }

    // Returns first property which label matches given string.
    // nullptr if none found. Note that this operation is extremely slow when
    // compared to GetPropertyByName().
    wxPGProperty* GetPropertyByLabel( const wxString& label ) const;

    // Returns pointer to a property with given name (case-sensitive).
    // If there is no property with such name, null pointer is returned.
    wxPGProperty* GetPropertyByName( const wxString& name ) const;

    // Returns child property 'subname' of property 'name'. Same as
    // calling GetPropertyByName("name.subname"), albeit slightly faster.
    wxPGProperty* GetPropertyByName( const wxString& name,
                                     const wxString& subname ) const;

    // Returns property's editor.
    const wxPGEditor* GetPropertyEditor( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(nullptr)
        return p->GetEditorClass();
    }

    // Returns help string associated with a property.
    wxString GetPropertyHelpString( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(wxString())
        return p->GetHelpString();
    }

    // Returns property's custom value image (nullptr if none).
    wxBitmap* GetPropertyImage( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(nullptr)
        return p->GetValueImage();
    }

    // Returns label of a property.
    const wxString& GetPropertyLabel( wxPGPropArg id )
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(m_emptyString)
        return p->GetLabel();
    }

    // Returns name of a property, by which it is globally accessible.
    wxString GetPropertyName( wxPGProperty* property )
    {
        return property->GetName();
    }

    // Returns parent item of a property.
    wxPGProperty* GetPropertyParent( wxPGPropArg id )
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(wxNullProperty)
        return p->GetParent();
    }

#if wxUSE_VALIDATORS
    // Returns validator of a property as a reference, which you
    // can pass to any number of SetPropertyValidator.
    wxValidator* GetPropertyValidator( wxPGPropArg id )
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(nullptr)
        return p->GetValidator();
    }
#endif

    // Returns value as wxVariant. To get wxObject pointer from it,
    // you will have to use WX_PG_VARIANT_TO_WXOBJECT(VARIANT,CLASSNAME) macro.
    // If property value is unspecified, null wxVariant is returned.
    wxVariant GetPropertyValue(wxPGPropArg id);

    wxString GetPropertyValueAsString( wxPGPropArg id ) const;
    long GetPropertyValueAsLong( wxPGPropArg id ) const;
    unsigned long GetPropertyValueAsULong( wxPGPropArg id ) const
    {
        return (unsigned long) GetPropertyValueAsLong(id);
    }
    int GetPropertyValueAsInt( wxPGPropArg id ) const
        { return (int)GetPropertyValueAsLong(id); }
    bool GetPropertyValueAsBool( wxPGPropArg id ) const;
    double GetPropertyValueAsDouble( wxPGPropArg id ) const;

    wxArrayString GetPropertyValueAsArrayString(wxPGPropArg id) const;

    wxLongLong_t GetPropertyValueAsLongLong(wxPGPropArg id) const;

    wxULongLong_t GetPropertyValueAsULongLong(wxPGPropArg id) const;

    wxArrayInt GetPropertyValueAsArrayInt(wxPGPropArg id) const;

#if wxUSE_DATETIME
    wxDateTime GetPropertyValueAsDateTime(wxPGPropArg id) const;
#endif

    // Returns a wxVariant list containing wxVariant versions of all
    // property values. Order is not guaranteed.
    // flags - Use wxPGPropertyValuesFlags::KeepStructure to retain category structure; each sub
    // category will be its own wxVariantList of wxVariant.
    // Use wxPGPropertyValuesFlags::IncAttributes to include property attributes as well.
    // Each attribute will be stored as list variant named
    // "@<propname>@attr."
#if WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use GetPropertyValues with flags argument as wxPGPropertyValuesFlags")
    wxVariant GetPropertyValues(const wxString& listname, wxPGProperty* baseparent, long flags) const
    {
        return m_pState->DoGetPropertyValues(listname, baseparent, static_cast<wxPGPropertyValuesFlags>(flags));
    }
#endif // WXWIN_COMPATIBILITY_3_2
    wxVariant GetPropertyValues(const wxString& listname = wxString(),
                                wxPGProperty* baseparent = nullptr,
                                wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::DontRecurse) const
    {
        return m_pState->DoGetPropertyValues(listname, baseparent, flags);
    }

    // Returns currently selected property. nullptr if none.
    // When wxPG_EX_MULTIPLE_SELECTION extra style is used, this
    // member function returns the focused property, that is the
    // one which can have active editor.
    wxPGProperty* GetSelection() const;

    // Returns list of currently selected properties.
    // wxArrayPGProperty should be compatible with std::vector API.
    const wxArrayPGProperty& GetSelectedProperties() const
    {
        return m_pState->m_selection;
    }

    wxPropertyGridPageState* GetState() const { return m_pState; }

    // Similar to GetIterator(), but instead returns wxPGVIterator instance,
    // which can be useful for forward-iterating through arbitrary property
    // containers.
    // flags - See wxPG_ITERATOR_FLAGS.
    virtual wxPGVIterator GetVIterator( int flags ) const;

    // Hides or reveals a property.
    // hide - If true, hides property, otherwise reveals it.
    // flags - By default changes are applied recursively. Set this parameter
    //   wxPGPropertyValuesFlags::DontRecurse to prevent this.
#if WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use HideProperty with flags argument as wxPGPropertyValuesFlags")
    bool HideProperty(wxPGPropArg id, bool hide, int flags)
    {
        return HideProperty(id, hide, static_cast<wxPGPropertyValuesFlags>(flags));
    }
#endif // WXWIN_COMPATIBILITY_3_2
    bool HideProperty(wxPGPropArg id, bool hide = true,
                      wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::Recurse);

#if wxPG_INCLUDE_ADVPROPS
    // Initializes *all* property types. Causes references to most object
    // files in the library, so calling this may cause significant increase
    // in executable size when linking with static library.
    static void InitAllTypeHandlers();
#else
    static void InitAllTypeHandlers() { }
#endif

    // Inserts property to the property container.
    // priorThis - New property is inserted just prior to this. Available only
    //   in the first variant. There are two versions of this function
    //   to allow this parameter to be either an id or name to
    //   a property.
    // newproperty - Pointer to the inserted property. wxPropertyGrid will take
    //   ownership of this object.
    // Returns id for the property,
    // wxPropertyGrid takes the ownership of the property pointer.
    // While Append may be faster way to add items, make note that when
    // both types of data storage (categoric and
    // non-categoric) are active, Insert becomes even more slow. This is
    // especially true if current mode is non-categoric.
    // Example of use:
    //  // append category
    //  wxPGProperty* my_cat_id = propertygrid->Append(
    //     new wxPropertyCategory("My Category") );
    //  ...
    //  // insert into category - using second variant
    //  wxPGProperty* my_item_id_1 = propertygrid->Insert(
    //     my_cat_id, 0, new wxStringProperty("My String 1") );
    // // insert before to first item - using first variant
    //  wxPGProperty* my_item_id_2 = propertygrid->Insert(
    //     my_item_id, new wxStringProperty("My String 2") );
    wxPGProperty* Insert( wxPGPropArg priorThis, wxPGProperty* newproperty );

    // Inserts property to the property container.
    //See the other overload for more details.
    // parent - New property is inserted under this category. Available only
    //   in the second variant. There are two versions of this function
    //   to allow this parameter to be either an id or name to
    //   a property.
    // index - Index under category. Available only in the second variant.
    //   If index is < 0, property is appended in category.
    // newproperty - Pointer to the inserted property. wxPropertyGrid will take
    // ownership of this object.
    // Returns id for the property.
    wxPGProperty* Insert( wxPGPropArg parent,
                          int index,
                          wxPGProperty* newproperty );

    // Returns true if property is a category.
    bool IsPropertyCategory( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return p->IsCategory();
    }

    // Returns true if property is enabled.
    bool IsPropertyEnabled( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return !p->HasFlag(wxPGFlags::Disabled);
    }

    // Returns true if given property is expanded.
    // Naturally, always returns false for properties that cannot be expanded.
    bool IsPropertyExpanded( wxPGPropArg id ) const;

    // Returns true if property has been modified after value set or modify
    // flag clear by software.
    bool IsPropertyModified( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return p->HasFlag(wxPGFlags::Modified);
    }

    // Returns true if property is selected.
    bool IsPropertySelected( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return m_pState->DoIsPropertySelected(p);
    }

    // Returns true if property is shown (i.e. HideProperty with true not
    // called for it).
    bool IsPropertyShown( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return !p->HasFlag(wxPGFlags::Hidden);
    }

    // Returns true if property value is set to unspecified.
    bool IsPropertyValueUnspecified( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(false)
        return p->IsValueUnspecified();
    }

    // Disables (limit = true) or enables (limit = false) wxTextCtrl editor
    // of a property, if it is not the sole mean to edit the value.
    void LimitPropertyEditing( wxPGPropArg id, bool limit = true );

    // If state is shown in its grid, refresh it now.
    virtual void RefreshGrid( wxPropertyGridPageState* state = nullptr );

#if wxPG_INCLUDE_ADVPROPS
    // Initializes additional property editors (SpinCtrl etc.). Causes
    // references to most object files in the library, so calling this may
    // cause significant increase in executable size when linking with static
    // library.
    static void RegisterAdditionalEditors();
#else
    static void RegisterAdditionalEditors() { }
#endif

    // Replaces property with id with newly created property. For example,
    // this code replaces existing property named "Flags" with one that
    // will have different set of items:
    //   pg->ReplaceProperty("Flags",
    //            wxFlagsProperty("Flags", wxPG_LABEL, newItems))
    // For more info, see wxPropertyGrid::Insert.
    wxPGProperty* ReplaceProperty( wxPGPropArg id, wxPGProperty* property );

    // Flags for wxPropertyGridInterface::SaveEditableState()
    // and wxPropertyGridInterface::RestoreEditableState().
    enum EditableStateFlags
    {
        // Include selected property.
        SelectionState   = 0x01,
        // Include expanded/collapsed property information.
        ExpandedState    = 0x02,
        // Include scrolled position.
        ScrollPosState   = 0x04,
        // Include selected page information.
        // Only applies to wxPropertyGridManager.
        PageState        = 0x08,
        // Include splitter position. Stored for each page.
        SplitterPosState = 0x10,
        // Include description box size.
        // Only applies to wxPropertyGridManager.
        DescBoxState     = 0x20,

        // Include all supported user editable state information.
        // This is usually the default value.
        AllStates        = SelectionState |
                           ExpandedState |
                           ScrollPosState |
                           PageState |
                           SplitterPosState |
                           DescBoxState
    };

    // Restores user-editable state.
    // See also wxPropertyGridInterface::SaveEditableState().
    // src - String generated by SaveEditableState.
    // restoreStates - Which parts to restore from source string. See @ref
    //   propgridinterface_editablestate_flags "list of editable state
    //   flags".
    // Returns false if there was problem reading the string.
    // If some parts of state (such as scrolled or splitter position) fail to
    // restore correctly, please make sure that you call this function after
    // wxPropertyGrid size has been set (this may sometimes be tricky when
    // sizers are used).
    bool RestoreEditableState( const wxString& src,
                               int restoreStates = AllStates );

    // Used to acquire user-editable state (selected property, expanded
    // properties, scrolled position, splitter positions).
    // includedStates - Which parts of state to include. See EditableStateFlags.
    wxString SaveEditableState( int includedStates = AllStates ) const;

    // Lets user set the strings listed in the choice dropdown of a
    // wxBoolProperty. Defaults are "True" and "False", so changing them to,
    // say, "Yes" and "No" may be useful in some less technical applications.
    static void SetBoolChoices( const wxString& trueChoice,
                                const wxString& falseChoice );

    // Set proportion of a auto-stretchable column. wxPG_SPLITTER_AUTO_CENTER
    // window style needs to be used to indicate that columns are auto-
    // resizable.
    // Returns false on failure.
    // You should call this for individual pages of wxPropertyGridManager
    // (if used).
    bool SetColumnProportion( unsigned int column, int proportion );

    // Returns auto-resize proportion of the given column.
    int GetColumnProportion( unsigned int column ) const
    {
        return m_pState->DoGetColumnProportion(column);
    }

    // Sets an attribute for this property.
    // name - Text identifier of attribute. See @ref propgrid_property_attributes.
    // value - Value of attribute.
    // flags - Optional. Use wxPGPropertyValuesFlags::Recurse to set the attribute to child
    //   properties recursively.
    // Setting attribute's value to null wxVariant will simply remove it
    // from property's set of attributes.
#if WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use SetPropertyAttribute with 'flags' argument as wxPGPropertyValuesFlags")
    void SetPropertyAttribute(wxPGPropArg id, const wxString& attrName,
                              wxVariant value, long flags)
    {
        DoSetPropertyAttribute(id, attrName, value, static_cast<wxPGPropertyValuesFlags>(flags));
    }
#endif // WXWIN_COMPATIBILITY_3_2
    void SetPropertyAttribute(wxPGPropArg id, const wxString& attrName, wxVariant value,
                              wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::DontRecurse)
    {
        DoSetPropertyAttribute(id, attrName, value, flags);
    }
    // Sets property attribute for all applicable properties.
    // Be sure to use this method only after all properties have been
    // added to the grid.
    void SetPropertyAttributeAll( const wxString& attrName, wxVariant value );

    // Sets background colour of a property.
    // id - Property name or pointer.
    // colour - New background colour.
    // flags - Default is wxPGPropertyValuesFlags::Recurse which causes colour to be set recursively.
    //   Omit this flag to only set colour for the property in question
    //   and not any of its children.
#if WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use SetPropertyBackgroundColour with flags argument as wxPGPropertyValuesFlags")
    void SetPropertyBackgroundColour(wxPGPropArg id, const wxColour& colour, int flags)
    {
        SetPropertyBackgroundColour(id, colour, static_cast<wxPGPropertyValuesFlags>(flags));
    }
#endif // WXWIN_COMPATIBILITY_3_2
    void SetPropertyBackgroundColour(wxPGPropArg id, const wxColour& colour,
                                     wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::Recurse);

    // Resets text and background colours of given property.
    // id - Property name or pointer.
    // flags - Default is wxPGPropertyValuesFlags::DontRecurse which causes colour to be reset
    //   only for the property in question (for backward compatibility).
#if WXWIN_COMPATIBILITY_3_0 || WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use SetPropertyColoursToDefault with flags argument as wxPGPropertyValuesFlags")
    void SetPropertyColoursToDefault(wxPGPropArg id, int flags)
    {
        SetPropertyColoursToDefault(id, static_cast<wxPGPropertyValuesFlags>(flags));
    }
#endif // WXWIN_COMPATIBILITY_3_0 || WXWIN_COMPATIBILITY_3_2
    void SetPropertyColoursToDefault(wxPGPropArg id, wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::DontRecurse);

    // Sets text colour of a property.
    // id - Property name or pointer.
    // colour - New background colour.
    // flags - Default is wxPGPropertyValuesFlags::Recurse which causes colour to be set recursively.
    //   Omit this flag to only set colour for the property in question
    //   and not any of its children.
#if WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use SetPropertyTextColour with flags argument as wxPGPropertyValuesFlags")
    void SetPropertyTextColour(wxPGPropArg id, const wxColour& col, int flags)
    {
        SetPropertyTextColour(id, col, static_cast<wxPGPropertyValuesFlags>(flags));
    }
#endif // WXWIN_COMPATIBILITY_3_2
    void SetPropertyTextColour(wxPGPropArg id, const wxColour& col,
                               wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::Recurse);

    // Returns background colour of first cell of a property.
    wxColour GetPropertyBackgroundColour( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(wxColour())
        return p->GetCell(0).GetBgCol();
    }

    // Returns text colour of first cell of a property.
    wxColour GetPropertyTextColour( wxPGPropArg id ) const
    {
        wxPG_PROP_ARG_CALL_PROLOG_RETVAL(wxColour())
        return p->GetCell(0).GetFgCol();
    }

    // Sets text, bitmap, and colours for given column's cell.
    // You can set label cell by setting column to 0.
    // You can use wxPG_LABEL as text to use default text for column.
    void SetPropertyCell( wxPGPropArg id,
                          int column,
                          const wxString& text = wxString(),
                          const wxBitmapBundle& bitmap = wxBitmapBundle(),
                          const wxColour& fgCol = wxNullColour,
                          const wxColour& bgCol = wxNullColour );

    // Sets client data (void*) of a property.
    // This untyped client data has to be deleted manually.
    void SetPropertyClientData( wxPGPropArg id, void* clientData )
    {
        wxPG_PROP_ARG_CALL_PROLOG()
        p->SetClientData(clientData);
    }

    // Sets editor for a property.
    // editor - For builtin editors, use wxPGEditor_X, where X is builtin editor's
    // name (TextCtrl, Choice, etc. see wxPGEditor documentation for full
    // list).
    // For custom editors, use pointer you received from
    // wxPropertyGrid::RegisterEditorClass().
    void SetPropertyEditor( wxPGPropArg id, const wxPGEditor* editor )
    {
        wxPG_PROP_ARG_CALL_PROLOG()
        wxCHECK_RET( editor, wxS("unknown/null editor") );
        p->SetEditor(editor);
        RefreshProperty(p);
    }

    // Sets editor control of a property. As editor argument, use
    // editor name string, such as "TextCtrl" or "Choice".
    void SetPropertyEditor( wxPGPropArg id, const wxString& editorName )
    {
        SetPropertyEditor(id,GetEditorByName(editorName));
    }

    // Sets label of a property.
    // Properties under same parent may have same labels. However,
    // property names must still remain unique.
    void SetPropertyLabel( wxPGPropArg id, const wxString& newproplabel );

    // Sets name of a property.
    // id - Name or pointer of property which name to change.
    // newName - New name for property.
    void SetPropertyName( wxPGPropArg id, const wxString& newName )
    {
        wxPG_PROP_ARG_CALL_PROLOG()
        m_pState->DoSetPropertyName( p, newName );
    }

    // Sets property (and, recursively, its children) to have read-only value.
    // In other words, user cannot change the value in the editor, but they
    // can still copy it.
    // This is mainly for use with textctrl editor. Not all other editors fully
    // support it.
    // By default changes are applied recursively. Set parameter "flags"
    // to wxPGPropertyValuesFlags::DontRecurse to prevent this.
#if WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use SetPropertyReadOnly with flags argument as wxPGPropertyValuesFlags")
    void SetPropertyReadOnly(wxPGPropArg id, bool set, int flags)
    {
        SetPropertyReadOnly(id, set, static_cast<wxPGPropertyValuesFlags>(flags));
    }
#endif // WXWIN_COMPATIBILITY_3_2
    void SetPropertyReadOnly(wxPGPropArg id, bool set = true,
                             wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::Recurse);

    // Sets property's value to unspecified.
    // If it has children (it may be category), then the same thing is done to
    // them.
    void SetPropertyValueUnspecified( wxPGPropArg id )
    {
        wxPG_PROP_ARG_CALL_PROLOG()
        p->SetValueToUnspecified();
    }

    // Sets property values from a list of wxVariants.
    void SetPropertyValues( const wxVariantList& list,
                            wxPGPropArg defaultCategory = wxNullProperty )
    {
        wxPGProperty* p = defaultCategory.HasName() ?
                          defaultCategory.GetPtr(this) : defaultCategory.GetPtr0();
        m_pState->DoSetPropertyValues(list, p);
    }

    // Sets property values from a list of wxVariants.
    void SetPropertyValues( const wxVariant& list,
                            wxPGPropArg defaultCategory = wxNullProperty )
    {
        SetPropertyValues(list.GetList(),defaultCategory);
    }

    // Associates the help string with property.
    // By default, text is shown either in the manager's "description"
    // text box or in the status bar. If extra window style
    // wxPG_EX_HELP_AS_TOOLTIPS is used, then the text will appear as a
    // tooltip.
    void SetPropertyHelpString( wxPGPropArg id, const wxString& helpString )
    {
        wxPG_PROP_ARG_CALL_PROLOG()
        p->SetHelpString(helpString);
    }

    // Set wxBitmap in front of the value.
    // Bitmap will be scaled to a size returned by
    // wxPropertyGrid::GetImageSize();
    void SetPropertyImage( wxPGPropArg id, const wxBitmapBundle& bmp )
    {
        wxPG_PROP_ARG_CALL_PROLOG()
        p->SetValueImage(bmp);
        RefreshProperty(p);
    }

    // Sets max length of property's text.
    bool SetPropertyMaxLength( wxPGPropArg id, int maxLen );

#if wxUSE_VALIDATORS
    // Sets validator of a property.
    void SetPropertyValidator( wxPGPropArg id, const wxValidator& validator )
    {
        wxPG_PROP_ARG_CALL_PROLOG()
        p->SetValidator(validator);
    }
#endif

    // Sets value (long integer) of a property.
    void SetPropertyValue( wxPGPropArg id, long value )
    {
        wxVariant v(value);
        SetPropVal( id, v );
    }

    // Sets value (integer) of a property.
    void SetPropertyValue( wxPGPropArg id, int value )
    {
        wxVariant v((long)value);
        SetPropVal( id, v );
    }

    // Sets value (floating point) of a property.
    void SetPropertyValue( wxPGPropArg id, double value )
    {
        wxVariant v(value);
        SetPropVal( id, v );
    }

    // Sets value (bool) of a property.
    void SetPropertyValue( wxPGPropArg id, bool value )
    {
        wxVariant v(value);
        SetPropVal( id, v );
    }

    // Sets value (wchar_t*) of a property.
    void SetPropertyValue( wxPGPropArg id, const wchar_t* value )
    {
        SetPropertyValueString( id, wxString(value) );
    }

#ifndef wxNO_IMPLICIT_WXSTRING_ENCODING
    // Sets value (char*) of a property.
    void SetPropertyValue( wxPGPropArg id, const char* value )
    {
        SetPropertyValueString( id, wxString(value) );
    }
#endif // !wxNO_IMPLICIT_WXSTRING_ENCODING

    // Sets value (string) of a property.
    void SetPropertyValue( wxPGPropArg id, const wxString& value )
    {
        SetPropertyValueString( id, value );
    }

    // Sets value (wxArrayString) of a property.
    void SetPropertyValue( wxPGPropArg id, const wxArrayString& value )
    {
        wxVariant v(value);
        SetPropVal( id, v );
    }

#if wxUSE_DATETIME
    // Sets value (wxDateTime) of a property.
    void SetPropertyValue( wxPGPropArg id, const wxDateTime& value )
    {
        wxVariant v(value);
        SetPropVal( id, v );
    }
#endif

    // Sets value (wxObject*) of a property.
    void SetPropertyValue( wxPGPropArg id, wxObject* value )
    {
        wxVariant v(value);
        SetPropVal( id, v );
    }

    // Sets value (wxObject&) of a property.
    void SetPropertyValue( wxPGPropArg id, wxObject& value )
    {
        wxVariant v(&value);
        SetPropVal( id, v );
    }

    // Sets value (native 64-bit int) of a property.
    void SetPropertyValue(wxPGPropArg id, wxLongLong_t value)
    {
        wxVariant v = wxLongLong(value);
        SetPropVal(id, v);
    }
    // Sets value (wxLongLong) of a property.
    void SetPropertyValue( wxPGPropArg id, wxLongLong value )
    {
        wxVariant v(value);
        SetPropVal( id, v );
    }
    // Sets value (native 64-bit unsigned int) of a property.
    void SetPropertyValue(wxPGPropArg id, wxULongLong_t value)
    {
        wxVariant v = wxULongLong(value);
        SetPropVal(id, v);
    }
    // Sets value (wxULongLong) of a property.
    void SetPropertyValue( wxPGPropArg id, wxULongLong value )
    {
        wxVariant v(value);
        SetPropVal( id, v );
    }

    // Sets value (wxArrayInt&) of a property.
    void SetPropertyValue( wxPGPropArg id, const wxArrayInt& value )
    {
        wxVariant v = WXVARIANT(value);
        SetPropVal( id, v );
    }

    // Sets value (wxString) of a property.
    // This method uses wxPGProperty::SetValueFromString, which all properties
    // should implement. This means that there should not be a type error,
    // and instead the string is converted to property's actual value type.
    void SetPropertyValueString( wxPGPropArg id, const wxString& value );

    // Sets value (wxVariant) of a property.
    // Use wxPropertyGrid::ChangePropertyValue() instead if you need to run
    // through validation process and send property change event.
    void SetPropertyValue( wxPGPropArg id, wxVariant value )
    {
        SetPropVal( id, value );
    }

    // Sets value (wxVariant&) of a property. Same as SetPropertyValue,
    // but accepts reference.
    void SetPropVal( wxPGPropArg id, wxVariant& value );

    // Adjusts how wxPropertyGrid behaves when invalid value is entered
    // in a property.
    // vfbFlags - See wxPG_VALIDATION_FAILURE_BEHAVIOR_FLAGS for possible values.
#if WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use SetValidationFailureBehavior with wxPGVFBFlags argument")
    void SetValidationFailureBehavior(int vfbFlags)
    {
        SetValidationFailureBehavior(static_cast<wxPGVFBFlags>(vfbFlags));
    }
#endif // WXWIN_COMPATIBILITY_3_2
    void SetValidationFailureBehavior(wxPGVFBFlags vfbFlags);

    // Sorts all properties recursively.
    // flags - This can contain any of the following options:
    //    wxPGPropertyValuesFlags::SortTopLevelOnly: Only sort categories and their
    //    immediate children. Sorting done by wxPG_AUTO_SORT option
    //    uses this.
    // See SortChildren, wxPropertyGrid::SetSortFunction
#if WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use Sort with wxPGPropertyValuesFlags argument")
    void Sort(int flags)
    {
        Sort(static_cast<wxPGPropertyValuesFlags>(flags));
    }
#endif // WXWIN_COMPATIBILITY_3_2
    void Sort(wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::DontRecurse);

    // Sorts children of a property.
    // id - Name or pointer to a property.
    // flags - This can contain any of the following options:
    //   wxPGPropertyValuesFlags::Recurse: Sorts recursively.
    // See Sort, wxPropertyGrid::SetSortFunction
#if WXWIN_COMPATIBILITY_3_2
    wxDEPRECATED_MSG("use SortChildren with wxPGPropertyValuesFlags argument")
    void SortChildren(wxPGPropArg id, int flags)
    {
        wxPG_PROP_ARG_CALL_PROLOG()
        m_pState->DoSortChildren(p, static_cast<wxPGPropertyValuesFlags>(flags));
    }
#endif // WXWIN_COMPATIBILITY_3_2
    void SortChildren(wxPGPropArg id, wxPGPropertyValuesFlags flags = wxPGPropertyValuesFlags::DontRecurse)
    {
        wxPG_PROP_ARG_CALL_PROLOG()
        m_pState->DoSortChildren(p, flags);
    }

    // GetPropertyByName() with nice assertion error message.
    wxPGProperty* GetPropertyByNameA( const wxString& name ) const;

    // Returns editor pointer of editor with given name.
    static wxPGEditor* GetEditorByName( const wxString& editorName );

    // NOTE: This function reselects the property and may cause
    //       excess flicker, so to just call Refresh() on a rect
    //       of single property, call DrawItem() instead.
    virtual void RefreshProperty( wxPGProperty* p ) = 0;

protected:

    bool DoClearSelection( bool validation = false,
                           wxPGSelectPropertyFlags selFlags = wxPGSelectPropertyFlags::Null );

    // In derived class, implement to set editable state component with
    // given name to given value.
    virtual bool SetEditableStateItem( const wxString& name, wxVariant value )
    {
        wxUnusedVar(name);
        wxUnusedVar(value);
        return false;
    }

    // In derived class, implement to return editable state component with
    // given name.
    virtual wxVariant GetEditableStateItem( const wxString& name ) const
    {
        wxUnusedVar(name);
        return wxVariant();
    }

    // Returns page state data for given (sub) page (-1 means current page).
    virtual wxPropertyGridPageState* GetPageState( int pageIndex ) const
    {
        return pageIndex <= 0 ? m_pState : nullptr;
    }

    virtual bool DoSelectPage( int WXUNUSED(index) ) { return true; }

    // Default call's m_pState's BaseGetPropertyByName
    virtual wxPGProperty* DoGetPropertyByName( const wxString& name ) const;

    // Deriving classes must set this (it must be only or current page).
    wxPropertyGridPageState*         m_pState;

    // Intermediate version needed due to wxVariant copying inefficiency
    void DoSetPropertyAttribute( wxPGPropArg id,
                                 const wxString& name,
                                 wxVariant& value, wxPGPropertyValuesFlags flags );

    // Empty string object to return from member functions returning const
    // wxString&.
    wxString                    m_emptyString;

private:
    // Cannot be GetGrid() due to ambiguity issues.
    wxPropertyGrid* GetPropertyGrid()
    {
        return m_pState ? m_pState->GetGrid() : nullptr;
    }

    // Cannot be GetGrid() due to ambiguity issues.
    const wxPropertyGrid* GetPropertyGrid() const
    {
        return m_pState ? m_pState->GetGrid() : nullptr;
    }
};

#endif // wxUSE_PROPGRID

#endif // __WX_PROPGRID_PROPGRIDIFACE_H__
