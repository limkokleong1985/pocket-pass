
// TextInputUI.cpp
#include "TextInputUI.h"
#include <string.h>
#include <stdio.h>

static inline void buildMaskedSlice(const char* src,
    uint8_t totalLen,
    char* out,
    uint8_t visibleMax,
    bool revealLastDigit)
{
  uint8_t visibleLen = (totalLen <= visibleMax) ? totalLen : visibleMax;
  uint8_t startIdx   = (totalLen <= visibleMax) ? 0 : (totalLen - visibleMax);

  if (visibleLen == 0) { out[0] = '\0'; return; }

  for (uint8_t i = 0; i < visibleLen; ++i) out[i] = '*';
  if (revealLastDigit && totalLen > 0) {
    uint8_t lastIdxGlobal = totalLen - 1;
    if (lastIdxGlobal >= startIdx) {
      uint8_t lastIdxLocal = lastIdxGlobal - startIdx;
      out[lastIdxLocal] = src[lastIdxGlobal];
    }
  }
  out[visibleLen] = '\0';
}

// ==== Ctors ====
TextInputUI::TextInputUI(SimpleRotaryController& encoder,
    const char* title,
    const char* description,
    uint8_t maxLen,
    InputMode inputMode)
: _encCtorPtr(&encoder),
  _title(title),
  _description(description),
  _maxLen(maxLen <= MAX_CAP ? maxLen : MAX_CAP),
  _inputMode(inputMode)
{}

TextInputUI::TextInputUI(const char* title,
    const char* description,
    uint8_t maxLen,
    InputMode inputMode)
: _encCtorPtr(nullptr),
  _title(title),
  _description(description),
  _maxLen(maxLen <= MAX_CAP ? maxLen : MAX_CAP),
  _inputMode(inputMode)
{}

// ==== Public ====
void TextInputUI::begin(SimpleRotaryController& encoder) {
  _encPtr = &encoder;

  _pickerMode = (_inputMode == InputMode::PASSCODE) ? PickerMode::NUM
               : (_inputMode == InputMode::INTEGER) ? PickerMode::INTNUM
               : (_inputMode == InputMode::MORSE) ? PickerMode::MORSE_ACTIONS
               : PickerMode::UPPER;
  _activeIndex = 0;
  _cursorVisible = true;
  _lastBlinkMs = millis();

  _morsePressing = false;
  _morsePressStart = 0;
  morseResetSeq();
  _lastMorseActivity = millis();
  _insertedSpaceSinceLastActivity = false;
  _aPrev = false; // ALT RELEASE DETECTION default

  drawStaticUI();
  drawInputText();
  drawScroller();
}

void TextInputUI::begin() {
  if (_encPtr == nullptr) _encPtr = _encCtorPtr;

  _pickerMode = (_inputMode == InputMode::PASSCODE) ? PickerMode::NUM
               : (_inputMode == InputMode::INTEGER) ? PickerMode::INTNUM
               : (_inputMode == InputMode::MORSE) ? PickerMode::MORSE_ACTIONS
               : PickerMode::UPPER;
  _activeIndex = 0;
  _cursorVisible = true;
  _lastBlinkMs = millis();

  _morsePressing = false;
  _morsePressStart = 0;
  morseResetSeq();
  _lastMorseActivity = millis();
  _insertedSpaceSinceLastActivity = false;
  _aPrev = false;

  drawStaticUI();
  drawInputText();
  drawScroller();
}

