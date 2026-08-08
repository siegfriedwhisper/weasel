#include "stdafx.h"
#include "GridLayout.h"

using namespace weasel;

void GridLayout::DoLayout(CDCHandle dc, PDWR pDWR) {
  CSize size;
  int width = offsetX + real_margin_x, height = offsetY + real_margin_y;
  int w = offsetX + real_margin_x;

  /* calc mark_text sizes */
  if ((_style.hilited_mark_color & 0xff000000)) {
    CSize sg;
    if (candidates_count) {
      if (_style.mark_text.empty())
        GetTextSizeDW(L"|", 1, pDWR->pTextFormat, pDWR, &sg);
      else
        GetTextSizeDW(_style.mark_text, _style.mark_text.length(),
                      pDWR->pTextFormat, pDWR, &sg);
    }

    mark_width = sg.cx;
    mark_height = sg.cy;
    if (_style.mark_text.empty()) {
      mark_width = mark_height / 7;
      if (_style.linespacing && _style.baseline)
        mark_width =
            (int)((float)mark_width / ((float)_style.linespacing / 100.0f));
      mark_width = max(mark_width, 6);
    }
    mark_gap = (_style.mark_text.empty()) ? mark_width
                                          : mark_width + _style.hilite_spacing;
  }
  int base_offset = ((_style.hilited_mark_color & 0xff000000)) ? mark_gap : 0;

  // calc page indicator
  CSize pgszl, pgszr;
  if (!IsInlinePreedit()) {
    GetTextSizeDW(pre, pre.length(), pDWR->pPreeditTextFormat, pDWR, &pgszl);
    GetTextSizeDW(next, next.length(), pDWR->pPreeditTextFormat, pDWR, &pgszr);
  }
  bool page_en = (_style.prevpage_color & 0xff000000) &&
                 (_style.nextpage_color & 0xff000000);
  int pgw = page_en ? (pgszl.cx + pgszr.cx + _style.hilite_spacing +
                       _style.hilite_padding_x * 2)
                    : 0;
  int pgh = page_en ? max(pgszl.cy, pgszr.cy) : 0;

  /* Preedit */
  if (!IsInlinePreedit() && !_context.preedit.str.empty()) {
    size = GetPreeditSize(dc, _context.preedit, pDWR->pPreeditTextFormat, pDWR);
    int szx = pgw, szy = max(size.cy, pgh);
    // icon size higher then preedit text
    int yoffset = (STATUS_ICON_SIZE >= szy && ShouldDisplayStatusIcon())
                      ? (STATUS_ICON_SIZE - szy) / 2
                      : 0;
    _preeditRect.SetRect(w, height + yoffset, w + size.cx,
                         height + yoffset + size.cy);
    height += szy + 2 * yoffset + _style.spacing;
    width = max(width, real_margin_x * 2 + size.cx + szx);
    if (ShouldDisplayStatusIcon())
      width += STATUS_ICON_SIZE;
  }

  /* Auxiliary */
  if (!_context.aux.str.empty()) {
    size = GetPreeditSize(dc, _context.aux, pDWR->pPreeditTextFormat, pDWR);
    // icon size higher then auxiliary text
    int yoffset = (STATUS_ICON_SIZE >= size.cy && ShouldDisplayStatusIcon())
                      ? (STATUS_ICON_SIZE - size.cy) / 2
                      : 0;
    _auxiliaryRect.SetRect(w, height + yoffset, w + size.cx,
                           height + yoffset + size.cy);
    height += size.cy + 2 * yoffset + _style.spacing;
    width = max(width, real_margin_x * 2 + size.cx);
  }

  /* grid candidates */
  const int grid_columns = 5;
  if (candidates_count) {
    CSize candSizes[MAX_CANDIDATES_COUNT];
    int max_cand_width = 0, max_cand_height = 0;
    for (auto i = 0; i < candidates_count && i < MAX_CANDIDATES_COUNT; ++i) {
      CSize label_sz, text_sz, cmt_sz;
      int cand_width = 0, cand_height = 0;
      /* Label */
      std::wstring label =
          GetLabelText(labels, i, _style.label_text_format.c_str());
      GetTextSizeDW(label, label.length(), pDWR->pLabelTextFormat, pDWR,
                    &label_sz);
      cand_width += label_sz.cx * labelFontValid;
      cand_height = max(cand_height, label_sz.cy);
      /* Text */
      const std::wstring& text = candidates.at(i).str;
      GetTextSizeDW(text, text.length(), pDWR->pTextFormat, pDWR, &text_sz);
      cand_width += _style.hilite_spacing + text_sz.cx * textFontValid;
      cand_height = max(cand_height, text_sz.cy);
      /* Comment */
      bool cmtFontNotTrans =
          (i == id && (_style.hilited_comment_text_color & 0xff000000)) ||
          (i != id && (_style.comment_text_color & 0xff000000));
      if (!comments.at(i).str.empty() && cmtFontValid && cmtFontNotTrans) {
        const std::wstring& comment = comments.at(i).str;
        GetTextSizeDW(comment, comment.length(), pDWR->pCommentTextFormat, pDWR,
                      &cmt_sz);
        cand_width += _style.hilite_spacing + cmt_sz.cx * cmtFontValid;
        cand_height = max(cand_height, cmt_sz.cy);
      }
      candSizes[i].cx = cand_width;
      candSizes[i].cy = cand_height;
      max_cand_width = max(max_cand_width, cand_width);
      max_cand_height = max(max_cand_height, cand_height);
    }

    int cell_w = max_cand_width + _style.hilite_padding_x * 2;
    int cell_h = max_cand_height + _style.hilite_padding_y * 2;
    int rows = (candidates_count + grid_columns - 1) / grid_columns;

    for (auto i = 0; i < candidates_count && i < MAX_CANDIDATES_COUNT; ++i) {
      int col = i % grid_columns;
      int row = i / grid_columns;
      int x =
          offsetX + real_margin_x + col * (cell_w + _style.candidate_spacing);
      int y = height + row * (cell_h + _style.spacing);

      _candidateRects[i].SetRect(x, y, x + cell_w, y + cell_h);

      // inner text, top-aligned, vertically centered within cell
      int tx = x + _style.hilite_padding_x;
      int ty = y + (cell_h - candSizes[i].cy) / 2;
      /* Label */
      std::wstring label =
          GetLabelText(labels, i, _style.label_text_format.c_str());
      CSize label_sz;
      GetTextSizeDW(label, label.length(), pDWR->pLabelTextFormat, pDWR,
                    &label_sz);
      _candidateLabelRects[i].SetRect(tx, ty, tx + label_sz.cx * labelFontValid,
                                      ty + label_sz.cy);
      tx += label_sz.cx * labelFontValid + _style.hilite_spacing;
      /* Text */
      const std::wstring& text = candidates.at(i).str;
      CSize text_sz;
      GetTextSizeDW(text, text.length(), pDWR->pTextFormat, pDWR, &text_sz);
      _candidateTextRects[i].SetRect(tx, ty, tx + text_sz.cx * textFontValid,
                                     ty + text_sz.cy);
      tx += text_sz.cx * textFontValid + _style.hilite_spacing;
      /* Comment */
      bool cmtFontNotTrans =
          (i == id && (_style.hilited_comment_text_color & 0xff000000)) ||
          (i != id && (_style.comment_text_color & 0xff000000));
      if (!comments.at(i).str.empty() && cmtFontValid && cmtFontNotTrans) {
        const std::wstring& comment = comments.at(i).str;
        CSize cmt_sz;
        GetTextSizeDW(comment, comment.length(), pDWR->pCommentTextFormat, pDWR,
                      &cmt_sz);
        _candidateCommentRects[i].SetRect(tx, ty, tx + cmt_sz.cx * cmtFontValid,
                                          ty + cmt_sz.cy);
      } else
        _candidateCommentRects[i].SetRect(tx, ty, tx, ty + text_sz.cy);
    }

    width = max(width, offsetX + real_margin_x + grid_columns * cell_w +
                           (grid_columns - 1) * _style.candidate_spacing +
                           real_margin_x);
    height += rows * cell_h + (rows - 1) * _style.spacing;
  } else {
    height -= _style.spacing + offsetY;
    width += _style.hilite_spacing + _style.border;
  }

  width += real_margin_x;
  height += real_margin_y;

  if (candidates_count) {
    width = max(width, _style.min_width);
    height = max(height, _style.min_height);
  }
  _highlightRect = _candidateRects[id];
  UpdateStatusIconLayout(&width, &height);
  _contentSize.SetSize(width + offsetX, height + 2 * offsetY);
  _contentRect.SetRect(0, 0, _contentSize.cx, _contentSize.cy);

  // calc page indicator
  if (page_en && candidates_count && !_style.inline_preedit) {
    int _prex = _contentSize.cx - offsetX - real_margin_x +
                _style.hilite_padding_x - pgw;
    int _prey = (_preeditRect.top + _preeditRect.bottom) / 2 - pgszl.cy / 2;
    _prePageRect.SetRect(_prex, _prey, _prex + pgszl.cx, _prey + pgszl.cy);
    _nextPageRect.SetRect(_prePageRect.right + _style.hilite_spacing, _prey,
                          _prePageRect.right + _style.hilite_spacing + pgszr.cx,
                          _prey + pgszr.cy);
    if (ShouldDisplayStatusIcon()) {
      _prePageRect.OffsetRect(-STATUS_ICON_SIZE, 0);
      _nextPageRect.OffsetRect(-STATUS_ICON_SIZE, 0);
    }
  }

  // prepare temp rect _bgRect for roundinfo calculation
  CopyRect(_bgRect, _contentRect);
  _bgRect.DeflateRect(offsetX + 1, offsetY + 1);
  // prepare round info for single row status, only for single row situation
  _PrepareRoundInfo(dc);
  // each grid cell rounds independently
  for (auto i = 0; i < candidates_count && i < MAX_CANDIDATES_COUNT; ++i) {
    _roundInfo[i].IsTopLeftNeedToRound = true;
    _roundInfo[i].IsBottomLeftNeedToRound = true;
    _roundInfo[i].IsTopRightNeedToRound = true;
    _roundInfo[i].IsBottomRightNeedToRound = true;
    _roundInfo[i].Hemispherical = _roundInfo[0].Hemispherical;
  }
  // truely draw content size calculation
  _contentRect.DeflateRect(offsetX, offsetY);
}
