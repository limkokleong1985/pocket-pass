

// TextInputUI.h
#pragma once
#include <Arduino.h>
#include <Display_ST7789.h>
#include "SimpleRotaryController.h"

class TextInputUI {
public:
  enum class InputMode : uint8_t { STANDARD = 0, PASSCODE, INTEGER, MORSE };

  struct Layout {
    uint16_t startX = 15, startY = 10;
    uint16_t inputStartX = 15, inputStartY = 130;
    uint16_t scrollPickX = 250, scrollPickGap = 30, offsetY = 20;
    uint16_t morseIndicatorX = 15;   // fixed pos for morse indicator
    uint16_t morseIndicatorY = 100;
  };

  struct MorseConfig {
    uint16_t dotMaxMs = 200;     // <= dotMaxMs -> dot
    uint16_t dashMinMs = 400;    // >= dashMinMs -> dash
    uint16_t symbolGapMs = 600;  // idle to end-of-character
    uint16_t wordGapMs = 1400;   // idle to auto-insert space
  };

  TextInputUI(SimpleRotaryController& encoder,
              const char* title,
              const char* description,
              uint8_t maxLen,
              InputMode inputMode = InputMode::STANDARD);

  TextInputUI(const char* title,
              const char* description,
              uint8_t maxLen,
              InputMode inputMode = InputMode::STANDARD);

  void begin(SimpleRotaryController& encoder);
  void begin();    // only if constructed with encoder reference
  void update();

  void setTitle(const char* t);
  void setDescription(const char* d);
  void setMaxLen(uint8_t m);
  void setInputInvert(bool invert);
  void setInputMode(InputMode m);
  uint8_t getMaxLen() const { return _maxLen; }
  uint8_t length() const { return _inputLen; }
  const char* c_str() const { return _buf; }

  void setOnSave(void (*cb)(const char* s)) { _onSave = cb; }
  void setOnCancel(void (*cb)()) { _onCancel = cb; }

  void setMorseConfig(const MorseConfig& cfg) { _morseCfg = cfg; }
  MorseConfig getMorseConfig() const { return _morseCfg; }

  void clear();
  Layout& layout() { return _layout; }

private:
  enum class PickerMode : uint8_t {
    UPPER = 0, LOWER, NUM, SYM, ACTIONS, SAVE,
    INTNUM,
    ACTIONS_INT,
    MORSE_ACTIONS,
    COUNT
  };

  SimpleRotaryController* _encPtr = nullptr;
  SimpleRotaryController* _encCtorPtr = nullptr;

  const char* _title;
  const char* _description;
  uint8_t _maxLen;
  InputMode _inputMode;

  bool _inputInvert = false;

  static const uint8_t MAX_CAP = 64;
  char _buf[MAX_CAP + 1] = {0};
  uint8_t _inputLen = 0;

  bool _cursorVisible = true;
  unsigned long _lastBlinkMs = 0;
  static const unsigned long BLINK_INTERVAL_MS = 500;

  PickerMode _pickerMode = PickerMode::UPPER;
  int16_t _activeIndex = 0;

  Layout _layout;

  static const uint8_t  INPUT_FONT_SIZE = 2;
  static const uint16_t CHAR_ADV       = 6 * INPUT_FONT_SIZE;
  static const uint16_t CHAR_BODY_W    = 5 * INPUT_FONT_SIZE;
  static const uint16_t LINE_H         = 8 * INPUT_FONT_SIZE;
  static const uint16_t INPUT_TEXT_PAD_X = 8;
  static const uint16_t INPUT_TEXT_PAD_Y = 4;

  void (*_onSave)(const char* s) = nullptr;
  void (*_onCancel)() = nullptr;

  // Morse state
  MorseConfig _morseCfg;
  bool _morsePressing = false;
  unsigned long _morsePressStart = 0;
  char _morseSeq[8] = {0}; // up to 7 elements + NUL
  uint8_t _morseSeqLen = 0;
  unsigned long _lastMorseActivity = 0;
  bool _insertedSpaceSinceLastActivity = false;

  // Optional: if using isPressedA() instead of wasReleasedA()
  bool _aPrev = false; // ALT RELEASE DETECTION: set true if using edge detect via isPressedA()

private:
  void drawStaticUI();
  void drawInputText();
  void drawScroller();
  void drawModeHeader();
  void drawRemainingLabel();

  void nextPickerMode();
  void doSelect();
  void flashInputLine();
  void updateCursorBlink();
  void getInputTextOrigin(uint16_t& baseX, uint16_t& baseY) const;
  void clearRightPaneContent();

  struct UICaretRect { uint16_t x, y, w, h; };
  UICaretRect getCaretRectThin() const;
  void drawCaret(bool visible);
  static uint16_t textPixelWidthFs1(const char* s);

  static constexpr const char SYM_ARR[] = "!@#$%^&*()-_=+[]{};:'\",.<>/?\\|";
  static constexpr const char* SYM_SET = SYM_ARR;
  static constexpr uint8_t SYM_LEN = sizeof(SYM_ARR) - 1;

  uint8_t modeCount(PickerMode m) const;
  char charAt(PickerMode m, int16_t idx) const;
  const char* actionLabel(PickerMode m, int16_t idx) const;

  static const uint8_t VISIBLE_CHARS = 16;

  // Morse helpers
  void morseHandlePressLogic();
  void morseHandleGaps();
  void morseAppendDot();
  void morseAppendDash();
  void morseResetSeq();
  bool morseDecodeAndCommit();
  char morseDecode(const char* seq) const;
  void drawMorseIndicator();
  void clearMorseIndicatorArea();
};