void TextInputUI::update() {
  if (!_encPtr) return;

  _encPtr->update();

  // Rotary scroller
  uint8_t n = modeCount(_pickerMode);
  bool changed = false;
  if (n > 1) {
    if (_encPtr->wasTurnedCCW())  { 
      if(_inputInvert)
        _activeIndex++;
      else
        _activeIndex--;
      changed = true; 
    }
    if (_encPtr->wasTurnedCW()) { 
       if(_inputInvert)
        _activeIndex--;
      else
        _activeIndex++;
      changed = true; 
    }
    if (changed) {
      int16_t m = static_cast<int16_t>(n);
      _activeIndex = (int16_t)((_activeIndex % m + m) % m);
      drawScroller();
      _lastBlinkMs = millis();
      _cursorVisible = true;
    }
  } else {
    _encPtr->wasTurnedCCW();
    _encPtr->wasTurnedCW();
  }

  // Buttons and modes
  if (_inputMode == InputMode::PASSCODE) {
    if (_encPtr->wasPressedA()) {
      if (_inputLen > 0) {
        _inputLen--;
        _buf[_inputLen] = '\0';
        _lastBlinkMs = millis(); _cursorVisible = true;
        drawInputText(); drawRemainingLabel();
      } else {
        flashInputLine();
      }
    }
    if (_encPtr->wasPressedB()) doSelect();
  } else if (_inputMode == InputMode::INTEGER) {
    if (_encPtr->wasPressedA()) nextPickerMode();
    if (_encPtr->wasPressedB()) doSelect();
  } else if (_inputMode == InputMode::MORSE) {
    morseHandlePressLogic();
    morseHandleGaps();
    if (_encPtr->wasPressedB()) doSelect(); // BACK/SAVE/QUIT
  } else {
    if (_encPtr->wasPressedA()) nextPickerMode();
    if (_encPtr->wasPressedB()) doSelect();
  }

  updateCursorBlink();
}

void TextInputUI::setInputInvert(bool invert) {
  _inputInvert = invert;
}

void TextInputUI::setTitle(const char* t) {
  _title = t;
  drawStaticUI(); drawInputText(); drawScroller();
}

void TextInputUI::setDescription(const char* d) {
  _description = d;
  drawStaticUI(); drawInputText(); drawScroller();
}

void TextInputUI::setMaxLen(uint8_t m) {
  _maxLen = (m <= MAX_CAP) ? m : MAX_CAP;
  if (_inputLen > _maxLen) { _inputLen = _maxLen; _buf[_inputLen] = '\0'; }
  drawRemainingLabel(); drawInputText();
}

void TextInputUI::setInputMode(InputMode m) {
  _inputMode = m;
  _pickerMode = (_inputMode == InputMode::PASSCODE) ? PickerMode::NUM
               : (_inputMode == InputMode::INTEGER) ? PickerMode::INTNUM
               : (_inputMode == InputMode::MORSE) ? PickerMode::MORSE_ACTIONS
               : PickerMode::UPPER;
  _activeIndex = 0;

  if (_inputMode == InputMode::MORSE) {
    _morsePressing = false;
    _morsePressStart = 0;
    morseResetSeq();
    _lastMorseActivity = millis();
    _insertedSpaceSinceLastActivity = false;
    _aPrev = false;
  }

  drawStaticUI(); drawInputText(); drawScroller();
}

void TextInputUI::clear() {
  _inputLen = 0; _buf[0] = '\0';
  morseResetSeq();
  //drawInputText(); 
  //drawRemainingLabel();
}

// ==== UI drawing ====
void TextInputUI::drawStaticUI() {
  LCD_Clear(BLACK);
  LCD_DrawLine(0, _layout.startY + 8,  LCD_Width() - 1, _layout.startY + 8,  WHITE);
  LCD_DrawLine(0, _layout.startY + 10, LCD_Width() - 1, _layout.startY + 10, WHITE);

  if (_title && *_title) {
    drawStringWithPadding(_layout.startX + 7, _layout.startY,
                          _title, WHITE, BLACK, 2, 8, 4, false);
  }
  if (_description && *_description) {
    drawStringWrapWidth(_layout.startX + 7, _layout.startY + 25,
                        _description, WHITE, BLACK, 2, _layout.scrollPickX - 25);
  }

  LCD_DrawLine(0, _layout.inputStartY, LCD_Width() - 1, _layout.inputStartY, WHITE);
  drawRemainingLabel();
  drawStringWithPadding(_layout.inputStartX, _layout.inputStartY + 15,
                        "> ", WHITE, BLACK, 2, 8, 4, false);

  LCD_FillRect(_layout.scrollPickX, 0, LCD_Width() - _layout.scrollPickX, LCD_Height(), BLACK);
  LCD_DrawLine(_layout.scrollPickX, 0, _layout.scrollPickX, LCD_Height(), WHITE);
}

