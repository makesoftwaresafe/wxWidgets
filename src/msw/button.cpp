/////////////////////////////////////////////////////////////////////////////
// Name:        src/msw/button.cpp
// Purpose:     wxButton
// Author:      Julian Smart
// Created:     04/01/98
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_BUTTON

#include "wx/button.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/brush.h"
    #include "wx/panel.h"
    #include "wx/bmpbuttn.h"
    #include "wx/settings.h"
    #include "wx/dcscreen.h"
    #include "wx/dcclient.h"
    #include "wx/toplevel.h"
    #include "wx/msw/wrapcctl.h"
    #include "wx/msw/private.h"
    #include "wx/msw/missing.h"
#endif

#include "wx/imaglist.h"
#include "wx/stockitem.h"
#include "wx/msw/private/button.h"
#include "wx/msw/private/dc.h"
#include "wx/private/rescale.h"
#include "wx/private/window.h"

#if wxUSE_MARKUP
    #include "wx/generic/private/markuptext.h"
#endif // wxUSE_MARKUP

// set the value for BCM_SETSHIELD (for the UAC shield) if it's not defined in
// the header
#ifndef BCM_SETSHIELD
    #define BCM_SETSHIELD       0x160c
#endif

// ----------------------------------------------------------------------------
// macros
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(wxButton, wxButtonBase)
    EVT_CHAR_HOOK(wxButton::OnCharHook)
wxEND_EVENT_TABLE()

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// creation/destruction
// ----------------------------------------------------------------------------

bool wxButton::Create(wxWindow *parent,
                      wxWindowID id,
                      const wxString& lbl,
                      const wxPoint& pos,
                      const wxSize& size,
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    wxString label;
    if ( !(style & wxBU_NOTEXT) )
    {
        label = lbl;
        if (label.empty() && wxIsStockID(id))
        {
            // On Windows, some buttons aren't supposed to have mnemonics
            label = wxGetStockLabel
                    (
                        id,
                        id == wxID_OK || id == wxID_CANCEL || id == wxID_CLOSE
                            ? wxSTOCK_NOFLAGS
                            : wxSTOCK_WITH_MNEMONIC
                    );
        }
    }

    if ( !CreateControl(parent, id, pos, size, style, validator, name) )
        return false;

    WXDWORD exstyle;
    WXDWORD msStyle = MSWGetStyle(style, &exstyle);

    // if the label contains several lines we must explicitly tell the button
    // about it or it wouldn't draw it correctly ("\n"s would just appear as
    // black boxes)
    //
    // NB: we do it here and not in MSWGetStyle() because we need the label
    //     value and the label is not set yet when MSWGetStyle() is called
    msStyle |= wxMSWButton::GetMultilineStyle(label);

    return MSWCreateControl(wxT("BUTTON"), msStyle, pos, size, label, exstyle);
}

wxButton::~wxButton()
{
    wxTopLevelWindow *tlw = wxDynamicCast(wxGetTopLevelParent(this), wxTopLevelWindow);
    if ( tlw && tlw->GetTmpDefaultItem() == this )
    {
        UnsetTmpDefault();
    }
}

// ----------------------------------------------------------------------------
// flags
// ----------------------------------------------------------------------------

WXDWORD wxButton::MSWGetStyle(long style, WXDWORD *exstyle) const
{
    // buttons never have an external border, they draw their own one
    WXDWORD msStyle = wxControl::MSWGetStyle
                      (
                        (style & ~wxBORDER_MASK) | wxBORDER_NONE, exstyle
                      );

    // we must use WS_CLIPSIBLINGS with the buttons or they would draw over
    // each other in any resizable dialog which has more than one button in
    // the bottom
    msStyle |= WS_CLIPSIBLINGS;

    // don't use "else if" here: weird as it is, but you may combine wxBU_LEFT
    // and wxBU_RIGHT to get BS_CENTER!
    if ( style & wxBU_LEFT )
        msStyle |= BS_LEFT;
    if ( style & wxBU_RIGHT )
        msStyle |= BS_RIGHT;
    if ( style & wxBU_TOP )
        msStyle |= BS_TOP;
    if ( style & wxBU_BOTTOM )
        msStyle |= BS_BOTTOM;
    // flat 2d buttons
    if ( style & wxNO_BORDER )
        msStyle |= BS_FLAT;

    return msStyle;
}

