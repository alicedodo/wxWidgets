/////////////////////////////////////////////////////////////////////////////
// Name:        htmlcell.cpp
// Purpose:     wxHtmlCell - basic element of HTML output
// Author:      Vaclav Slavik
// RCS-ID:      $Id$
// Copyright:   (c) 1999 Vaclav Slavik
// Licence:     wxWindows Licence
/////////////////////////////////////////////////////////////////////////////

#ifdef __GNUG__
#pragma implementation "htmlcell.h"
#endif

#include "wx/wxprec.h"

#include "wx/defs.h"

#if wxUSE_HTML && wxUSE_STREAMS

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WXPRECOMP
    #include "wx/brush.h"
    #include "wx/colour.h"
    #include "wx/dc.h"
#endif

#include "wx/html/htmlcell.h"
#include "wx/html/htmlwin.h"
#include "wx/settings.h"
#include <stdlib.h>

//-----------------------------------------------------------------------------
// Helper classes
//-----------------------------------------------------------------------------

void wxHtmlSelection::Set(const wxPoint& fromPos, wxHtmlCell *fromCell,
                          const wxPoint& toPos, wxHtmlCell *toCell)
{
    m_fromCell = fromCell;
    m_toCell = toCell;
    m_fromPos = fromPos;
    m_toPos = toPos;
}

void wxHtmlSelection::Set(wxHtmlCell *fromCell, wxHtmlCell *toCell)
{
    wxPoint p1 = fromCell ? fromCell->GetAbsPos() : wxDefaultPosition;
    wxPoint p2 = toCell ? toCell->GetAbsPos() : wxDefaultPosition;
    if ( toCell )
    {
        p2.x += toCell->GetWidth()-1;
        p2.y += toCell->GetHeight()-1;
    }
    Set(p1, fromCell, p2, toCell);
}

//-----------------------------------------------------------------------------
// wxHtmlCell
//-----------------------------------------------------------------------------

wxHtmlCell::wxHtmlCell() : wxObject()
{
    m_Next = NULL;
    m_Parent = NULL;
    m_Width = m_Height = m_Descent = 0;
    m_CanLiveOnPagebreak = TRUE;
    m_Link = NULL;
}

wxHtmlCell::~wxHtmlCell()
{
    delete m_Link;
}


void wxHtmlCell::OnMouseClick(wxWindow *parent, int x, int y,
                              const wxMouseEvent& event)
{
    wxHtmlLinkInfo *lnk = GetLink(x, y);
    if (lnk != NULL)
    {
        wxHtmlLinkInfo lnk2(*lnk);
        lnk2.SetEvent(&event);
        lnk2.SetHtmlCell(this);

        // note : this cast is legal because parent is *always* wxHtmlWindow
        wxStaticCast(parent, wxHtmlWindow)->OnLinkClicked(lnk2);
    }
}



bool wxHtmlCell::AdjustPagebreak(int *pagebreak, int* WXUNUSED(known_pagebreaks), int WXUNUSED(number_of_pages)) const
{
    if ((!m_CanLiveOnPagebreak) &&
                m_PosY < *pagebreak && m_PosY + m_Height > *pagebreak)
    {
        *pagebreak = m_PosY;
        return TRUE;
    }

    return FALSE;
}



void wxHtmlCell::SetLink(const wxHtmlLinkInfo& link)
{
    if (m_Link) delete m_Link;
    m_Link = NULL;
    if (link.GetHref() != wxEmptyString)
        m_Link = new wxHtmlLinkInfo(link);
}



void wxHtmlCell::Layout(int WXUNUSED(w))
{
    SetPos(0, 0);
}



void wxHtmlCell::GetHorizontalConstraints(int *left, int *right) const
{
    if (left)
        *left = m_PosX;
    if (right)
        *right = m_PosX + m_Width;
}



const wxHtmlCell* wxHtmlCell::Find(int WXUNUSED(condition), const void* WXUNUSED(param)) const
{
    return NULL;
}


wxHtmlCell *wxHtmlCell::FindCellByPos(wxCoord x, wxCoord y,
                                      unsigned flags) const
{
    if ( x >= 0 && x < m_Width && y >= 0 && y < m_Height )
    {
        return wxConstCast(this, wxHtmlCell);
    }
    else
    {
        if ((flags & wxHTML_FIND_NEAREST_AFTER) &&
                (y < 0 || (y == 0 && x <= 0)))
            return wxConstCast(this, wxHtmlCell);
        else if ((flags & wxHTML_FIND_NEAREST_BEFORE) &&
                (y > m_Height-1 || (y == m_Height-1 && x >= m_Width)))
            return wxConstCast(this, wxHtmlCell);
        else
            return NULL;
    }
}