void TextInputUI::drawInputText() {
  const uint16_t clearX = _layout.inputStartX + 25;
  const uint16_t clearY = _layout.inputStartY + 15;
  const uint16_t clearW = _layout.scrollPickX - clearX - 5;
  const uint16_t clearH = 26;

  LCD_FillRect(clearX - 7, clearY - 10, clearW + 10, clearH + 10, BLACK);

  uint8_t visibleLen = (_inputLen <= VISIBLE_CHARS) ? _inputLen : VISIBLE_CHARS;
  uint8_t startIdx   = (_inputLen <= VISIBLE_CHARS) ? 0 : (_inputLen - VISIBLE_CHARS);

  char line[VISIBLE_CHARS + 1];
  if (_inputMode == InputMode::PASSCODE) {
    buildMaskedSlice(_buf, _inputLen, line, VISIBLE_CHARS, true);
  } else {
    if (visibleLen) memcpy(line, _buf + startIdx, visibleLen);
    line[visibleLen] = '\0';
  }

  drawStringWithPadding(clearX, clearY, line, WHITE, BLACK,
                        INPUT_FONT_SIZE, INPUT_TEXT_PAD_X, INPUT_TEXT_PAD_Y, false);

  if (_cursorVisible) {
    uint16_t baseX = (uint16_t)(_layout.inputStartX + 25 + INPUT_TEXT_PAD_X);
    uint16_t baseY = (uint16_t)(_layout.inputStartY + 15 + INPUT_TEXT_PAD_Y);
    UICaretRect r;
    r.x = (uint16_t)(baseX + (visibleLen * CHAR_ADV) - 5);
    r.y = (uint16_t)(baseY + (LINE_H) - 1);
    r.w = CHAR_BODY_W;
    r.h = 1;
    LCD_FillRect(r.x, r.y, r.w, r.h, WHITE);
  }

  if (_inputMode == InputMode::MORSE) {
    drawMorseIndicator();
  }
}

void TextInputUI::drawModeHeader() {
  LCD_FillRect(_layout.scrollPickX + 2, 2, LCD_Width() - (_layout.scrollPickX), 16, BLACK);
  const char* name = "";
  if (_inputMode == InputMode::PASSCODE) name = "";
  else if (_inputMode == InputMode::MORSE) name = "morse actions";
  else {
    switch (_pickerMode) {
      case PickerMode::UPPER: name = "UPPER"; break;
      case PickerMode::LOWER: name = "lower"; break;
      case PickerMode::NUM:   name = "123"; break;
      case PickerMode::SYM:   name = "symbols"; break;
      case PickerMode::ACTIONS: name = "action"; break;
      case PickerMode::SAVE:  name = "save"; break;
      case PickerMode::INTNUM: name = "numbers"; break;
      case PickerMode::ACTIONS_INT: name = "actions"; break;
      case PickerMode::MORSE_ACTIONS: name = "morse actions"; break;
      default: break;
    }
  }
  if (name[0] != '\0') {
    drawStringWithPadding(_layout.scrollPickX + 10, 4, name, WHITE, BLACK, 1, 6, 3, false);
  }
}

void TextInputUI::clearRightPaneContent() {
  LCD_FillRect(_layout.scrollPickX + 1, 20,
               LCD_Width() - (_layout.scrollPickX),
               LCD_Height() - 22, BLACK);
}

