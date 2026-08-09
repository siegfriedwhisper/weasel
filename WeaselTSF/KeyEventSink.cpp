#include "stdafx.h"
#include "WeaselIPC.h"
#include "WeaselTSF.h"
#include <KeyEvent.h>
#include "CandidateList.h"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

static weasel::KeyEvent prevKeyEvent;
static BOOL prevfEaten = FALSE;
static int keyCountToSimulate = 0;

// temp grid-mode diagnostic log (TSF side). Remove after root-causing.
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

void WeaselTSF::_ProcessKeyEvent(WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
  // when _IsKeyboardDisabled don't eat the key,
  // when keyboard closable and keyboard closed, don't eat the key
  if ((_isToOpenClose && !_IsKeyboardOpen()) || _IsKeyboardDisabled()) {
    *pfEaten = FALSE;
    return;
  }

  // if server connection is Not OK, don't eat it.
  if (!_EnsureServerConnected()) {
    *pfEaten = FALSE;
    return;
  }
  weasel::KeyEvent ke;
  GetKeyboardState(_lpbKeyState);
  if (!ConvertKeyEvent(static_cast<UINT>(wParam), lParam, _lpbKeyState, ke)) {
    /* Unknown key event */
    *pfEaten = FALSE;
  } else {
    // cheet key code when vertical auto reverse happened, swap up and down
    if (_cand->GetIsReposition()) {
      if (ke.keycode == ibus::Up)
        ke.keycode = ibus::Down;
      else if (ke.keycode == ibus::Down)
        ke.keycode = ibus::Up;
    }
    if (_HandleGridModeKey(ke, pfEaten)) {
      prevfEaten = *pfEaten;
      prevKeyEvent = ke;
      return;
    }
    if (!keyCountToSimulate)
      *pfEaten = (BOOL)m_client.ProcessKeyEvent(ke);

    if (ke.keycode == ibus::Caps_Lock) {
      if (prevKeyEvent.keycode == ibus::Caps_Lock && prevfEaten == TRUE &&
          (ke.mask & ibus::RELEASE_MASK) && (!keyCountToSimulate)) {
        if ((GetKeyState(VK_CAPITAL) & 0x01)) {
          if (_committed || (!*pfEaten && _status.composing)) {
            keyCountToSimulate = 2;
            INPUT inputs[2];
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki = {VK_CAPITAL, 0, 0, 0, 0};
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki = {VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0, 0};
            ::SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));
          }
        }
        *pfEaten = TRUE;
      }
      if (keyCountToSimulate)
        keyCountToSimulate--;
    }

    prevfEaten = *pfEaten;
    prevKeyEvent = ke;
  }
}

/* Grid Layout (5x5 candidate matrix) */
void WeaselTSF::_EnterGridMode() {
  if (m_grid_mode)
    return;
  m_grid_original_layout = _cand->style().layout_type;
  m_grid_mode = true;
  grid_log("EnterGridMode: SetLayoutType(GRID) orig_layout=%d",
           m_grid_original_layout);
  // 通过 IPC 通知 Server 切换渲染布局（渲染在 WeaselServer 进程）
  m_client.SetLayoutType(weasel::UIStyle::LAYOUT_GRID);
  _cand->SetLayoutType(weasel::UIStyle::LAYOUT_GRID);
  _cand->Refresh();
}

void WeaselTSF::_ExitGridMode() {
  if (!m_grid_mode)
    return;
  m_grid_mode = false;
  grid_log("ExitGridMode: SetLayoutType(%d)", m_grid_original_layout);
  m_client.SetLayoutType(m_grid_original_layout);
  _cand->SetLayoutType((weasel::UIStyle::LayoutType)m_grid_original_layout);
  _cand->Refresh();
}