wxPoint wxHtmlCell::GetAbsPos() const
{
    wxPoint p(m_PosX, m_PosY);
    for (wxHtmlCell *parent = m_Parent; parent; parent = parent->m_Parent)
    {
        p.x += parent->m_PosX;
        p.y += parent->m_PosY;
    }
    return p;
}
    
unsigned wxHtmlCell::GetDepth() const
{
    unsigned d = 0;
    for (wxHtmlCell *p = m_Parent; p; p = p->m_Parent)
        d++;
    return d;
}
    
bool wxHtmlCell::IsBefore(wxHtmlCell *cell) const
{
    const wxHtmlCell *c1 = this;
    const wxHtmlCell *c2 = cell;
    unsigned d1 = GetDepth();
    unsigned d2 = cell->GetDepth();

    if ( d1 > d2 )
        for (; d1 != d2; d1-- )
            c1 = c1->m_Parent;
    else if ( d1 < d2 )
        for (; d1 != d2; d2-- )
            c2 = c2->m_Parent;

    if ( cell == this )
        return true;

    while ( c1 && c2 )
    {
        if ( c1->m_Parent == c2->m_Parent )
        {
            while ( c1 )
            {
                if ( c1 == c2 )
                    return true;
                c1 = c1->GetNext();
            }            
            return false;
        }
        else
        {
            c1 = c1->m_Parent;
            c2 = c2->m_Parent;
        }
    }

    wxFAIL_MSG(_T("Cells are in different trees"));
    return false;
}


//-----------------------------------------------------------------------------
// wxHtmlWordCell
//-----------------------------------------------------------------------------

wxHtmlWordCell::wxHtmlWordCell(const wxString& word, wxDC& dc) : wxHtmlCell()
{
    m_Word = word;
    dc.GetTextExtent(m_Word, &m_Width, &m_Height, &m_Descent);
    SetCanLiveOnPagebreak(FALSE);
}


// Splits m_Word into up to three parts according to selection, returns
// substring before, in and after selection and the points (in relative coords)
// where s2 and s3 start:
void wxHtmlWordCell::Split(wxDC& dc, 
                           const wxPoint& selFrom, const wxPoint& selTo,
                           wxString& s1, wxString& s2, wxString& s3,
                           unsigned& pos1, unsigned& pos2)
{
    wxPoint pt1 = (selFrom == wxDefaultPosition) ?
                   wxDefaultPosition : selFrom - GetAbsPos();
    wxPoint pt2 = (selTo == wxDefaultPosition) ?
                   wxPoint(m_Width, -1) : selTo - GetAbsPos();

    wxCoord charW, charH;
    unsigned len = m_Word.length();
    unsigned i = 0;
    pos1 = 0;
    s1 = s2 = s3 = wxEmptyString;

    // before selection:
    while ( pt1.x > 0 && i < len )
    {
        dc.GetTextExtent(m_Word[i], &charW, &charH);
        pt1.x -= charW;
        if ( pt1.x >= 0 )
        {
            pos1 += charW;
            i++;
        }
    }

    // in selection:
    unsigned j = i;
    pos2 = pos1;
        pt2.x -= pos2;
    while ( pt2.x > 0 && j < len )
    {
        dc.GetTextExtent(m_Word[j], &charW, &charH);
        pt2.x -= charW;
        if ( pt2.x >= 0 )
        {
            pos2 += charW;
            j++;
        }
    }
    
    s1 = m_Word.Mid(0, i);
    s2 = m_Word.Mid(i, j-i);
    s3 = m_Word.Mid(j);
#if 0 // FIXME
    printf("  '%s' %i '%s' %i '%s'\n", s1.mb_str(), pos1,
            s2.mb_str(), pos2,s3.mb_str());
#endif
}