/* static */
wxSize wxButtonBase::GetDefaultSize(wxWindow* win)
{
    static wxPrivate::DpiDependentValue<wxSize> s_sizeBtn;

    if ( s_sizeBtn.HasChanged(win) )
    {
        // The "Recommended sizing and spacing" section of MSDN layout article
        // documents the default button size as being 50*14 dialog units or
        // 75*23 relative pixels (what we call DIPs). But dialog units don't
        // work well in high DPI (and not just because of rounding errors, i.e.
        // the values differ from the actual default button size used by
        // Windows itself in high DPI by too much), so we use the ad hoc
        // formula fitting the sizes of the buttons in the standard message box
        // (which differ from the sizes of the buttons used by "Explorer"
        // which, in turn, differ from the sizes of the buttons in "Open file"
        // dialogs -- in short, it's a mess).
        s_sizeBtn.SetAtNewDPI(
            wxWindow::FromDIP(wxSize(77, 25), win) - wxSize(2, 2)
        );
    }

    return s_sizeBtn.Get();
}

// ----------------------------------------------------------------------------
// default button handling
// ----------------------------------------------------------------------------

/*
   In normal Windows programs there is no need to handle default button
   manually because this is taken care by the system provided you use
   WM_NEXTDLGCTL and not just SetFocus() to switch focus betweeh the controls
   (see http://blogs.msdn.com/oldnewthing/archive/2004/08/02/205624.aspx for
   the full explanation why just calling SetFocus() is not enough).

   However this only works if the window is a dialog, i.e. uses DefDlgProc(),
   but not with plain windows using DefWindowProc() and we do want to have
   default buttons inside frames as well, so we're forced to reimplement all
   this logic ourselves. It would be great to avoid having to do this but using
   DefDlgProc() for all the windows would almost certainly result in more
   problems, we'd need to carefully filter messages and pass some of them to
   DefWindowProc() and some of them to DefDlgProc() which looks dangerous (what
   if the handling of some message changes in some Windows version?), so doing
   this ourselves is probably a lesser evil.

   Read the rest to learn everything you ever wanted to know about the default
   buttons but were afraid to ask.


   In MSW the default button should be activated when the user presses Enter
   and the current control doesn't process Enter itself somehow. This is
   handled by ::DefWindowProc() (or maybe ::DefDialogProc()) using DM_SETDEFID
   Another aspect of "defaultness" is that the default button has different
   appearance: this is due to BS_DEFPUSHBUTTON style which is only partially
   handled by using DM_SETDEFID. Also note that BS_DEFPUSHBUTTON should
   be unset if our parent window is not active so it should be unset whenever
   we lose activation and set back when we regain it.

   Final complication is that when a button is active, it should be the default
   one, i.e. pressing Enter on a button always activates it and not another
   one.

   We handle this by maintaining a permanent and a temporary default items in
   wxControlContainer (both may be null). When a button becomes the current
   control (i.e. gets focus) it sets itself as the temporary default which
   ensures that it has the right appearance and that Enter will be redirected
   to it. When the button loses focus, it unsets the temporary default and so
   the default item will be the permanent default -- that is the default button
   if any had been set or none otherwise, which is just what we want.
 */

// set this button as the (permanently) default one in its panel
wxWindow *wxButton::SetDefault()
{
    // set this one as the default button both for wxWidgets ...
    wxWindow *winOldDefault = wxButtonBase::SetDefault();

    // ... and Windows
    SetDefaultStyle(wxDynamicCast(winOldDefault, wxButton), false);
    SetDefaultStyle(this, true);

    return winOldDefault;
}

// return the top level parent window if it's not being deleted yet, otherwise
// return nullptr
static wxTopLevelWindow *GetTLWParentIfNotBeingDeleted(wxWindow *win)
{
    for ( ;; )
    {
        // IsTopLevel() will return false for a wxTLW being deleted, so we also
        // need the parent test for this case
        wxWindow * const parent = win->GetParent();
        if ( !parent || win->IsTopLevel() )
        {
            if ( win->IsBeingDeleted() )
                return nullptr;

            break;
        }

        win = parent;
    }

    wxASSERT_MSG( win, wxT("button without top level parent?") );

    // Note that this may still return null for a button inside wxPopupWindow.
    return wxDynamicCast(win, wxTopLevelWindow);
}

// set this button as being currently default
void wxButton::SetTmpDefault()
{
    wxTopLevelWindow * const tlw = GetTLWParentIfNotBeingDeleted(this);
    if ( !tlw )
        return;

    wxWindow *winOldDefault = tlw->GetDefaultItem();

    tlw->SetTmpDefaultItem(this);

    // Notice that the order of these statements is important, the old button
    // is not reset if we do it the other way round, probably because of
    // something done by the default DM_SETDEFID handler.
    SetDefaultStyle(this, true);
    if ( winOldDefault != this )
    {
        // But we mustn't reset the default style on this button itself if it
        // had already been the default.
        SetDefaultStyle(wxDynamicCast(winOldDefault, wxButton), false);
    }
}