void TextInputUI::drawScroller() {
  clearRightPaneContent();
  drawModeHeader();

  const int8_t  relIdx[5]    = { -2, -1,  0, +1, +2 };
  const int16_t xOffsets[5]  = {  30,  20, 10,  20, 30 };

  for (uint8_t i = 0; i < 5; i++) {
    int16_t idx = _activeIndex + relIdx[i];
    uint16_t x = _layout.scrollPickX + xOffsets[i];
    uint16_t y = _layout.offsetY + (i * _layout.scrollPickGap);

    if (_inputMode == InputMode::PASSCODE) {
      char c = '0' + ((idx % 10 + 10) % 10);
      char s[2] = { c, 0 };
      if (i == 2) drawStringWithPadding(x, y, s, BLACK, WHITE, 2, 8, 4, false);
      else        drawStringWithPadding(x, y, s, WHITE, BLACK, 2, 8, 4, false);
      continue;
    }

    if (_inputMode == InputMode::INTEGER) {
      if (_pickerMode == PickerMode::INTNUM) {
        char c = '0' + ((idx % 10 + 10) % 10);
        char s[2] = { c, 0 };
        if (i == 2) drawStringWithPadding(x, y, s, BLACK, WHITE, 2, 8, 4, false);
        else        drawStringWithPadding(x, y, s, WHITE, BLACK, 2, 8, 4, false);
      } else {
        const char* s = actionLabel(_pickerMode, idx);
        if (i == 2) drawStringWithPadding(x, y, s, BLACK, WHITE, 2, 8, 4, false);
        else        drawStringWithPadding(x, y, s, WHITE, BLACK, 2, 8, 4, false);
      }
      continue;
    }

    if (_inputMode == InputMode::MORSE) {
      const char* s = actionLabel(PickerMode::MORSE_ACTIONS, idx);
      if (i == 2) drawStringWithPadding(x, y, s, BLACK, WHITE, 2, 8, 4, false);
      else        drawStringWithPadding(x, y, s, WHITE, BLACK, 2, 8, 4, false);
      continue;
    }

    if (_pickerMode == PickerMode::ACTIONS || _pickerMode == PickerMode::SAVE) {
      const char* s = actionLabel(_pickerMode, idx);
      if (i == 2) drawStringWithPadding(x, y, s, BLACK, WHITE, 2, 8, 4, false);
      else        drawStringWithPadding(x, y, s, WHITE, BLACK, 2, 8, 4, false);
    } else {
      char c = charAt(_pickerMode, idx);
      char s[2] = { c ? c : ' ', 0 };
      if (i == 2) drawStringWithPadding(x, y, s, BLACK, WHITE, 2, 8, 4, false);
      else        drawStringWithPadding(x, y, s, WHITE, BLACK, 2, 8, 4, false);
    }
  }
}

void TextInputUI::drawRemainingLabel() {
  uint8_t remaining = (_inputLen < _maxLen) ? (_maxLen - _inputLen) : 0;
  char buf[20];
  snprintf(buf, sizeof(buf), "LEFT: %u", (unsigned)remaining);

  const uint16_t y = (uint16_t)(_layout.inputStartY - 12) + 1;
  const uint16_t rightEdge = _layout.scrollPickX - 6;

  uint16_t w = (uint16_t)(strlen(buf) * 6);
  uint16_t x = (rightEdge > w) ? (rightEdge - w) : 0;

  drawStringWithPadding(x, y, buf, BLACK, WHITE, 1, 6, 3, false);
}

// ==== Interactions ====
void TextInputUI::nextPickerMode() {
  if (_inputMode == InputMode::PASSCODE) {
    _pickerMode = PickerMode::NUM; _activeIndex = 0;
    drawScroller(); _lastBlinkMs = millis(); _cursorVisible = true; return;
  }

  if (_inputMode == InputMode::INTEGER) {
    _pickerMode = (_pickerMode == PickerMode::INTNUM) ? PickerMode::ACTIONS_INT : PickerMode::INTNUM;
    _activeIndex = 0; drawScroller(); _lastBlinkMs = millis(); _cursorVisible = true; return;
  }

  if (_inputMode == InputMode::MORSE) {
    _pickerMode = PickerMode::MORSE_ACTIONS; _activeIndex = 0;
    drawScroller(); _lastBlinkMs = millis(); _cursorVisible = true; return;
  }

  switch (_pickerMode) {
    case PickerMode::UPPER:     _pickerMode = PickerMode::LOWER;   break;
    case PickerMode::LOWER:     _pickerMode = PickerMode::NUM;     break;
    case PickerMode::NUM:       _pickerMode = PickerMode::SYM;     break;
    case PickerMode::SYM:       _pickerMode = PickerMode::ACTIONS; break;
    case PickerMode::ACTIONS:   _pickerMode = PickerMode::SAVE;    break;
    case PickerMode::SAVE:      _pickerMode = PickerMode::UPPER;   break;
    default:                    _pickerMode = PickerMode::UPPER;   break;
  }
  _activeIndex = 0;
  drawScroller(); _lastBlinkMs = millis(); _cursorVisible = true;
}