static void SwitchSelState(wxDC& dc, wxHtmlRenderingState& state,
                           bool toSelection)
{
    if ( toSelection )
    {
        dc.SetBackgroundMode(wxSOLID);
        dc.SetTextBackground(
                wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
        dc.SetTextForeground(
                wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
    }
    else
    {
        dc.SetBackgroundMode(wxTRANSPARENT);
        dc.SetTextForeground(state.GetFgColour());
        dc.SetTextBackground(state.GetBgColour());
    }
}


void wxHtmlWordCell::Draw(wxDC& dc, int x, int y,
                          int WXUNUSED(view_y1), int WXUNUSED(view_y2),
                          wxHtmlRenderingState& state)
{
#if 0 // useful for debugging
    dc.DrawRectangle(x+m_PosX,y+m_PosY,m_Width,m_Height);
#endif

    if ( state.GetSelectionState() == wxHTML_SEL_CHANGING )
    {
        // Selection changing, we must draw the word piecewise:
    
        unsigned ofs1, ofs2;
        wxString textBefore, textInside, textAfter;
        wxHtmlSelection *s = state.GetSelection();
        
        Split(dc, 
              this == s->GetFromCell() ? s->GetFromPos() : wxDefaultPosition,
              this == s->GetToCell() ? s->GetToPos() : wxDefaultPosition,
              textBefore, textInside, textAfter, ofs1, ofs2);
        dc.DrawText(textBefore, x + m_PosX, y + m_PosY);
        int w1,w2,w3,h123;
        dc.GetTextExtent(textBefore, &w1,&h123);
        dc.GetTextExtent(textInside, &w2,&h123);
        dc.GetTextExtent(textAfter, &w3,&h123);
        if ( !textInside.empty() )
        {
            SwitchSelState(dc, state, true);
            dc.DrawText(textInside, x + m_PosX + ofs1, y + m_PosY);
        }
        if ( !textAfter.empty() )
        {
            SwitchSelState(dc, state, false);
            dc.DrawText(textAfter, x + m_PosX + ofs2, y + m_PosY);
        }
    }
    else
    {
        // Not changing selection state, draw the word in single mode:

        if ( state.GetSelectionState() != wxHTML_SEL_OUT &&
             dc.GetBackgroundMode() != wxSOLID )
        {
            SwitchSelState(dc, state, true);
        }
        else if ( state.GetSelectionState() == wxHTML_SEL_OUT &&
                  dc.GetBackgroundMode() == wxSOLID )
        {
            SwitchSelState(dc, state, false);
        }
        dc.DrawText(m_Word, x + m_PosX, y + m_PosY);
    }
}
    

wxString wxHtmlWordCell::ConvertToText(wxHtmlSelection *sel) const
{
    // FIXME - use selection
    return m_Word;
}



//-----------------------------------------------------------------------------
// wxHtmlContainerCell
//-----------------------------------------------------------------------------


wxHtmlContainerCell::wxHtmlContainerCell(wxHtmlContainerCell *parent) : wxHtmlCell()
{
    m_Cells = m_LastCell = NULL;
    m_Parent = parent;
    if (m_Parent) m_Parent->InsertCell(this);
    m_AlignHor = wxHTML_ALIGN_LEFT;
    m_AlignVer = wxHTML_ALIGN_BOTTOM;
    m_IndentLeft = m_IndentRight = m_IndentTop = m_IndentBottom = 0;
    m_WidthFloat = 100; m_WidthFloatUnits = wxHTML_UNITS_PERCENT;
    m_UseBkColour = FALSE;
    m_UseBorder = FALSE;
    m_MinHeight = 0;
    m_MinHeightAlign = wxHTML_ALIGN_TOP;
    m_LastLayout = -1;
}

wxHtmlContainerCell::~wxHtmlContainerCell()
{
    wxHtmlCell *cell = m_Cells;
    while ( cell )
    {
        wxHtmlCell *cellNext = cell->GetNext();
        delete cell;
        cell = cellNext;
    }
}



void wxHtmlContainerCell::SetIndent(int i, int what, int units)
{
    int val = (units == wxHTML_UNITS_PIXELS) ? i : -i;
    if (what & wxHTML_INDENT_LEFT) m_IndentLeft = val;
    if (what & wxHTML_INDENT_RIGHT) m_IndentRight = val;
    if (what & wxHTML_INDENT_TOP) m_IndentTop = val;
    if (what & wxHTML_INDENT_BOTTOM) m_IndentBottom = val;
    m_LastLayout = -1;
}



int wxHtmlContainerCell::GetIndent(int ind) const
{
    if (ind & wxHTML_INDENT_LEFT) return m_IndentLeft;
    else if (ind & wxHTML_INDENT_RIGHT) return m_IndentRight;
    else if (ind & wxHTML_INDENT_TOP) return m_IndentTop;
    else if (ind & wxHTML_INDENT_BOTTOM) return m_IndentBottom;
    else return -1; /* BUG! Should not be called... */
}




int wxHtmlContainerCell::GetIndentUnits(int ind) const
{
    bool p = FALSE;
    if (ind & wxHTML_INDENT_LEFT) p = m_IndentLeft < 0;
    else if (ind & wxHTML_INDENT_RIGHT) p = m_IndentRight < 0;
    else if (ind & wxHTML_INDENT_TOP) p = m_IndentTop < 0;
    else if (ind & wxHTML_INDENT_BOTTOM) p = m_IndentBottom < 0;
    if (p) return wxHTML_UNITS_PERCENT;
    else return wxHTML_UNITS_PIXELS;
}



bool wxHtmlContainerCell::AdjustPagebreak(int *pagebreak, int* known_pagebreaks, int number_of_pages) const
{
    if (!m_CanLiveOnPagebreak)
        return wxHtmlCell::AdjustPagebreak(pagebreak, known_pagebreaks, number_of_pages);

    else
    {
        wxHtmlCell *c = GetFirstChild();
        bool rt = FALSE;
        int pbrk = *pagebreak - m_PosY;

        while (c)
        {
            if (c->AdjustPagebreak(&pbrk, known_pagebreaks, number_of_pages))
                rt = TRUE;
            c = c->GetNext();
        }
        if (rt)
            *pagebreak = pbrk + m_PosY;
        return rt;
    }
}



void wxHtmlContainerCell::Layout(int w)
{
    wxHtmlCell::Layout(w);

    if (m_LastLayout == w) return;

    // VS: Any attempt to layout with negative or zero width leads to hell,
    // but we can't ignore such attempts completely, since it sometimes
    // happen (e.g. when trying how small a table can be). The best thing we
    // can do is to set the width of child cells to zero
    if (w < 1)
    {
       m_Width = 0;
       for (wxHtmlCell *cell = m_Cells; cell; cell = cell->GetNext())
            cell->Layout(0);
            // this does two things: it recursively calls this code on all
            // child contrainers and resets children's position to (0,0)
       return;
    }

    wxHtmlCell *cell = m_Cells, *line = m_Cells;
    long xpos = 0, ypos = m_IndentTop;
    int xdelta = 0, ybasicpos = 0, ydiff;
    int s_width, s_indent;
    int ysizeup = 0, ysizedown = 0;
    int MaxLineWidth = 0;
    int xcnt = 0;


    /*

    WIDTH ADJUSTING :

    */

    if (m_WidthFloatUnits == wxHTML_UNITS_PERCENT)
    {
        if (m_WidthFloat < 0) m_Width = (100 + m_WidthFloat) * w / 100;
        else m_Width = m_WidthFloat * w / 100;
    }
    else
    {
        if (m_WidthFloat < 0) m_Width = w + m_WidthFloat;
        else m_Width = m_WidthFloat;
    }

    if (m_Cells)
    {
        int l = (m_IndentLeft < 0) ? (-m_IndentLeft * m_Width / 100) : m_IndentLeft;
        int r = (m_IndentRight < 0) ? (-m_IndentRight * m_Width / 100) : m_IndentRight;
        for (wxHtmlCell *cell = m_Cells; cell; cell = cell->GetNext())
            cell->Layout(m_Width - (l + r));
    }

    /*

    LAYOUTING :

    */

    // adjust indentation:
    s_indent = (m_IndentLeft < 0) ? (-m_IndentLeft * m_Width / 100) : m_IndentLeft;
    s_width = m_Width - s_indent - ((m_IndentRight < 0) ? (-m_IndentRight * m_Width / 100) : m_IndentRight);

    // my own layouting:
    while (cell != NULL)
    {
        switch (m_AlignVer)
        {
            case wxHTML_ALIGN_TOP :      ybasicpos = 0; break;
            case wxHTML_ALIGN_BOTTOM :   ybasicpos = - cell->GetHeight(); break;
            case wxHTML_ALIGN_CENTER :   ybasicpos = - cell->GetHeight() / 2; break;
        }
        ydiff = cell->GetHeight() + ybasicpos;

        if (cell->GetDescent() + ydiff > ysizedown) ysizedown = cell->GetDescent() + ydiff;
        if (ybasicpos + cell->GetDescent() < -ysizeup) ysizeup = - (ybasicpos + cell->GetDescent());

        cell->SetPos(xpos, ybasicpos + cell->GetDescent());
        xpos += cell->GetWidth();
        cell = cell->GetNext();
        xcnt++;

        // force new line if occured:
        if ((cell == NULL) || (xpos + cell->GetWidth() > s_width))
        {
            if (xpos > MaxLineWidth) MaxLineWidth = xpos;
            if (ysizeup < 0) ysizeup = 0;
            if (ysizedown < 0) ysizedown = 0;
            switch (m_AlignHor) {
                case wxHTML_ALIGN_LEFT :
                case wxHTML_ALIGN_JUSTIFY :
                         xdelta = 0;
                         break;
                case wxHTML_ALIGN_RIGHT :
                         xdelta = 0 + (s_width - xpos);
                         break;
                case wxHTML_ALIGN_CENTER :
                         xdelta = 0 + (s_width - xpos) / 2;
                         break;
            }
            if (xdelta < 0) xdelta = 0;
            xdelta += s_indent;

            ypos += ysizeup;

            if (m_AlignHor != wxHTML_ALIGN_JUSTIFY || cell == NULL)
                while (line != cell)
                {
                    line->SetPos(line->GetPosX() + xdelta,
                                   ypos + line->GetPosY());
                    line = line->GetNext();
                }
            else
            {
                int counter = 0;
                int step = (s_width - xpos);
                if (step < 0) step = 0;
                xcnt--;
                if (xcnt > 0) while (line != cell)
                {
                    line->SetPos(line->GetPosX() + s_indent +
                                   (counter++ * step / xcnt),
                                   ypos + line->GetPosY());
                    line = line->GetNext();
                }
                xcnt++;
            }

            ypos += ysizedown;
            xpos = xcnt = 0;
            ysizeup = ysizedown = 0;
            line = cell;
        }
    }

    // setup height & width, depending on container layout:
    m_Height = ypos + (ysizedown + ysizeup) + m_IndentBottom;

    if (m_Height < m_MinHeight)
    {
        if (m_MinHeightAlign != wxHTML_ALIGN_TOP)
        {
            int diff = m_MinHeight - m_Height;
            if (m_MinHeightAlign == wxHTML_ALIGN_CENTER) diff /= 2;
            cell = m_Cells;
            while (cell)
            {
                cell->SetPos(cell->GetPosX(), cell->GetPosY() + diff);
                cell = cell->GetNext();
            }
        }
        m_Height = m_MinHeight;
    }

    MaxLineWidth += s_indent + ((m_IndentRight < 0) ? (-m_IndentRight * m_Width / 100) : m_IndentRight);
    if (m_Width < MaxLineWidth) m_Width = MaxLineWidth;

    m_LastLayout = w;
}

void wxHtmlContainerCell::UpdateRenderingStatePre(wxHtmlRenderingState& state,
                                                  wxHtmlCell *cell) const
{
    wxHtmlSelection *s = state.GetSelection();
    if (!s) return;
    if (s->GetFromCell() == cell || s->GetToCell() == cell)
    {
        state.SetSelectionState(wxHTML_SEL_CHANGING);
    }
}

void wxHtmlContainerCell::UpdateRenderingStatePost(wxHtmlRenderingState& state,
                                                   wxHtmlCell *cell) const
{
    wxHtmlSelection *s = state.GetSelection();
    if (!s) return;
    if (s->GetToCell() == cell)
        state.SetSelectionState(wxHTML_SEL_OUT);
    else if (s->GetFromCell() == cell)
        state.SetSelectionState(wxHTML_SEL_IN);
}

#define mMin(a, b) (((a) < (b)) ? (a) : (b))
#define mMax(a, b) (((a) < (b)) ? (b) : (a))

void wxHtmlContainerCell::Draw(wxDC& dc, int x, int y, int view_y1, int view_y2,
                               wxHtmlRenderingState& state)
{
    // container visible, draw it:
    if ((y + m_PosY <= view_y2) && (y + m_PosY + m_Height > view_y1))
    {
        if (m_UseBkColour)
        {
            wxBrush myb = wxBrush(m_BkColour, wxSOLID);

            int real_y1 = mMax(y + m_PosY, view_y1);
            int real_y2 = mMin(y + m_PosY + m_Height - 1, view_y2);

            dc.SetBrush(myb);
            dc.SetPen(*wxTRANSPARENT_PEN);
            dc.DrawRectangle(x + m_PosX, real_y1, m_Width, real_y2 - real_y1 + 1);
        }

        if (m_UseBorder)
        {
            wxPen mypen1(m_BorderColour1, 1, wxSOLID);
            wxPen mypen2(m_BorderColour2, 1, wxSOLID);

            dc.SetPen(mypen1);
            dc.DrawLine(x + m_PosX, y + m_PosY, x + m_PosX, y + m_PosY + m_Height - 1);
            dc.DrawLine(x + m_PosX, y + m_PosY, x + m_PosX + m_Width, y + m_PosY);
            dc.SetPen(mypen2);
            dc.DrawLine(x + m_PosX + m_Width - 1, y + m_PosY, x + m_PosX +  m_Width - 1, y + m_PosY + m_Height - 1);
            dc.DrawLine(x + m_PosX, y + m_PosY + m_Height - 1, x + m_PosX + m_Width, y + m_PosY + m_Height - 1);
        }

        if (m_Cells)
        {
            // draw container's contents:
            for (wxHtmlCell *cell = m_Cells; cell; cell = cell->GetNext())
            {
                UpdateRenderingStatePre(state, cell);
                cell->Draw(dc,
                           x + m_PosX, y + m_PosY, view_y1, view_y2,
                           state);
                UpdateRenderingStatePost(state, cell);
            }
        }
    }
    // container invisible, just proceed font+color changing:
    else
    {
        DrawInvisible(dc, x, y, state);
    }
}



void wxHtmlContainerCell::DrawInvisible(wxDC& dc, int x, int y,
                                        wxHtmlRenderingState& state)
{
    if (m_Cells)
    {
        for (wxHtmlCell *cell = m_Cells; cell; cell = cell->GetNext())
        {
            UpdateRenderingStatePre(state, cell);
            cell->DrawInvisible(dc, x + m_PosX, y + m_PosY, state);
            UpdateRenderingStatePost(state, cell);
        }
    }
}


wxColour wxHtmlContainerCell::GetBackgroundColour()
{
    if (m_UseBkColour)
        return m_BkColour;
    else
        return wxNullColour;
}



wxHtmlLinkInfo *wxHtmlContainerCell::GetLink(int x, int y) const
{
    wxHtmlCell *cell = FindCellByPos(x, y);

    // VZ: I don't know if we should pass absolute or relative coords to
    //     wxHtmlCell::GetLink()? As the base class version just ignores them
    //     anyhow, it hardly matters right now but should still be clarified
    return cell ? cell->GetLink(x, y) : NULL;
}



void wxHtmlContainerCell::InsertCell(wxHtmlCell *f)
{
    if (!m_Cells) m_Cells = m_LastCell = f;
    else
    {
        m_LastCell->SetNext(f);
        m_LastCell = f;
        if (m_LastCell) while (m_LastCell->GetNext()) m_LastCell = m_LastCell->GetNext();
    }
    f->SetParent(this);
    m_LastLayout = -1;
}



void wxHtmlContainerCell::SetAlign(const wxHtmlTag& tag)
{
    if (tag.HasParam(wxT("ALIGN")))
    {
        wxString alg = tag.GetParam(wxT("ALIGN"));
        alg.MakeUpper();
        if (alg == wxT("CENTER"))
            SetAlignHor(wxHTML_ALIGN_CENTER);
        else if (alg == wxT("LEFT"))
            SetAlignHor(wxHTML_ALIGN_LEFT);
        else if (alg == wxT("JUSTIFY"))
            SetAlignHor(wxHTML_ALIGN_JUSTIFY);
        else if (alg == wxT("RIGHT"))
            SetAlignHor(wxHTML_ALIGN_RIGHT);
        m_LastLayout = -1;
    }
}



void wxHtmlContainerCell::SetWidthFloat(const wxHtmlTag& tag, double pixel_scale)
{
    if (tag.HasParam(wxT("WIDTH")))
    {
        int wdi;
        wxString wd = tag.GetParam(wxT("WIDTH"));

        if (wd[wd.Length()-1] == wxT('%'))
        {
            wxSscanf(wd.c_str(), wxT("%i%%"), &wdi);
            SetWidthFloat(wdi, wxHTML_UNITS_PERCENT);
        }
        else
        {
            wxSscanf(wd.c_str(), wxT("%i"), &wdi);
            SetWidthFloat((int)(pixel_scale * (double)wdi), wxHTML_UNITS_PIXELS);
        }
        m_LastLayout = -1;
    }
}



const wxHtmlCell* wxHtmlContainerCell::Find(int condition, const void* param) const
{
    if (m_Cells)
    {
        const wxHtmlCell *r = NULL;

        for (wxHtmlCell *cell = m_Cells; cell; cell = cell->GetNext())
        {
            r = cell->Find(condition, param);
            if (r) return r;
        }
    }
    return NULL;
}


wxHtmlCell *wxHtmlContainerCell::FindCellByPos(wxCoord x, wxCoord y,
                                               unsigned flags) const
{
    if ( flags & wxHTML_FIND_EXACT )
    {
        for ( const wxHtmlCell *cell = m_Cells; cell; cell = cell->GetNext() )
        {
            int cx = cell->GetPosX(),
                cy = cell->GetPosY();

            if ( (cx <= x) && (cx + cell->GetWidth() > x) &&
                 (cy <= y) && (cy + cell->GetHeight() > y) )
            {
                return cell->FindCellByPos(x - cx, y - cy, flags);
            }
        }
    }
    else if ( flags & wxHTML_FIND_NEAREST_AFTER )
    {
        wxHtmlCell *c;
        int y2;
        for ( const wxHtmlCell *cell = m_Cells; cell; cell = cell->GetNext() )
        {
            y2 = cell->GetPosY() + cell->GetHeight() - 1;
            if (y2 < y || (y2 == y && cell->GetPosX()+cell->GetWidth()-1 < x))
                continue;
            c = cell->FindCellByPos(x - cell->GetPosX(), y - cell->GetPosY(),
                                    flags);
            if (c) return c;
        }
    }
    else if ( flags & wxHTML_FIND_NEAREST_BEFORE )
    {
        wxHtmlCell *c2, *c = NULL;
        for ( const wxHtmlCell *cell = m_Cells; cell; cell = cell->GetNext() )
        {
            if (cell->GetPosY() > y || 
                    (cell->GetPosY() == y && cell->GetPosX() > x))
                break;
            c2 = cell->FindCellByPos(x - cell->GetPosX(), y - cell->GetPosY(),
                                     flags);
            if (c2)
                c = c2;
        }
        if (c) return c;
    }

    return NULL;
}


void wxHtmlContainerCell::OnMouseClick(wxWindow *parent, int x, int y, const wxMouseEvent& event)
{
    wxHtmlCell *cell = FindCellByPos(x, y);
    if ( cell )
        cell->OnMouseClick(parent, x, y, event);
}



void wxHtmlContainerCell::GetHorizontalConstraints(int *left, int *right) const
{
    int cleft = m_PosX + m_Width, cright = m_PosX; // worst case
    int l, r;

    for (wxHtmlCell *cell = m_Cells; cell; cell = cell->GetNext())
    {
        cell->GetHorizontalConstraints(&l, &r);
        if (l < cleft)
            cleft = l;
        if (r > cright)
            cright = r;
    }

    cleft -= (m_IndentLeft < 0) ? (-m_IndentLeft * m_Width / 100) : m_IndentLeft;
    cright += (m_IndentRight < 0) ? (-m_IndentRight * m_Width / 100) : m_IndentRight;

    if (left)
        *left = cleft;
    if (right)
        *right = cright;
}

    
wxHtmlCell *wxHtmlContainerCell::GetFirstTerminal() const
{
    if ( m_Cells )
    {
        wxHtmlCell *c2;
        for (wxHtmlCell *c = m_Cells; c; c = c->GetNext())
        {
            c2 = c->GetFirstTerminal();
            if ( c2 )
                return c2;
        }
    }
    return NULL;
}

wxHtmlCell *wxHtmlContainerCell::GetLastTerminal() const
{
    if ( m_Cells )
    {
        // most common case first:
        wxHtmlCell *c = m_LastCell->GetLastTerminal();
        if ( c )
            return c;

        wxHtmlCell *c2 = NULL;
        for (c = m_Cells; c; c = c->GetNext())
            c2 = c->GetLastTerminal();
        return c2;
    }
    else
        return NULL;
}




// --------------------------------------------------------------------------
// wxHtmlColourCell
// --------------------------------------------------------------------------

void wxHtmlColourCell::Draw(wxDC& dc,
                            int x, int y,
                            int WXUNUSED(view_y1), int WXUNUSED(view_y2),
                            wxHtmlRenderingState& state)
{
    DrawInvisible(dc, x, y, state);
}

void wxHtmlColourCell::DrawInvisible(wxDC& dc,
                                     int WXUNUSED(x), int WXUNUSED(y),
                                     wxHtmlRenderingState& state)
{
    if (m_Flags & wxHTML_CLR_FOREGROUND)
    {
        state.SetFgColour(m_Colour);
        if (state.GetSelectionState() != wxHTML_SEL_IN)
            dc.SetTextForeground(m_Colour);        
    }
    if (m_Flags & wxHTML_CLR_BACKGROUND)
    {
        state.SetBgColour(m_Colour);
        if (state.GetSelectionState() != wxHTML_SEL_IN)
            dc.SetTextBackground(m_Colour);
        dc.SetBackground(wxBrush(m_Colour, wxSOLID));
    }
}




// ---------------------------------------------------------------------------
// wxHtmlFontCell
// ---------------------------------------------------------------------------

void wxHtmlFontCell::Draw(wxDC& dc,
                          int WXUNUSED(x), int WXUNUSED(y),
                          int WXUNUSED(view_y1), int WXUNUSED(view_y2),
                          wxHtmlRenderingState& WXUNUSED(state))
{
    dc.SetFont(m_Font);
}

void wxHtmlFontCell::DrawInvisible(wxDC& dc, int WXUNUSED(x), int WXUNUSED(y),
                                   wxHtmlRenderingState& WXUNUSED(state))
{
    dc.SetFont(m_Font);
}








// ---------------------------------------------------------------------------
// wxHtmlWidgetCell
// ---------------------------------------------------------------------------

wxHtmlWidgetCell::wxHtmlWidgetCell(wxWindow *wnd, int w)
{
    int sx, sy;
    m_Wnd = wnd;
    m_Wnd->GetSize(&sx, &sy);
    m_Width = sx, m_Height = sy;
    m_WidthFloat = w;
}


void wxHtmlWidgetCell::Draw(wxDC& WXUNUSED(dc),
                            int WXUNUSED(x), int WXUNUSED(y),
                            int WXUNUSED(view_y1), int WXUNUSED(view_y2),
                            wxHtmlRenderingState& WXUNUSED(state))
{
    int absx = 0, absy = 0, stx, sty;
    wxHtmlCell *c = this;

    while (c)
    {
        absx += c->GetPosX();
        absy += c->GetPosY();
        c = c->GetParent();
    }

    ((wxScrolledWindow*)(m_Wnd->GetParent()))->GetViewStart(&stx, &sty);
    m_Wnd->SetSize(absx - wxHTML_SCROLL_STEP * stx, absy  - wxHTML_SCROLL_STEP * sty, m_Width, m_Height);
}



void wxHtmlWidgetCell::DrawInvisible(wxDC& WXUNUSED(dc),
                                     int WXUNUSED(x), int WXUNUSED(y),
                                     wxHtmlRenderingState& WXUNUSED(state))
{
    int absx = 0, absy = 0, stx, sty;
    wxHtmlCell *c = this;

    while (c)
    {
        absx += c->GetPosX();
        absy += c->GetPosY();
        c = c->GetParent();
    }

    ((wxScrolledWindow*)(m_Wnd->GetParent()))->GetViewStart(&stx, &sty);
    m_Wnd->SetSize(absx - wxHTML_SCROLL_STEP * stx, absy  - wxHTML_SCROLL_STEP * sty, m_Width, m_Height);
}



void wxHtmlWidgetCell::Layout(int w)
{
    if (m_WidthFloat != 0)
    {
        m_Width = (w * m_WidthFloat) / 100;
        m_Wnd->SetSize(m_Width, m_Height);
    }

    wxHtmlCell::Layout(w);
}



// ----------------------------------------------------------------------------
// wxHtmlTerminalCellsInterator
// ----------------------------------------------------------------------------

const wxHtmlCell* wxHtmlTerminalCellsInterator::operator++()
{
    if ( !m_pos )
        return NULL;

    do
    {
        if ( m_pos == m_to )
        {
            m_pos = NULL;
            return NULL;
        }

        if ( m_pos->GetNext() )
            m_pos = m_pos->GetNext();
        else
        {
            // we must go up the hierarchy until we reach container where this
            // is not the last child, and then go down to first terminal cell:
            while ( m_pos->GetNext() == NULL )
            {
                m_pos = m_pos->GetParent();
                if ( !m_pos )
                    return NULL;
            }
            m_pos = m_pos->GetNext();
        }
        while ( m_pos->GetFirstChild() != NULL )
            m_pos = m_pos->GetFirstChild();
    } while ( !m_pos->IsTerminalCell() );
    
    return m_pos;
}

#endif
