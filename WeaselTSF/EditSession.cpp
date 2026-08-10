#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "ResponseParser.h"
#include <cstdio>
#include <cstdlib>

// temp grid diagnostic log (TSF side). Remove after root-causing.
static void grid_log(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  const char* tmp = getenv("TEMP");
  std::string path = std::string(tmp ? tmp : "C:\\") + "\\weasel-grid-tsf.log";
  FILE* f = fopen(path.c_str(), "a");
  if (f) {
    fprintf(f, "%s\n", buf);
    fclose(f);
  }
}

STDAPI WeaselTSF::DoEditSession(TfEditCookie ec) {
  // get commit string from server
  std::wstring commit;
  weasel::Config config;
  auto context = std::make_shared<weasel::Context>();
  weasel::ResponseParser parser(&commit, context.get(), &_status, &config,
                                &_cand->style());

  bool ok = m_client.GetResponseData(std::ref(parser));
  grid_log("Edit ok=%d comp=%d cand=%zu flip=%d row=%d", ok, _status.composing,
           context->cinfo.candies.size(), m_grid_flip, m_grid_row);

  _UpdateLanguageBar(_status);

  if (ok) {
    if (!commit.empty()) {
      // For auto-selecting, commit and preedit can both exist.
      // Commit and close the original composition first.
      if (!_IsComposing()) {
        _StartComposition(_pEditSessionContext,
                          _fCUASWorkaroundEnabled && !config.inline_preedit);
      }
      _InsertText(_pEditSessionContext, commit);
      _EndComposition(_pEditSessionContext, false);
      _committed = TRUE;
    } else {
      _committed = FALSE;
    }
    if (_status.composing && !_IsComposing()) {
      _StartComposition(_pEditSessionContext,
                        _fCUASWorkaroundEnabled && !config.inline_preedit);
    } else if (!_status.composing && _IsComposing()) {
      _EndComposition(_pEditSessionContext, true);
    }
    if (_IsComposing() && config.inline_preedit) {
      _ShowInlinePreedit(_pEditSessionContext, context);
    }
    _UpdateCompositionWindow(_pEditSessionContext);
    // Only refresh the UI cache when a real response was parsed. When the
    // key was eaten (e.g. grid-mode navigation) there is no new response and
    // `context` is a fresh empty Context — updating would wipe the cached
    // candidate list, making GetCount() return 0 and prematurely collapsing
    // the grid layout on the very next key.
    if (m_grid_flip) {
      m_grid_flip = false;  // up/down flip: keep the highlight row
    } else {
      m_grid_row = 0;  // any other key: back to the top row (current page)
    }
    if (_status.composing && !context->cinfo.candies.empty())
      _ExpandCandidatesToGrid(*context);
    _UpdateUI(*context, _status);
  }

  return TRUE;
}

void WeaselTSF::_ExpandCandidatesToGrid(weasel::Context& ctx) {
  const int kRows = 4, kCols = 5;
  const int row = m_grid_row;  // engine's current page sits at grid row
  weasel::Status page_status;
  weasel::Config page_config;
  std::vector<weasel::Text> pages[kRows];
  int p1 = 0, p2 = 0, p3 = 0;
  // 1. Walk back `row` pages to the window start, caching each page.
  for (int i = 0; i < row; ++i) {
    if (!m_client.ChangePage(true)) {
      p1 = -1;
      break;
    }
    ++p1;
    weasel::Context pc;
    weasel::ResponseParser parser(nullptr, &pc, &page_status, &page_config,
                                  nullptr);
    if (!m_client.GetResponseData(std::ref(parser)))
      break;
    pages[row - 1 - i] = pc.cinfo.candies;
  }
  // 2. Pull the remaining pages after the current one (rows row+1..3).
  for (int i = 0; i < kRows - 1 - row; ++i) {
    if (!m_client.ChangePage(false)) {
      p2 = -1;
      break;
    }
    ++p2;
    weasel::Context pc;
    weasel::ResponseParser parser(nullptr, &pc, &page_status, &page_config,
                                  nullptr);
    if (!m_client.GetResponseData(std::ref(parser)))
      break;
    pages[row + 1 + i] = pc.cinfo.candies;
  }
  // 3. Restore the engine's current page so page-relative Select/Highlight
  // semantics (page_start = selected_index / page_size * page_size) hold.
  for (int i = 0; i < kRows - 1 - row; ++i) {
    if (!m_client.ChangePage(true)) {
      p3 = -1;
      break;
    }
    ++p3;
    weasel::Context pc;
    weasel::ResponseParser parser(nullptr, &pc, &page_status, &page_config,
                                  nullptr);
    m_client.GetResponseData(std::ref(parser));
  }
  grid_log("Expand row=%d p1=%d p2=%d p3=%d", row, p1, p2, p3);
  // 4. Assemble the 4x5 grid; the engine's current page is row `row`.
  auto& cinfo = ctx.cinfo;
  std::vector<weasel::Text> candies;
  std::vector<weasel::Text> comments;
  std::vector<weasel::Text> labels;
  candies.reserve(kRows * kCols);
  comments.reserve(kRows * kCols);
  labels.reserve(kRows * kCols);
  for (int r = 0; r < kRows; ++r) {
    if (r == row) {
      candies.insert(candies.end(), cinfo.candies.begin(), cinfo.candies.end());
      comments.insert(comments.end(), cinfo.comments.begin(),
                      cinfo.comments.end());
      labels.insert(labels.end(), cinfo.labels.begin(), cinfo.labels.end());
    } else {
      candies.insert(candies.end(), pages[r].begin(), pages[r].end());
    }
  }
  cinfo.candies = std::move(candies);
  cinfo.comments = std::move(comments);
  cinfo.labels = std::move(labels);
  cinfo.highlighted += row * kCols;  // highlight the row's column
}

void WeaselTSF::_GridMoveRow(int delta) {
  const int kMaxRow = 3;
  int new_row = m_grid_row + delta;
  if (new_row >= 0 && new_row <= kMaxRow)
    m_grid_row = new_row;  // move within the window; at the edge the window
                           // scrolls instead (page follows, row stays pinned)
  bool cp = m_client.ChangePage(delta > 0 ? false : true);
  m_grid_flip = true;
  grid_log("GridMoveRow d=%d row->%d chgpage=%d flip=%d", delta, m_grid_row,
           (int)cp, m_grid_flip);
}