void TextInputUI::flashInputLine() {
  const uint16_t clearX = _layout.inputStartX + 25;
  const uint16_t clearY = _layout.inputStartY + 15;
  const uint16_t clearW = _layout.scrollPickX - clearX;
  const uint16_t clearH = 26;

  uint8_t visibleLen = (_inputLen <= VISIBLE_CHARS) ? _inputLen : VISIBLE_CHARS;
  char line[VISIBLE_CHARS + 1];
  if (_inputMode == InputMode::PASSCODE) buildMaskedSlice(_buf, _inputLen, line, VISIBLE_CHARS, true);
  else {
    uint8_t startIdx = (_inputLen <= VISIBLE_CHARS) ? 0 : (_inputLen - VISIBLE_CHARS);
    if (visibleLen) memcpy(line, _buf + startIdx, visibleLen);
    line[visibleLen] = '\0';
  }

  LCD_FillRect(clearX - 7, clearY - 5, clearW, clearH, WHITE);
  drawStringWithPadding(clearX, clearY, line, BLACK, WHITE, INPUT_FONT_SIZE, INPUT_TEXT_PAD_X, INPUT_TEXT_PAD_Y, false);
  drawInputText();
}

void TextInputUI::doSelect() {
  if (_inputMode == InputMode::PASSCODE) {
    if (_inputLen < _maxLen) {
      uint8_t num = (uint8_t)((_activeIndex % 10 + 10) % 10);
      _buf[_inputLen++] = '0' + num; _buf[_inputLen] = '\0';
      _lastBlinkMs = millis(); _cursorVisible = true;
      drawInputText(); drawRemainingLabel();
      if (_inputLen >= _maxLen) {
        if (_onSave) _onSave(_buf);
        flashInputLine(); 
        _inputLen = 0;
        _buf[0] = '\0';
        clear();
        drawInputText(); drawRemainingLabel();
      }
    } else flashInputLine();
    return;
  }

  if (_inputMode == InputMode::INTEGER) {
    if (_pickerMode == PickerMode::ACTIONS_INT) {
      uint8_t n = modeCount(_pickerMode);
      int16_t idx = (_activeIndex % n + n) % n;
      if (idx == 0) {
        if (_inputLen > 0) { _inputLen--; _buf[_inputLen] = '\0'; _lastBlinkMs = millis(); _cursorVisible = true; drawInputText(); drawRemainingLabel(); }
        else flashInputLine();
      } else if (idx == 1) {
        if (_onSave) _onSave(_buf);
        flashInputLine();
        clear();
      } else {
        flashInputLine(); 
        if (_onCancel) _onCancel();
        clear();
      }
      _lastBlinkMs = millis(); _cursorVisible = true; drawInputText(); drawRemainingLabel();
      return;
    }
    if (_inputLen < _maxLen) {
      char c = '0' + (( _activeIndex % 10 + 10) % 10);
      _buf[_inputLen++] = c; _buf[_inputLen] = '\0';
      _lastBlinkMs = millis(); _cursorVisible = true;
      drawInputText(); drawRemainingLabel();
    } else flashInputLine();
    return;
  }

  if (_inputMode == InputMode::MORSE) {
    uint8_t n = modeCount(PickerMode::MORSE_ACTIONS);
    int16_t idx = (_activeIndex % n + n) % n; // 0 BACK, 1 SAVE, 2 QUIT
    if (idx == 0) {
      if (_morseSeqLen > 0) {
        morseResetSeq();
        _lastMorseActivity = millis();
        drawMorseIndicator();
        drawInputText();
      } else if (_inputLen > 0) {
        _inputLen--; _buf[_inputLen] = '\0';
        _lastBlinkMs = millis(); _cursorVisible = true;
        drawInputText(); drawRemainingLabel();
      } else flashInputLine();
    } else if (idx == 1) {
      if (_morseSeqLen > 0) morseDecodeAndCommit();
      if (_onSave) _onSave(_buf);
      flashInputLine();
      clear();
    } else {
      flashInputLine();
      if (_onCancel) _onCancel();
      clear();
    }
    return;
  }

  // STANDARD
  if (_pickerMode == PickerMode::ACTIONS) {
    uint8_t n = modeCount(_pickerMode);
    int16_t idx = (_activeIndex % n + n) % n;
    if (idx == 0) { // SPACE
      if (_inputLen < _maxLen) {
        _buf[_inputLen++] = ' ';
        _buf[_inputLen] = '\0';
        _lastBlinkMs = millis(); _cursorVisible = true;
        drawInputText(); drawRemainingLabel();
      }
    } else { // BACK
      if (_inputLen > 0) {
        _inputLen--; _buf[_inputLen] = '\0';
        _lastBlinkMs = millis(); _cursorVisible = true;
        drawInputText(); drawRemainingLabel();
      }
    }
    return;
  }

  if (_pickerMode == PickerMode::SAVE) {
    uint8_t n = modeCount(_pickerMode);
    int16_t idx = (_activeIndex % n + n) % n;
    if (idx == 0) { 
      if (_onSave) _onSave(_buf); 
      flashInputLine();
      clear(); 
    }
    else { 
      flashInputLine(); 
      if (_onCancel) _onCancel();
      clear(); 
    }
    _lastBlinkMs = millis(); _cursorVisible = true;
    drawInputText(); drawRemainingLabel();
    return;
  }

  if (_inputLen < _maxLen) {
    char c = charAt(_pickerMode, _activeIndex);
    if (c) {
      _buf[_inputLen++] = c; _buf[_inputLen] = '\0';
      _lastBlinkMs = millis(); _cursorVisible = true;
      drawInputText(); drawRemainingLabel();
    }
  } else flashInputLine();
}

