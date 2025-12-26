//SimpleRotaryController.h
#pragma once
#include <Arduino.h>

class SimpleRotaryController {
public:
  SimpleRotaryController();

  // Provide pins on begin; all inputs will be set to INPUT_PULLUP
  void begin(uint8_t pinA, uint8_t pinB, uint8_t btnA, uint8_t btnB);

  // Overload: also provide UP and DOWN substitute buttons
  void begin(uint8_t pinA, uint8_t pinB, uint8_t btnA, uint8_t btnB, uint8_t btnUp, uint8_t btnDown);

  // Call this frequently (each loop). Handles decoding and debouncing.
  void update();

  // Counter API
  int getCount() const { return _count; }
  void setCount(int v) { _count = v; _stepAccum = 0; }

  // Turn edge flags (true only on the loop after a detected detent)
  bool wasTurnedCW()  { bool f = _turnCW;  _turnCW = false;  return f; }
  bool wasTurnedCCW() { bool f = _turnCCW; _turnCCW = false; return f; }

  // Button A state/edges
  bool isPressedA() const { return _btnAStable == LOW; }
  bool wasPressedA()  { bool f = _btnAEventPress;   _btnAEventPress = false;   return f; }
  bool wasReleasedA() { bool f = _btnAEventRelease; _btnAEventRelease = false; return f; }

  // Button B state/edges
  bool isPressedB() const { return _btnBStable == LOW; }
  bool wasPressedB()  { bool f = _btnBEventPress;   _btnBEventPress = false;   return f; }
  bool wasReleasedB() { bool f = _btnBEventRelease; _btnBEventRelease = false; return f; }

  // UP/DOWN substitute buttons (optional)
  bool isPressedUp() const { return _btnUpStable == LOW; }
  bool wasPressedUp()  { bool f = _btnUpEventPress;   _btnUpEventPress = false;   return f; }
  bool wasReleasedUp() { bool f = _btnUpEventRelease; _btnUpEventRelease = false; return f; }
  bool wasLongPressedUp() { bool f = _btnUpEventLong; _btnUpEventLong = false; return f; }

  bool isPressedDown() const { return _btnDownStable == LOW; }
  bool wasPressedDown()  { bool f = _btnDownEventPress;   _btnDownEventPress = false;   return f; }
  bool wasReleasedDown() { bool f = _btnDownEventRelease; _btnDownEventRelease = false; return f; }
  bool wasLongPressedDown() { bool f = _btnDownEventLong; _btnDownEventLong = false; return f; }

  // Tuning: set how many valid edges per detent (default internal = 4)
  void setEdgesPerDetent(uint8_t edges) { _edgesPerDetent = edges ? edges : 4; }

  // Optionally invert direction if needed
  void setInvertDirection(bool inv) { _invert = inv; }

  // Set debounce interval (ms) for buttons (default 20)
  void setButtonDebounceMs(uint16_t ms) { _debounceMs = ms; }

  // Long-press threshold (ms) for UP/DOWN (default 600)
  void setLongPressMs(uint16_t ms) { _longPressMs = ms; }

  // Auto-repeat timing for UP/DOWN (default delay 400ms, repeat every 120ms)
  void setRepeatTimings(uint16_t startDelayMs, uint16_t repeatMs) {
    _repeatDelayMs = startDelayMs;
    _repeatPeriodMs = repeatMs ? repeatMs : 1;
  }

private:
  // Pins
  uint8_t _pinA = 0xFF, _pinB = 0xFF, _btnA = 0xFF, _btnB = 0xFF;
  uint8_t _btnUp = 0xFF, _btnDown = 0xFF;

  // Encoder state
  uint8_t _lastState = 0; // last stable 2-bit AB
  int8_t  _stepAccum = 0; // accumulates +1/-1 per valid edge until +/-edgesPerDetent
  int     _count = 0;
  uint8_t _edgesPerDetent = 4;
  bool    _invert = false;

  // Turn edge flags
  bool _turnCW = false, _turnCCW = false;

  // Button debouncing
  uint16_t _debounceMs = 20;

  // A/B debounced states and debouncing helpers
  uint8_t _btnAStable = HIGH, _btnBStable = HIGH;     // debounced state
  uint8_t _btnARawPrev = HIGH, _btnBRawPrev = HIGH;   // last raw read
  unsigned long _btnAChangedAt = 0, _btnBChangedAt = 0;

  // Edge flags after debounced state updates (A/B)
  bool _btnAEventPress = false, _btnAEventRelease = false;
  bool _btnBEventPress = false, _btnBEventRelease = false;

  // UP/DOWN debounced states and debouncing helpers
  uint8_t _btnUpStable = HIGH, _btnDownStable = HIGH;     // debounced state
  uint8_t _btnUpRawPrev = HIGH, _btnDownRawPrev = HIGH;   // last raw read
  unsigned long _btnUpChangedAt = 0, _btnDownChangedAt = 0;

  // UP/DOWN long-press and repeat
  uint16_t _longPressMs = 600;
  uint16_t _repeatDelayMs = 400;
  uint16_t _repeatPeriodMs = 120;

  unsigned long _btnUpPressedAt = 0, _btnDownPressedAt = 0;
  unsigned long _btnUpLastRepeatAt = 0, _btnDownLastRepeatAt = 0;
  bool _btnUpLongFired = false, _btnDownLongFired = false;

  // UP/DOWN edge flags
  bool _btnUpEventPress = false, _btnUpEventRelease = false, _btnUpEventLong = false;
  bool _btnDownEventPress = false, _btnDownEventRelease = false, _btnDownEventLong = false;

  inline uint8_t readAB() const {
    uint8_t a = (uint8_t)digitalRead(_pinA);
    uint8_t b = (uint8_t)digitalRead(_pinB);
    return (a << 1) | b;
  }

  // Apply one "detent step" (+1 CW or -1 CCW) and set flags
  inline void applyDetentStep(int dir) {
    if (dir > 0) {
      _count++;
      _turnCW = true;
    } else if (dir < 0) {
      _count--;
      _turnCCW = true;
    }
  }
};