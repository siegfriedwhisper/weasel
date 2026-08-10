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
  grid_log("Edit ok=%d comp=%d cand=%zu", ok, _status.composing,
           context->cinfo.candies.size());

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
    // key was eaten (e.g. grid navigation) there is no new response and
    // `context` is a fresh empty Context — updating would wipe the cached
    // candidate list, making GetCount() return 0 and prematurely collapsing
    // the grid layout on the very next key.
    if (_status.composing && !context->cinfo.candies.empty()) {
      _UpdateUI(*context, _status);
    } else if (!_status.composing) {
      // composition ended: clear the UI normally
      _UpdateUI(*context, _status);
    }
    // composing but no candidates (exhausted menu / failed page flip):
    // keep the current UI, do NOT wipe the candidate window.
  }

  return TRUE;
}
