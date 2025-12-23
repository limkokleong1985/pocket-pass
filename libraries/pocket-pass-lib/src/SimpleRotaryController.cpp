// SimpleRotaryController.cpp
#include "SimpleRotaryController.h"

static int8_t quadTable[16] = {
  0,  +1,  -1,   0,
  -1,  0,   0,  +1,
  +1,  0,   0,  -1,
   0,  -1, +1,   0
};

SimpleRotaryController::SimpleRotaryController() {}

void SimpleRotaryController::begin(uint8_t pinA, uint8_t pinB, uint8_t btnA, uint8_t btnB) {
  _pinA = pinA;
  _pinB = pinB;
  _btnA = btnA;
  _btnB = btnB;

  pinMode(_pinA, INPUT_PULLUP);
  pinMode(_pinB, INPUT_PULLUP);
  if (_btnA != 0xFF) pinMode(_btnA, INPUT_PULLUP);
  if (_btnB != 0xFF) pinMode(_btnB, INPUT_PULLUP);

  uint8_t a = (uint8_t)digitalRead(_pinA);
  uint8_t b = (uint8_t)digitalRead(_pinB);
  _lastState = (a << 1) | b;

  _stepAccum = 0;
  _count = 0;
  _turnCW = _turnCCW = false;

  // Initialize buttons A/B
  if (_btnA != 0xFF) {
    uint8_t r = (uint8_t)digitalRead(_btnA);
    _btnAStable = r;
    _btnARawPrev = r;
    _btnAChangedAt = millis();
  } else {
    _btnAStable = HIGH;
    _btnARawPrev = HIGH;
    _btnAChangedAt = 0;
  }

  if (_btnB != 0xFF) {
    uint8_t r = (uint8_t)digitalRead(_btnB);
    _btnBStable = r;
    _btnBRawPrev = r;
    _btnBChangedAt = millis();
  } else {
    _btnBStable = HIGH;
    _btnBRawPrev = HIGH;
    _btnBChangedAt = 0;
  }

  _btnAEventPress = _btnAEventRelease = false;
  _btnBEventPress = _btnBEventRelease = false;

  // Reset UP/DOWN related (they might be set in the 6-pin begin)
  _btnUp = 0xFF; _btnDown = 0xFF;
  _btnUpStable = HIGH; _btnDownStable = HIGH;
  _btnUpRawPrev = HIGH; _btnDownRawPrev = HIGH;
  _btnUpChangedAt = _btnDownChangedAt = 0;
  _btnUpEventPress = _btnUpEventRelease = _btnUpEventLong = false;
  _btnDownEventPress = _btnDownEventRelease = _btnDownEventLong = false;
  _btnUpPressedAt = _btnDownPressedAt = 0;
  _btnUpLastRepeatAt = _btnDownLastRepeatAt = 0;
  _btnUpLongFired = _btnDownLongFired = false;
}

void SimpleRotaryController::begin(uint8_t pinA, uint8_t pinB, uint8_t btnA, uint8_t btnB, uint8_t btnUp, uint8_t btnDown) {
  // Initialize base pins first
  begin(pinA, pinB, btnA, btnB);

  _btnUp = btnUp;
  _btnDown = btnDown;

  if (_btnUp != 0xFF) {
    pinMode(_btnUp, INPUT_PULLUP);
    uint8_t r = (uint8_t)digitalRead(_btnUp);
    _btnUpStable = r;
    _btnUpRawPrev = r;
    _btnUpChangedAt = millis();
    _btnUpEventPress = _btnUpEventRelease = _btnUpEventLong = false;
    _btnUpPressedAt = _btnUpLastRepeatAt = 0;
    _btnUpLongFired = false;
  } else {
    _btnUpStable = HIGH;
    _btnUpRawPrev = HIGH;
    _btnUpChangedAt = 0;
    _btnUpEventPress = _btnUpEventRelease = _btnUpEventLong = false;
    _btnUpPressedAt = _btnUpLastRepeatAt = 0;
    _btnUpLongFired = false;
  }

  if (_btnDown != 0xFF) {
    pinMode(_btnDown, INPUT_PULLUP);
    uint8_t r = (uint8_t)digitalRead(_btnDown);
    _btnDownStable = r;
    _btnDownRawPrev = r;
    _btnDownChangedAt = millis();
    _btnDownEventPress = _btnDownEventRelease = _btnDownEventLong = false;
    _btnDownPressedAt = _btnDownLastRepeatAt = 0;
    _btnDownLongFired = false;
  } else {
    _btnDownStable = HIGH;
    _btnDownRawPrev = HIGH;
    _btnDownChangedAt = 0;
    _btnDownEventPress = _btnDownEventRelease = _btnDownEventLong = false;
    _btnDownPressedAt = _btnDownLastRepeatAt = 0;
    _btnDownLongFired = false;
  }
}