void TextInputUI::updateCursorBlink() {
  unsigned long now = millis();
  if (now - _lastBlinkMs < BLINK_INTERVAL_MS) return;
  _lastBlinkMs = now; _cursorVisible = !_cursorVisible; drawCaret(_cursorVisible);
}

// ==== Geometry & helpers ====
void TextInputUI::getInputTextOrigin(uint16_t& baseX, uint16_t& baseY) const {
  baseX = (uint16_t)(_layout.inputStartX + 25 + INPUT_TEXT_PAD_X);
  baseY = (uint16_t)(_layout.inputStartY + 15 + INPUT_TEXT_PAD_Y);
}

TextInputUI::UICaretRect TextInputUI::getCaretRectThin() const {
  uint8_t visibleLen = (_inputLen <= VISIBLE_CHARS) ? _inputLen : VISIBLE_CHARS;
  uint16_t baseX, baseY; getInputTextOrigin(baseX, baseY);
  UICaretRect r;
  r.x = (uint16_t)(baseX + (visibleLen * CHAR_ADV) - 5);
  r.y = (uint16_t)(baseY + (LINE_H) - 1);
  r.w = CHAR_BODY_W; r.h = 1;
  return r;
}

void TextInputUI::drawCaret(bool visible) {
  UICaretRect r = getCaretRectThin();
  LCD_FillRect(r.x, r.y, r.w, r.h, visible ? WHITE : BLACK);
}

uint16_t TextInputUI::textPixelWidthFs1(const char* s) {
  return (uint16_t)(strlen(s) * 6);
}

