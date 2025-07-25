/////////////////////////////////////////////////////////////////////////////
// Name:        src/msw/hyperlink.cpp
// Purpose:     Hyperlink control
// Author:      Rickard Westerlund
// Created:     2010-08-03
// Copyright:   (c) 2010 wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_HYPERLINKCTRL

#include "wx/hyperlink.h"

#include "wx/versioninfo.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/settings.h"
    #include "wx/msw/wrapcctl.h" // include <commctrl.h> "properly"
    #include "wx/msw/private.h"
    #include "wx/msw/missing.h"
#endif

// ----------------------------------------------------------------------------
// Definitions
// ----------------------------------------------------------------------------

#ifndef LM_GETIDEALSIZE
    #define LM_GETIDEALSIZE (WM_USER + 0x301)
#endif

#ifndef LWS_RIGHT
    #define LWS_RIGHT 0x0020
#endif

#ifndef WC_LINK
    #define WC_LINK L"SysLink"
#endif

// ----------------------------------------------------------------------------
// Helper functions
// ----------------------------------------------------------------------------

namespace
{
    wxString GetLabelForSysLink(const wxString& text, const wxString& url)
    {
        // Any "&"s in the text should appear on the screen and not be (mis)
        // interpreted as mnemonics.
        return wxString::Format
               (
                "<A HREF=\"%s\">%s</A>",
                url,
                wxControl::EscapeMnemonics(text.empty() ? url : text)
               );
    }
}

// ----------------------------------------------------------------------------
// wxHyperlinkCtrl
// ----------------------------------------------------------------------------

bool wxHyperlinkCtrl::Create(wxWindow *parent,
                             wxWindowID id,
                             const wxString& label, const wxString& url,
                             const wxPoint& pos,
                             const wxSize& size,
                             long style,
                             const wxString& name)
{
    if ( !CreateControl(parent, id, pos, size, style,
                        wxDefaultValidator, name) )
    {
        return false;
    }

    // Don't call our own version of SetURL() which would try to update the
    // label of the not yet created window which wouldn't do anything and is
    // unnecessary anyhow as we're going to set the label when creating it.
    wxGenericHyperlinkCtrl::SetURL( url );

    if ( !MSWCreateControl(WC_LINK, GetLabelForSysLink(label, url), pos, size) )
    {
        return false;
    }

    // Make sure our GetLabel() returns the label that was specified and not
    // the HTML fragment used as the label by the native control.
    m_labelOrig = label;

    if ( wxSystemSettings::GetAppearance().IsDark() )
    {
        // Override the colour used by default by the native control with the
        // colour appropriate for the dark mode, as the default one doesn't
        // have enough contrast in it.
        SetNormalColour(GetClassDefaultAttributes().colFg);
    }

    ConnectMenuHandlers();

    return true;
}

wxHyperlinkCtrl::~wxHyperlinkCtrl()
{
    if ( m_hWnd )
    {
        // Due to https://bugs.winehq.org/show_bug.cgi?id=54066 we have to
        // reset the font before the native control destroys it in Wine < 10.4.
        wxVersionInfo wineVer;
        if ( wxIsRunningUnderWine(&wineVer) && !wineVer.AtLeast(10, 4) )
            ::SendMessage(m_hWnd, WM_SETFONT, 0, FALSE);
    }
}

WXDWORD wxHyperlinkCtrl::MSWGetStyle(long style, WXDWORD *exstyle) const
{
    WXDWORD msStyle = wxControl::MSWGetStyle( style, exstyle );

    if ( style & wxHL_ALIGN_RIGHT )
        msStyle |= LWS_RIGHT;

    return msStyle;
}

void wxHyperlinkCtrl::SetURL(const wxString &url)
{
    if ( GetURL() != url )
        SetVisited( false );
    wxGenericHyperlinkCtrl::SetURL( url );
    SetLabel(m_labelOrig);
}

void wxHyperlinkCtrl::SetLabel(const wxString &label)
{
    m_labelOrig = label;
    wxWindow::SetLabel( GetLabelForSysLink(label, GetURL()) );
    InvalidateBestSize();
}

bool wxHyperlinkCtrl::Enable(bool enable)
{
    if ( !wxGenericHyperlinkCtrl::Enable(enable) )
        return false;

    wxColour colour;
    if ( enable )
    {
        colour = m_savedEnabledColour;
    }
    else
    {
        m_savedEnabledColour = GetNormalColour();
        colour = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    }

    SetForegroundColour(colour);

    return true;
}

