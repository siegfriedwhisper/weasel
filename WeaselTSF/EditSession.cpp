#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "ResponseParser.h"

STDAPI WeaselTSF::DoEditSession(TfEditCookie ec) {
  // get commit string from server
  std::wstring commit;
  weasel::Config config;
  auto context = std::make_shared<weasel::Context>();
  weasel::ResponseParser parser(&commit, context.get(), &_status, &config,
                                &_cand->style());

  bool ok = m_client.GetResponseData(std::ref(parser));

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
    if (_status.composing && !context->cinfo.candies.empty())
      _ExpandCandidatesToGrid(*context);
    _UpdateUI(*context, _status);
  }

  return TRUE;
}

void WeaselTSF::_ExpandCandidatesToGrid(weasel::Context& ctx) {
  const int kExtraPages = 3;  // current page + 3 = 4 rows x 5 columns
  weasel::Status page_status;
  weasel::Config page_config;
  // Pull the next 3 pages from the engine, appending each page's
  // candidates/comments/labels to the current context.
  for (int i = 0; i < kExtraPages; ++i) {
    if (!m_client.ChangePage(false))
      break;
    weasel::Context page_ctx;
    weasel::ResponseParser parser(nullptr, &page_ctx, &page_status,
                                  &page_config, nullptr);
    if (!m_client.GetResponseData(std::ref(parser)))
      break;
    auto& src = page_ctx.cinfo;
    auto& dst = ctx.cinfo;
    dst.candies.insert(dst.candies.end(), src.candies.begin(),
                       src.candies.end());
    dst.comments.insert(dst.comments.end(), src.comments.begin(),
                        src.comments.end());
    dst.labels.insert(dst.labels.end(), src.labels.begin(), src.labels.end());
  }
  // Restore the original page so page-relative Select/Highlight semantics
  // (page_start = selected_index / page_size * page_size) stay correct.
  for (int i = 0; i < kExtraPages; ++i) {
    if (!m_client.ChangePage(true))
      break;
    weasel::Context page_ctx;
    weasel::ResponseParser parser(nullptr, &page_ctx, &page_status,
                                  &page_config, nullptr);
    m_client.GetResponseData(std::ref(parser));
  }
}