void SimpleRotaryController::update() {
  // 1) Encoder decode
  uint8_t curr = readAB();
  uint8_t idx = (uint8_t)((_lastState << 2) | curr);
  int8_t step = quadTable[idx];

  if (step != 0) {
    _lastState = curr;
    if (_invert) step = -step;
    _stepAccum += step;
    const int E = (int)_edgesPerDetent;
    if (_stepAccum >= E) {
      _stepAccum -= E;
      applyDetentStep(+1);
    } else if (_stepAccum <= -E) {
      _stepAccum += E;
      applyDetentStep(-1);
    }
  }

  // 2) Non-blocking button debounce for A and B
  unsigned long now = millis();

  if (_btnA != 0xFF) {
    uint8_t raw = (uint8_t)digitalRead(_btnA);

    if (raw != _btnARawPrev) {
      // Raw level changed: start/restart debounce window
      _btnARawPrev = raw;
      _btnAChangedAt = now;
    } else {
      // Raw is same as previous; if stable long enough and differs from debounced, commit
      if ((unsigned long)(now - _btnAChangedAt) >= _debounceMs && raw != _btnAStable) {
        _btnAStable = raw;
        if (_btnAStable == LOW)  _btnAEventPress = true;
        else                     _btnAEventRelease = true;
      }
    }
  }

  if (_btnB != 0xFF) {
    uint8_t raw = (uint8_t)digitalRead(_btnB);

    if (raw != _btnBRawPrev) {
      _btnBRawPrev = raw;
      _btnBChangedAt = now;
    } else {
      if ((unsigned long)(now - _btnBChangedAt) >= _debounceMs && raw != _btnBStable) {
        _btnBStable = raw;
        if (_btnBStable == LOW)  _btnBEventPress = true;
        else                     _btnBEventRelease = true;
      }
    }
  }

  // 3) UP/DOWN substitute buttons: debounce + long-press + auto-repeat
  if (_btnUp != 0xFF) {
    uint8_t raw = (uint8_t)digitalRead(_btnUp);
    if (raw != _btnUpRawPrev) {
      _btnUpRawPrev = raw;
      _btnUpChangedAt = now;
    } else {
      if ((unsigned long)(now - _btnUpChangedAt) >= _debounceMs && raw != _btnUpStable) {
        _btnUpStable = raw;
        if (_btnUpStable == LOW) {
          // Pressed (debounced)
          _btnUpEventPress = true;
          _btnUpPressedAt = now;
          _btnUpLastRepeatAt = now;
          _btnUpLongFired = false;

          // Single step on press (tap)
          applyDetentStep(+1);
        } else {
          // Released
          _btnUpEventRelease = true;
          _btnUpLongFired = false;
        }
      }
    }

    // While held: long press detection and repeat
    if (_btnUpStable == LOW) {
      if (!_btnUpLongFired && (unsigned long)(now - _btnUpPressedAt) >= _longPressMs) {
        _btnUpLongFired = true;
        _btnUpEventLong = true;
      }
      if ((unsigned long)(now - _btnUpPressedAt) >= _repeatDelayMs) {
        if ((unsigned long)(now - _btnUpLastRepeatAt) >= _repeatPeriodMs) {
          _btnUpLastRepeatAt = now;
          applyDetentStep(+1);
        }
      }
    }
  }

  if (_btnDown != 0xFF) {
    uint8_t raw = (uint8_t)digitalRead(_btnDown);
    if (raw != _btnDownRawPrev) {
      _btnDownRawPrev = raw;
      _btnDownChangedAt = now;
    } else {
      if ((unsigned long)(now - _btnDownChangedAt) >= _debounceMs && raw != _btnDownStable) {
        _btnDownStable = raw;
        if (_btnDownStable == LOW) {
          // Pressed (debounced)
          _btnDownEventPress = true;
          _btnDownPressedAt = now;
          _btnDownLastRepeatAt = now;
          _btnDownLongFired = false;

          // Single step on press (tap)
          applyDetentStep(-1);
        } else {
          // Released
          _btnDownEventRelease = true;
          _btnDownLongFired = false;
        }
      }
    }

    // While held: long press detection and repeat
    if (_btnDownStable == LOW) {
      if (!_btnDownLongFired && (unsigned long)(now - _btnDownPressedAt) >= _longPressMs) {
        _btnDownLongFired = true;
        _btnDownEventLong = true;
      }
      if ((unsigned long)(now - _btnDownPressedAt) >= _repeatDelayMs) {
        if ((unsigned long)(now - _btnDownLastRepeatAt) >= _repeatPeriodMs) {
          _btnDownLastRepeatAt = now;
          applyDetentStep(-1);
        }
      }
    }
  }
}