// ==== Character sets ====
uint8_t TextInputUI::modeCount(PickerMode m) const {
  switch (m) {
    case PickerMode::UPPER:         return 26;
    case PickerMode::LOWER:         return 26;
    case PickerMode::NUM:           return 10;
    case PickerMode::SYM:           return SYM_LEN;
    case PickerMode::ACTIONS:       return 2;
    case PickerMode::SAVE:          return 2;
    case PickerMode::INTNUM:        return 10;
    case PickerMode::ACTIONS_INT:   return 3;
    case PickerMode::MORSE_ACTIONS: return 3;
    default:                        return 1;
  }
}

char TextInputUI::charAt(PickerMode m, int16_t idx) const {
  uint8_t n = modeCount(m);
  if (n == 0) return 0;
  idx = (idx % n + n) % n;
  switch (m) {
    case PickerMode::UPPER:   return (char)('A' + idx);
    case PickerMode::LOWER:   return (char)('a' + idx);
    case PickerMode::NUM:     return (char)('0' + idx);
    case PickerMode::SYM:
      if ((uint8_t)idx < SYM_LEN) return SYM_SET[idx];
      return 0;
    case PickerMode::INTNUM:  return (char)('0' + idx);
    default:                  return 0;
  }
}

const char* TextInputUI::actionLabel(PickerMode m, int16_t idx) const {
  uint8_t n = modeCount(m);
  uint8_t i = (idx % n + n) % n;
  if (m == PickerMode::ACTIONS) return (i == 0) ? "SPACE" : "BACK";
  else if (m == PickerMode::SAVE) return (i == 0) ? "SAVE" : "QUIT";
  else if (m == PickerMode::MORSE_ACTIONS || m == PickerMode::ACTIONS_INT) {
    if (i == 0) return "BACK";
    if (i == 1) return "SAVE";
    return "QUIT";
  }
  return "";
}

// ==== Morse helpers ====
// Preferred: finalize on release using wasReleasedA()
void TextInputUI::morseHandlePressLogic() {
  unsigned long now = millis();

  // Press edge
  if (!_morsePressing && _encPtr->wasPressedA()) {
    _morsePressing = true;
    _morsePressStart = now;
    _lastMorseActivity = now;
    _insertedSpaceSinceLastActivity = false;
    return;
  }

  // Release edge
  if (_morsePressing && _encPtr->wasReleasedA()) {
    unsigned long held = now - _morsePressStart;
    if (held <= _morseCfg.dotMaxMs) {
      morseAppendDot();
    } else if (held >= _morseCfg.dashMinMs) {
      morseAppendDash();
    } else {
      // Ambiguous: classify as dot
      morseAppendDot();
    }
    _morsePressing = false;
    _lastMorseActivity = now;
    drawInputText();
    return;
  }

}

void TextInputUI::clearMorseIndicatorArea() {
  // Fixed-size clear box sized for size-2 font and up to 11 chars "[.........]"
  const uint16_t charW = 6 * 2;
  const uint16_t charH = 8 * 2;
  const uint16_t padX = 8, padY = 4;
  const uint16_t maxChars = 11; // '[' + up to 9 symbols + ']'
  const uint16_t boxW = (maxChars * charW) + (2 * padX) + 6;
  const uint16_t boxH = charH + (2 * padY) + 6;

  LCD_FillRect(_layout.morseIndicatorX - 4,
               _layout.morseIndicatorY - 6,
               boxW,
               boxH,
               BLACK);
}

void TextInputUI::drawMorseIndicator() {
  clearMorseIndicatorArea();

  if (_morseSeqLen == 0) return;

  char preview[12] = {0};
  uint8_t k = 0;
  preview[k++] = '[';
  const uint8_t maxSymbols = 9; // ensure it fits in clear box
  uint8_t toCopy = (_morseSeqLen > maxSymbols) ? maxSymbols : _morseSeqLen;
  for (uint8_t i = 0; i < toCopy; ++i) preview[k++] = _morseSeq[i];
  preview[k++] = ']';
  preview[k] = 0;

  drawStringWithPadding(_layout.morseIndicatorX,
                        _layout.morseIndicatorY,
                        preview,
                        WHITE, BLACK,
                        2, 8, 4, false);
}