bool wxHyperlinkCtrl::MSWAreCustomColoursEnabled() const
{
    LITEM litem = { };
    litem.mask = LIF_ITEMINDEX | LIF_STATE;
    litem.stateMask = LIS_DEFAULTCOLORS;
    if ( !::SendMessage(GetHwnd(), LM_GETITEM, 0, (LPARAM)&litem) )
    {
        wxLogDebug("LM_GETITEM(LIS_DEFAULTCOLORS) unexpectedly failed");
        return false;
    }

    return (litem.state & LIS_DEFAULTCOLORS) != 0;
}

void wxHyperlinkCtrl::MSWEnableCustomColours()
{
    // By default, the native control ignores the colours we set for it, so we
    // need to explicitly enable this for them to be used.
    if ( !MSWAreCustomColoursEnabled() )
    {
        LITEM litem = { };
        litem.mask = LIF_ITEMINDEX | LIF_STATE;
        litem.state =
        litem.stateMask = LIS_DEFAULTCOLORS;
        if ( !::SendMessage(GetHwnd(), LM_SETITEM, 0, (LPARAM)&litem) )
            wxLogDebug("LM_SETITEM(LIS_DEFAULTCOLORS) unexpectedly failed");
    }
}

wxColour wxHyperlinkCtrl::GetHoverColour() const
{
    // Native control doesn't use special colour on hover.
    return GetNormalColour();
}

bool wxHyperlinkCtrl::SetForegroundColour(const wxColour& colour)
{
    if ( !wxGenericHyperlinkCtrl::SetForegroundColour(colour) )
        return false;

    SetNormalColour(colour);

    return true;
}

wxColour wxHyperlinkCtrl::GetNormalColour() const
{
    if ( MSWAreCustomColoursEnabled() )
        return wxGenericHyperlinkCtrl::GetNormalColour();

    return GetClassDefaultAttributes().colFg;
}

void wxHyperlinkCtrl::SetNormalColour(const wxColour &colour)
{
    MSWEnableCustomColours();

    wxGenericHyperlinkCtrl::SetNormalColour(colour);
}

wxColour wxHyperlinkCtrl::GetVisitedColour() const
{
    if ( MSWAreCustomColoursEnabled() )
        return wxGenericHyperlinkCtrl::GetVisitedColour();

    // Native control doesn't show visited links differently.
    return GetNormalColour();
}

void wxHyperlinkCtrl::SetVisitedColour(const wxColour &colour)
{
    MSWEnableCustomColours();

    wxGenericHyperlinkCtrl::SetVisitedColour(colour);
}

wxVisualAttributes wxHyperlinkCtrl::GetDefaultAttributes() const
{
    return GetClassDefaultAttributes(GetWindowVariant());
}

/* static */
wxVisualAttributes
wxHyperlinkCtrl::GetClassDefaultAttributes(wxWindowVariant variant)
{
    auto attrs = wxGenericHyperlinkCtrl::GetClassDefaultAttributes(variant);

    if ( !wxSystemSettings::GetAppearance().IsDark() )
        attrs.colFg = wxSystemSettings::GetColour(wxSYS_COLOUR_HOTLIGHT);

    return attrs;
}

wxSize wxHyperlinkCtrl::DoGetBestClientSize() const
{
    // Windows allows to pass 0 as maximum width here, but Wine interprets 0 as
    // meaning "minimum possible width", so use something that works for both.
    const WPARAM UNLIMITED_WIDTH = 10000;

    SIZE idealSize;
    ::SendMessage(m_hWnd, LM_GETIDEALSIZE, UNLIMITED_WIDTH, (LPARAM)&idealSize);

    return wxSize(idealSize.cx, idealSize.cy);
}

bool wxHyperlinkCtrl::MSWOnNotify(int idCtrl, WXLPARAM lParam, WXLPARAM *result)
{
    switch ( ((LPNMHDR) lParam)->code )
    {
        case NM_CLICK:
        case NM_RETURN:
            SetVisited();
            SendEvent();

            // SendEvent() launches the browser by default, so we consider
            // that the event was processed in any case, either by user
            // code or by wx itself, hence we always return true to
            // indicate that the default processing shouldn't take place.
            return true;
    }

   return wxGenericHyperlinkCtrl::MSWOnNotify(idCtrl, lParam, result);
}

#endif // wxUSE_HYPERLINKCTRL