// unset this button as currently default, it may still stay permanent default
void wxButton::UnsetTmpDefault()
{
    wxTopLevelWindow * const tlw = GetTLWParentIfNotBeingDeleted(this);
    if ( !tlw )
        return;

    tlw->SetTmpDefaultItem(nullptr);

    wxWindow *winOldDefault = tlw->GetDefaultItem();

    // Just as in SetTmpDefault() above, the order is important here.
    SetDefaultStyle(wxDynamicCast(winOldDefault, wxButton), true);
    if ( winOldDefault != this )
    {
        SetDefaultStyle(this, false);
    }
}

/* static */
void
wxButton::SetDefaultStyle(wxButton *btn, bool on)
{
    // we may be called with null pointer -- simpler to do the check here than
    // in the caller which does wxDynamicCast()
    if ( !btn )
        return;

    // we shouldn't set BS_DEFPUSHBUTTON for any button if we don't have
    // focus at all any more
    if ( on && !wxTheApp->IsActive() )
        return;

    // first, let DefDlgProc() know about the new default button
    wxWindow * const tlw = wxGetTopLevelParent(btn);
    wxCHECK_RET( tlw, wxT("button without top level window?") );

    // passing -1 to indicate absence of the default button is not documented
    // as being supported, but we need to pass something to DM_SETDEFID when
    // resetting the default button it in order to prevent DefDlgProc() from
    // restoring BS_DEFPUSHBUTTON on it later, see #19245, and -1 shouldn't
    // conflict with anything, as it can never be a valid ID
    ::SendMessage(GetHwndOf(tlw), DM_SETDEFID, on ? btn->GetId() : -1, 0L);

    // then also change the style as needed
    long style = ::GetWindowLong(GetHwndOf(btn), GWL_STYLE);
    if ( !(style & BS_DEFPUSHBUTTON) == on )
    {
        // don't do it with the owner drawn buttons because it will
        // reset BS_OWNERDRAW style bit too (as BS_OWNERDRAW &
        // BS_DEFPUSHBUTTON != 0)!
        if ( (style & BS_OWNERDRAW) != BS_OWNERDRAW )
        {
            ::SendMessage(GetHwndOf(btn), BM_SETSTYLE,
                          on ? style | BS_DEFPUSHBUTTON
                             : style & ~BS_DEFPUSHBUTTON,
                          1L /* redraw */);
        }
        else // owner drawn
        {
            // redraw the button - it will notice itself that it's
            // [not] the default one [any longer]
            btn->Refresh();
        }
    }
    //else: already has correct style
}

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

bool wxButton::SendClickEvent()
{
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);

    return ProcessCommand(event);
}

void wxButton::Command(wxCommandEvent & event)
{
    ProcessCommand(event);
}

// ----------------------------------------------------------------------------
// event/message handlers
// ----------------------------------------------------------------------------

void wxButton::OnCharHook(wxKeyEvent& event)
{
    // We want to ensure that the button always processes Enter key events
    // itself, even if it's inside some control that normally takes over them
    // (this happens when the button is part of an in-place editor control for
    // example).
    if ( event.GetKeyCode() == WXK_RETURN )
    {
        // We should ensure that subsequent key events are still generated even
        // if we did handle EVT_CHAR_HOOK (normally this would suppress their
        // generation).
        event.DoAllowNextEvent();
    }
    else
    {
        event.Skip();
    }
}

bool wxButton::MSWCommand(WXUINT param, WXWORD WXUNUSED(id))
{
    bool processed = false;
    switch ( param )
    {
        // NOTE: Currently all versions of Windows send two BN_CLICKED messages
        //       for all button types, so we don't catch BN_DOUBLECLICKED
        //       in order to not get 3 EVT_BUTTON events.  If this is a problem
        //       then we need to figure out which version of the comctl32 changed
        //       this behaviour and test for it.

        case 1:                     // message came from an accelerator
        case BN_CLICKED:            // normal buttons send this
            processed = SendClickEvent();
            break;
    }

    return processed;
}

WXLRESULT wxButton::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    // when we receive focus, we want to temporarily become the default button in
    // our parent panel so that pressing "Enter" would activate us -- and when
    // losing it we should restore the previous default button as well
    if ( nMsg == WM_SETFOCUS )
    {
        SetTmpDefault();

        // let the default processing take place too
    }
    else if ( nMsg == WM_KILLFOCUS )
    {
        UnsetTmpDefault();
    }

    // let the base class do all real processing
    return wxAnyButton::MSWWindowProc(nMsg, wParam, lParam);
}

// ----------------------------------------------------------------------------
// authentication needed handling
// ----------------------------------------------------------------------------

bool wxButton::DoGetAuthNeeded() const
{
    return m_authNeeded;
}

void wxButton::DoSetAuthNeeded(bool show)
{
    m_authNeeded = show;
    ::SendMessage(GetHwnd(), BCM_SETSHIELD, 0, show);
    InvalidateBestSize();
}

#endif // wxUSE_BUTTON