void TextInputUI::morseHandleGaps() {
  unsigned long now = millis();
  if (_morsePressing) return;

  unsigned long idle = now - _lastMorseActivity;

  if (_morseSeqLen > 0) {
    if (idle >= _morseCfg.symbolGapMs) {
      if (morseDecodeAndCommit()) {
        drawInputText();
        drawRemainingLabel();
      } else {
        flashInputLine();
        clearMorseIndicatorArea();
      }
      morseResetSeq();
      _lastMorseActivity = now;
    }
  } else {
    // No pending seq; consider word gap => insert space
    if (idle >= _morseCfg.wordGapMs) {
      if (_inputMode == InputMode::MORSE) {
        // NEW GUARD: do not auto-insert space at the very start
        if (_inputLen > 0) {
          // If you’re using the single-space-per-pause guard, keep it here:
          if (!_insertedSpaceSinceLastActivity) {
            if (_inputLen < _maxLen) {
              _buf[_inputLen++] = ' ';
              _buf[_inputLen] = '\0';
              drawInputText();
              drawRemainingLabel();
              _insertedSpaceSinceLastActivity = true;
            } else {
              flashInputLine();
            }
          }
        }
      }
      _lastMorseActivity = now;
    }
  }
}

void TextInputUI::morseAppendDot() {
  if (_morseSeqLen < sizeof(_morseSeq) - 1) {
    _morseSeq[_morseSeqLen++] = '.';
    _morseSeq[_morseSeqLen] = 0;
    drawMorseIndicator();
  } else {
    flashInputLine();
    morseResetSeq();
    drawMorseIndicator();
  }
}

void TextInputUI::morseAppendDash() {
  if (_morseSeqLen < sizeof(_morseSeq) - 1) {
    _morseSeq[_morseSeqLen++] = '-';
    _morseSeq[_morseSeqLen] = 0;
    drawMorseIndicator();
  } else {
    flashInputLine();
    morseResetSeq();
    drawMorseIndicator();
  }
}

void TextInputUI::morseResetSeq() {
  _morseSeqLen = 0;
  _morseSeq[0] = 0;
  clearMorseIndicatorArea();
}

bool TextInputUI::morseDecodeAndCommit() {
  char c = morseDecode(_morseSeq);
  if (c == 0) return false;
  if (_inputLen < _maxLen) {
    _buf[_inputLen++] = c;
    _buf[_inputLen] = '\0';
    _lastBlinkMs = millis();
    _cursorVisible = true;
    return true;
  }
  return false;
}

char TextInputUI::morseDecode(const char* seq) const {
  struct Map { const char* code; char ch; };
  static const Map table[] PROGMEM = {
    {".-", 'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'}, {".", 'E'},
    {"..-.", 'F'}, {"--.", 'G'}, {"....", 'H'}, {"..", 'I'}, {".---", 'J'},
    {"-.-", 'K'}, {".-..", 'L'}, {"--", 'M'}, {"-.", 'N'}, {"---", 'O'},
    {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'}, {"...", 'S'}, {"-", 'T'},
    {"..-", 'U'}, {"...-", 'V'}, {".--", 'W'}, {"-..-", 'X'}, {"-.--", 'Y'},
    {"--..", 'Z'},
    {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'}, {"....-", '4'},
    {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'}, {"----.", '9'},
    {".-.-.-", '.'}, {"--..--", ','}, {"..--..", '?'}, {".----.", '\''},
    {"-.-.--", '!'}, {"-..-.", '/'}, {"-.--.", '('}, {"-.--.-", ')'},
    {".-...", '&'}, {"---...", ':'}, {"-.-.-.", ';'}, {"-...-", '='},
    {".-.-.", '+'}, {"-....-", '-'}, {"..--.-", '_'}, {".-..-.", '\"'},
    {".--.-.", '@'},{"...-..-", '$'}, { ".--.-", '>' },{ ".-.--", '<' }
  };

  for (uint16_t i = 0; i < sizeof(table)/sizeof(table[0]); ++i) {
    if (strcmp_P(seq, (PGM_P)table[i].code) == 0) return table[i].ch;
  }
  return 0;
}