bool WeaselTSF::_HandleGridModeKey(const weasel::KeyEvent& ke, BOOL* pfEaten) {
  UINT cand_count = 0, current_select = 0;
  _cand->GetCount(&cand_count);
  _cand->GetSelection(&current_select);

  bool is_release = (ke.mask & ibus::RELEASE_MASK) != 0;
  bool has_modifier =
      (ke.mask & (ibus::SHIFT_MASK | ibus::CONTROL_MASK | ibus::ALT_MASK)) != 0;

  grid_log(
      "HandleKey keycode=%d mask=0x%x release=%d cand=%u sel=%u grid=%d mod=%d",
      ke.keycode, ke.mask, is_release, cand_count, current_select, m_grid_mode,
      has_modifier);

  if (!m_grid_mode) {
    // ↓ 且非释放 且无修饰键 且有候选 → 展开矩阵
    if (!is_release && !has_modifier && ke.keycode == ibus::Down &&
        cand_count > 0) {
      grid_log("-> expand grid");
      _EnterGridMode();
      *pfEaten = TRUE;
      return true;
    }
    return false;
  }

  // grid 模式
  if (cand_count == 0) {
    // 候选消失（选词/清空）后自动退出矩阵
    grid_log("-> exit grid (cand_count==0)");
    _ExitGridMode();
    return false;
  }

  // keyup 直接吃键不处理：避免弹起方向键时误移动高亮/误触发收起
  if (is_release) {
    grid_log("-> eat keyup");
    *pfEaten = TRUE;
    return true;
  }

  int index = (int)current_select;
  int new_index = index;
  bool handled = false;
  switch (ke.keycode) {
    case ibus::Left:
      if (index > 0)
        new_index = index - 1;
      handled = true;
      break;
    case ibus::Right:
      if (index + 1 < (int)cand_count)
        new_index = index + 1;
      handled = true;
      break;
    case ibus::Up:
      if (index < 5) {  // 顶行 ↑ → 收起
        grid_log("-> exit grid (up on top row)");
        _ExitGridMode();
        *pfEaten = TRUE;
        return true;
      }
      new_index = index - 5;
      handled = true;
      break;
    case ibus::Down:
      if (index + 5 < (int)cand_count)
        new_index = index + 5;
      handled = true;
      break;
    case ibus::Escape:  // Esc → 收起
      grid_log("-> exit grid (escape)");
      _ExitGridMode();
      *pfEaten = TRUE;
      return true;
    default:
      // 其他键（空格/回车/字母）交给引擎正常处理
      break;
  }

  if (handled) {
    if (new_index != index)
      m_client.HighlightCandidateOnCurrentPage(new_index);
    grid_log("-> grid nav %d -> %d (handled)", index, new_index);
    *pfEaten = TRUE;
    return true;
  }
  return false;
}

STDAPI WeaselTSF::OnSetFocus(BOOL fForeground) {
  if (fForeground)
    m_client.FocusIn();
  else {
    m_client.FocusOut();
    _AbortComposition();
  }

  return S_OK;
}

/* Some apps sends strange OnTestKeyDown/OnKeyDown combinations:
 *  Some sends OnKeyDown() only. (QQ2012)
 *  Some sends multiple OnTestKeyDown() for a single key event. (MS WORD 2010
 * x64)
 *
 * We assume every key event will eventually cause a OnKeyDown() call.
 * We use _fTestKeyDownPending to omit multiple OnTestKeyDown() calls,
 *  and for OnKeyDown() to check if the key has already been sent to the server.
 */

STDAPI WeaselTSF::OnTestKeyDown(ITfContext* pContext,
                                WPARAM wParam,
                                LPARAM lParam,
                                BOOL* pfEaten) {
  _fTestKeyUpPending = FALSE;
  if (_fTestKeyDownPending) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  if (*pfEaten)
    _fTestKeyDownPending = TRUE;
  return S_OK;
}

STDAPI WeaselTSF::OnKeyDown(ITfContext* pContext,
                            WPARAM wParam,
                            LPARAM lParam,
                            BOOL* pfEaten) {
  _fTestKeyUpPending = FALSE;
  if (_fTestKeyDownPending) {
    _fTestKeyDownPending = FALSE;
    *pfEaten = TRUE;
  } else {
    _ProcessKeyEvent(wParam, lParam, pfEaten);
    _UpdateComposition(pContext);
  }
  return S_OK;
}

STDAPI WeaselTSF::OnTestKeyUp(ITfContext* pContext,
                              WPARAM wParam,
                              LPARAM lParam,
                              BOOL* pfEaten) {
  _fTestKeyDownPending = FALSE;
  if (_fTestKeyUpPending) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  if (*pfEaten)
    _fTestKeyUpPending = TRUE;
  return S_OK;
}

STDAPI WeaselTSF::OnKeyUp(ITfContext* pContext,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL* pfEaten) {
  _fTestKeyDownPending = FALSE;
  if (_fTestKeyUpPending) {
    _fTestKeyUpPending = FALSE;
    *pfEaten = TRUE;
  } else {
    _ProcessKeyEvent(wParam, lParam, pfEaten);
    if (!_async_edit)
      _UpdateComposition(pContext);
  }
  return S_OK;
}

STDAPI WeaselTSF::OnPreservedKey(ITfContext* pContext,
                                 REFGUID rguid,
                                 BOOL* pfEaten) {
  *pfEaten = FALSE;
  return S_OK;
}

BOOL WeaselTSF::_InitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
  HRESULT hr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return FALSE;

  hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink*)this,
                                         TRUE);

  return (hr == S_OK);
}

void WeaselTSF::_UninitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return;

  pKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);
}

BOOL WeaselTSF::_InitPreservedKey() {
  return TRUE;
#if 0
	com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
	if (_pThreadMgr->QueryInterface(pKeystrokeMgr.GetAddressOf()) != S_OK)
	{
		return FALSE;
	}
	TF_PRESERVEDKEY preservedKeyImeMode;

	/* Define SHIFT ONLY for now */
	preservedKeyImeMode.uVKey = VK_SHIFT;
	preservedKeyImeMode.uModifiers = TF_MOD_ON_KEYUP;

	auto hr = pKeystrokeMgr->PreserveKey(
		_tfClientId,
		GUID_IME_MODE_PRESERVED_KEY,
		&preservedKeyImeMode, L"", 0);
	
	return SUCCEEDED(hr);
#endif
}

void WeaselTSF::_UninitPreservedKey() {}
