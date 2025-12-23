
// RotaryMarqueeMenu.cpp
#include "RotaryMarqueeMenu.h"
#include <vector>

// ============== Public API ==============

void RotaryMarqueeMenu::begin(SimpleRotaryController& encoder,
                              uint8_t lcdOrientation,
                              uint8_t backlight) {
  // Bind external encoder (must already be initialized by caller)
  enc = &encoder;

  // Optional: If you want to apply these here (will affect shared encoder globally):
  // enc->setEdgesPerDetent(edgesPerDetent);
  // enc->setInvertDirection(invertEncoderDirection);

  // Display (leave here if you want Menu to own LCD init)
  LCD_Init();
  Set_Backlight(backlight);
  LCD_SetOrientation(lcdOrientation);
  LCD_Clear(BLACK);

  calcRowMetrics();
  drawStaticUI();

  topIndex = 0;
  selectedIndex = 0;
  drawVisibleWindow();
  prevSelectedIndex = selectedIndex;

  resetMarquee();
  resetSubMarquee();
}

void RotaryMarqueeMenu::loop() {
  if (!enc) return;
  enc->update();

  bool moved = false;

  if (!invertDir_) {
    // Normal direction
    if (enc->wasTurnedCW()) {
      if (menuCount > 0 && selectedIndex < (int8_t)menuCount - 1) {
        selectedIndex++;
        moved = true;
      }
    }
    if (enc->wasTurnedCCW()) {
      if (menuCount > 0 && selectedIndex > 0) {
        selectedIndex--;
        moved = true;
      }
    }
  } else {
    // Inverted direction
    if (enc->wasTurnedCW()) {
      if (menuCount > 0 && selectedIndex > 0) {
        selectedIndex--;
        moved = true;
      }
    }
    if (enc->wasTurnedCCW()) {
      if (menuCount > 0 && selectedIndex < (int8_t)menuCount - 1) {
        selectedIndex++;
        moved = true;
      }
    }
  }

  if (moved) {
    int8_t oldTop = topIndex;
    ensureSelectionVisible(); // may change topIndex

    if (topIndex != oldTop) {
      drawVisibleWindow();
    } else {
      redrawSelectionInWindow(prevSelectedIndex, selectedIndex);
    }
    prevSelectedIndex = selectedIndex;
    resetMarquee();  // re-enable row marquee
  }

  // Buttons
  if (enc->wasPressedB()) handleSelect(selectedIndex); // Select
  if (enc->wasPressedA()) handleBack();                // Back

  // Row marquee tick
  maybeStartOrStepMarquee();

  // Subtitle marquee tick
  maybeStartOrStepSubMarquee();
}

void RotaryMarqueeMenu::setMenu(const char* items[], uint8_t count) {
  menuItems = items;
  menuCount = count;

  // Reset selection bookkeeping for a fresh menu
  if (menuCount == 0) {
    selectedIndex = -1;
    prevSelectedIndex = -1;
    topIndex = 0;
  } else {
    if (selectedIndex < 0 || selectedIndex >= menuCount) selectedIndex = 0;
    prevSelectedIndex = selectedIndex; // align so no stray redraws
    ensureSelectionVisible();
  }

  drawVisibleWindow();
  resetMarquee();
  // Subtitle marquee independent, keep running
}

void RotaryMarqueeMenu::setTitle(const char* t) {
  title = t ? t : "";
  redrawHeader();
}

void RotaryMarqueeMenu::setSubTitle(const char* st) {
  subTitle = st ? st : "";
  // Reset subtitle marquee state whenever subtitle changes
  resetSubMarquee();
  redrawHeader();
}

void RotaryMarqueeMenu::setSelectedIndex(int8_t idx) {
  if (menuCount == 0) return;

  if (idx < 0) idx = 0;
  if (idx >= menuCount) idx = menuCount - 1;
  if (idx == selectedIndex) { resetMarquee(); return; }

  int8_t oldSelected = selectedIndex;
  int8_t oldTop = topIndex;

  selectedIndex = idx;
  ensureSelectionVisible();

  if (topIndex != oldTop) {
    // Window moved; repaint visible window
    drawVisibleWindow();
  } else {
    // Window stayed; repaint only old and new rows if they’re visible
    if (oldSelected >= topIndex && oldSelected < topIndex + visibleRows) {
      drawVisibleItem(oldSelected - topIndex, oldSelected, false);
    }
    if (selectedIndex >= topIndex && selectedIndex < topIndex + visibleRows) {
      drawVisibleItem(selectedIndex - topIndex, selectedIndex, true);
    }
  }

  prevSelectedIndex = selectedIndex;
  resetMarquee();
}

void RotaryMarqueeMenu::redrawHeader() {
  // Clear header band and redraw stripes and text
  LCD_FillRect(0, startY - 2, LCD_Width(), 46, BLACK);
  LCD_DrawLine(0, startY + 8,  LCD_Width() - 1, startY + 8,  WHITE);
  LCD_DrawLine(0, startY + 10, LCD_Width() - 1, startY + 10, WHITE);

  // Title
  if (!title.isEmpty()) {
    drawStringWithPadding(startX + 7, startY, title.c_str(), WHITE, BLACK, 2, 8, 4, false);
  }

  // Subtitle
  uint16_t subY = startY + 25;
  LCD_FillRect(startX + 20, subY, LCD_Width() - (startX + 20), 20, BLACK); // clear subtitle area first
  if (!subTitle.isEmpty()) {
    if ((uint16_t)subTitle.length() <= subMarqueeThreshold) {
      drawStringWithPadding(startX + 20, subY, subTitle.c_str(), WHITE, BLACK, 2, 8, 4, false);
    } else {
      drawSubtitleFrame(); // first marquee frame handles long subtitle
    }
  }
}

// ============== Private helpers ==============

void RotaryMarqueeMenu::calcRowMetrics() {
  MENU_ROW_X  = mStartX;
  MENU_TEXT_X = mStartX + 25;
  // Use LCD_Width() if available
  uint16_t lcdW = LCD_Width();
  MENU_ROW_W  = (uint16_t)(lcdW) - MENU_ROW_X - 2;
  MENU_ROW_H  = 20;
  MENU_CURSOR_W = 20;
}

void RotaryMarqueeMenu::drawStaticUI() {
  // Header lines
  LCD_DrawLine(0, startY + 8,  LCD_Width() - 1, startY + 8,  WHITE);
  LCD_DrawLine(0, startY + 10, LCD_Width() - 1, startY + 10, WHITE);

  // Title (skip when empty)
  if (!title.isEmpty()) {
    drawStringWithPadding(startX + 7, startY, title.c_str(), WHITE, BLACK, 2, 8, 4, false);
  }

  // Subtitle (skip when empty)
  if (!subTitle.isEmpty()) {
    if ((uint16_t)subTitle.length() <= subMarqueeThreshold) {
      drawStringWithPadding(startX + 20, startY + 25, subTitle.c_str(), WHITE, BLACK, 2, 8, 4, false);
    } else {
      // Clear area; first marquee frame will draw when ticking
      uint16_t subY = startY + 25;
      LCD_FillRect(startX + 20, subY, LCD_Width() - (startX + 20), 20, BLACK);
    }
  } else {
    // Ensure subtitle area is cleared when empty
    uint16_t subY = startY + 25;
    LCD_FillRect(startX + 20, subY, LCD_Width() - (startX + 20), 20, BLACK);
  }
}

void RotaryMarqueeMenu::drawVisibleWindow() {
  for (uint8_t vis = 0; vis < visibleRows; vis++) {
    uint8_t absIdx = topIndex + vis;
    if (absIdx < menuCount) {
      bool sel = (absIdx == selectedIndex);
      drawVisibleItem(vis, absIdx, sel);
    } else {
      clearRow(vis, BLACK);
    }
  }
}

void RotaryMarqueeMenu::drawVisibleItem(uint8_t visRow, uint8_t absIndex, bool selected) {
  clearRow(visRow, selected ? WHITE : BLACK); // selected rows WHITE BG for inverted text
  uint16_t y = rowY(visRow);

  // Cursor
  drawStringWithPadding(mStartX, y, selected ? "> " : "  ", WHITE, BLACK, 2, 8, 4, false);

  const char* label = itemAt(absIndex);
  if (selected) {
    drawStringWithPadding(MENU_TEXT_X, y, label, BLACK, WHITE, 2, 8, 4, false);
  } else {
    drawStringWithPadding(MENU_TEXT_X, y, label, WHITE, BLACK, 2, 8, 4, false);
  }
}

void RotaryMarqueeMenu::clearRow(uint8_t visRow, uint16_t bgColor) {
  // Slightly taller to cover font ascent/descenders
  LCD_FillRect(MENU_ROW_X, rowY(visRow) - 4, 300, MENU_ROW_H + 4, bgColor);
}

void RotaryMarqueeMenu::clearTextArea(uint8_t visRow, uint16_t bgColor) {
  uint16_t y = rowY(visRow);
  uint16_t w = MENU_ROW_W - (MENU_TEXT_X - MENU_ROW_X);
  LCD_FillRect(MENU_TEXT_X, y, w, MENU_ROW_H, bgColor);
}

void RotaryMarqueeMenu::ensureSelectionVisible() {
  if (menuCount == 0) { topIndex = 0; return; }
  if (selectedIndex < topIndex) {
    topIndex = selectedIndex;
  }
  if (selectedIndex >= topIndex + visibleRows) {
    topIndex = selectedIndex - (visibleRows - 1);
  }
  int8_t maxTop = max(0, (int)menuCount - (int)visibleRows);
  if (topIndex < 0) topIndex = 0;
  if (topIndex > maxTop) topIndex = maxTop;
}

void RotaryMarqueeMenu::redrawSelectionInWindow(int8_t oldAbsIdx, int8_t newAbsIdx) {
  if (oldAbsIdx >= 0 && oldAbsIdx >= topIndex && oldAbsIdx < topIndex + visibleRows) {
    uint8_t visRow = oldAbsIdx - topIndex;
    drawVisibleItem(visRow, oldAbsIdx, false);
  }
  if (newAbsIdx >= 0 && newAbsIdx >= topIndex && newAbsIdx < topIndex + visibleRows) {
    uint8_t visRow = newAbsIdx - topIndex;
    drawVisibleItem(visRow, newAbsIdx, true);
  }
}

// Actions
void RotaryMarqueeMenu::handleSelect(uint8_t index) {
  if (menuCount == 0) return;

  // Clear marquee and force the old selection row to redraw as unselected
  resetMarquee();
  if (prevSelectedIndex < 0) prevSelectedIndex = selectedIndex; // bootstrap
  if (prevSelectedIndex >= topIndex && prevSelectedIndex < topIndex + visibleRows) {
    uint8_t visOld = prevSelectedIndex - topIndex;
    drawVisibleItem(visOld, prevSelectedIndex, false);
  }

  // Now the current row should be drawn as selected (in case callback doesn't change menu)
  if (selectedIndex >= topIndex && selectedIndex < topIndex + visibleRows) {
    uint8_t visNew = selectedIndex - topIndex;
    drawVisibleItem(visNew, selectedIndex, true);
  }

  // Update bookkeeping BEFORE callback
  prevSelectedIndex = selectedIndex;

  // Invoke callback (may call setMenu)
  if (onSelectCb) onSelectCb(index, itemAt(index));

  // After callback, ensure window is consistent (new menu/items/selection)
  ensureSelectionVisible();
  drawVisibleWindow();
}

void RotaryMarqueeMenu::handleBack() {
  if (menuCount == 0) return;

  resetMarquee();

  // Same immediate cleanup as select
  if (prevSelectedIndex < 0) prevSelectedIndex = selectedIndex;
  if (prevSelectedIndex >= topIndex && prevSelectedIndex < topIndex + visibleRows) {
    uint8_t visOld = prevSelectedIndex - topIndex;
    drawVisibleItem(visOld, prevSelectedIndex, false);
  }
  if (selectedIndex >= topIndex && selectedIndex < topIndex + visibleRows) {
    uint8_t visNew = selectedIndex - topIndex;
    drawVisibleItem(visNew, selectedIndex, true);
  }
  prevSelectedIndex = selectedIndex;

  if (onBackCb) onBackCb();

  ensureSelectionVisible();
  drawVisibleWindow();
}

// ===== Row (menu items) Marquee =====
void RotaryMarqueeMenu::resetMarquee() {
  marqueeActive = false;
  marqueeOffset = 0;
  marqueeLoop = "";
  marqueeLoopLen = 0;
  marqueeStartTime = millis();
  lastMarqueeStep = millis();
}

void RotaryMarqueeMenu::maybeStartOrStepMarquee() {
  if (menuCount == 0) return;

  // Only if selected row is visible
  if (!(selectedIndex >= topIndex && selectedIndex < topIndex + visibleRows)) return;

  const char* text = itemAt(selectedIndex);
  uint16_t len = strlen(text);
  if (len <= marqueeThreshold) return;

  unsigned long now = millis();

  if (!marqueeActive) {
    if (now - marqueeStartTime < marqueeStartDelayMs) return;

    marqueeLoop = String(text) + MARQUEE_GAP + String(text) + MARQUEE_GAP;
    marqueeLoopLen = marqueeLoop.length();

    marqueeWindowChars = max<uint8_t>(marqueeThreshold, 21);
    marqueeOffset = 0;
    marqueeActive = true;
    lastMarqueeStep = now;
  }

  if (now - lastMarqueeStep < marqueeStepIntervalMs) return;
  lastMarqueeStep = now;

  marqueeOffset++;
  if (marqueeOffset >= marqueeLoopLen) {
    marqueeOffset = 0;
    marqueeActive = false;
    marqueeStartTime = now;
    // Draw one frame at offset 0 immediately
    if (selectedIndex >= topIndex && selectedIndex < topIndex + visibleRows) {
      uint8_t visRow = selectedIndex - topIndex;
      drawSelectedWithMarquee(visRow);
    }
    return;
  }

  // Draw current marquee frame
  uint8_t visRow = selectedIndex - topIndex;
  drawSelectedWithMarquee(visRow);
}

void RotaryMarqueeMenu::drawSelectedWithMarquee(uint8_t visRow) {
  if (marqueeLoopLen == 0) return;

  const uint8_t MAX_BUF = 64;
  uint8_t n = marqueeWindowChars;
  if (n > MAX_BUF - 1) n = MAX_BUF - 1;

  char buf[MAX_BUF];
  for (uint8_t i = 0; i < n; i++) {
    uint16_t idx = (marqueeOffset + i) % marqueeLoopLen;
    buf[i] = marqueeLoop[idx];
  }
  buf[n] = '\0';

  // Clear only text area with WHITE (selected bg), keep cursor area
  clearTextArea(visRow, WHITE);

  uint16_t y = rowY(visRow);

  // Ensure cursor is visible
  drawStringWithPadding(mStartX, y, "> ", WHITE, BLACK, 2, 8, 4, false);

  // Draw marquee slice (inverted)
  drawStringWithPadding(MENU_TEXT_X, y, buf, BLACK, WHITE, 2, 8, 4, false);
}

// ===== Subtitle Marquee =====
void RotaryMarqueeMenu::resetSubMarquee() {
  subMarqueeActive = false;
  subMarqueeOffset = 0;
  subMarqueeLoop = "";
  subMarqueeLoopLen = 0;
  subMarqueeStartTime = millis();
  lastSubMarqueeStep = millis();
}

void RotaryMarqueeMenu::maybeStartOrStepSubMarquee() {
  // If subtitle is short, nothing to do
  if ((uint16_t)subTitle.length() <= subMarqueeThreshold) return;

  unsigned long now = millis();

  if (!subMarqueeActive) {
    if (now - subMarqueeStartTime < subMarqueeStartDelayMs) return;

    // Build loop
    subMarqueeLoop = subTitle + String(SUB_MARQUEE_GAP) + subTitle + String(SUB_MARQUEE_GAP);
    subMarqueeLoopLen = subMarqueeLoop.length();
    subMarqueeOffset = 0;
    subMarqueeActive = true;
    lastSubMarqueeStep = now;

    // Draw first frame immediately
    drawSubtitleFrame();
    return;
  }

  if (now - lastSubMarqueeStep < subMarqueeStepIntervalMs) return;
  lastSubMarqueeStep = now;

  subMarqueeOffset++;
  if (subMarqueeOffset >= subMarqueeLoopLen) {
    subMarqueeOffset = 0;
    subMarqueeActive = false;
    subMarqueeStartTime = now;

    // Draw one frame at offset 0
    drawSubtitleFrame();
    return;
  }

  drawSubtitleFrame();
}

void RotaryMarqueeMenu::drawSubtitleFrame() {
  // Subtitle line geometry
  uint16_t x = startX + 20;
  uint16_t y = startY + 25;

  // Clear the subtitle text area (keep header background as BLACK)
  LCD_FillRect(x, y, LCD_Width() - x, 20, BLACK);

  // If short, just draw normally
  if ((uint16_t)subTitle.length() <= subMarqueeThreshold) {
    drawStringWithPadding(x, y, subTitle.c_str(), WHITE, BLACK, 2, 8, 4, false);
    return;
  }

  // Draw marquee slice
  const uint8_t MAX_BUF = 80; // subtitle slice buffer
  uint8_t windowChars = 28;
  if (windowChars > MAX_BUF - 1) windowChars = MAX_BUF - 1;

  if (subMarqueeLoopLen == 0) {
    // Safety: build loop if not built (should be built by maybeStartOrStepSubMarquee)
    subMarqueeLoop = subTitle + String(SUB_MARQUEE_GAP) + subTitle + String(SUB_MARQUEE_GAP);
    subMarqueeLoopLen = subMarqueeLoop.length();
  }

  char buf[MAX_BUF];
  for (uint8_t i = 0; i < windowChars; i++) {
    uint16_t idx = (subMarqueeOffset + i) % subMarqueeLoopLen;
    buf[i] = subMarqueeLoop[idx];
  }
  buf[windowChars] = '\0';

  drawStringWithPadding(x, y, buf, WHITE, BLACK, 2, 8, 4, false);
}

void RotaryMarqueeMenu::clearScreen(uint16_t bgColor) {
  // Clear the entire screen
  LCD_Clear(bgColor);

  // Keep header area BLACK for readability
  LCD_FillRect(0, startY - 2, LCD_Width(), 46, BLACK);

  // Recompute row metrics in case dimensions/orientation changed
  calcRowMetrics();

  // Redraw static UI (header lines, title, subtitle)
  drawStaticUI();

  // Clamp and ensure the current selection is visible
  ensureSelectionVisible();

  // Redraw the visible menu area
  drawVisibleWindow();

  // Reset marquees timing/state
  resetMarquee();
  resetSubMarquee();

  // Keep prevSelectedIndex consistent
  prevSelectedIndex = selectedIndex;
}

// ===== Internal: wrap text and modal =====
void RotaryMarqueeMenu::wrapTextNoBreak(const String& text, uint8_t maxWidth, std::vector<String>& outLines) {
  outLines.clear();

  size_t i = 0;
  const size_t n = text.length();

  auto readToken = [&](String& tok)->bool {
    while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r')) i++;
    if (i < n && text[i] == '\n') { i++; tok = "\n"; return true; }
    if (i >= n) return false;

    size_t start = i;
    while (i < n && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n') i++;
    tok = text.substring(start, i);
    return true;
  };

  String current;
  auto flushLine = [&]() {
    if (current.length() > 0) {
      outLines.push_back(current);
      current = "";
    }
  };

  String tok;
  while (readToken(tok)) {
    if (tok == "\n") {
      flushLine();
      continue;
    }

    if ((uint8_t)tok.length() > maxWidth) {
      flushLine();
      size_t pos = 0;
      while (pos < tok.length()) {
        size_t len = std::min((size_t)maxWidth, (size_t)(tok.length() - pos));
        outLines.push_back(tok.substring(pos, pos + len));
        pos += len;
      }
      continue;
    }

    if (current.length() == 0) {
      current = tok;
      continue;
    }

    if ((uint8_t)(current.length() + 1 + tok.length()) > maxWidth) {
      flushLine();
      current = tok;
    } else {
      current += " ";
      current += tok;
    }
  }
  flushLine();

  if (outLines.empty()) outLines.push_back("");
}

bool RotaryMarqueeMenu::showInfoModal(const char* ttl,
                                      const char* sub,
                                      const String& longText,
                                      const char* finalAction,
                                      uint8_t maxWidth,
                                      bool disableBack) {
  // Wrap the long text into menu rows
  std::vector<String> lines;
  wrapTextNoBreak(longText, maxWidth, lines);

  // Build pointers array (truncate to fit)
  static const uint8_t MAX_ITEMS = 64;
  static const char* itemsBuf[MAX_ITEMS];
  uint8_t count = 0;

  for (size_t k = 0; k < lines.size() && count + 1 < MAX_ITEMS; ++k) {
    itemsBuf[count++] = lines[k].c_str();
  }
  itemsBuf[count++] = finalAction && finalAction[0] ? finalAction : "[ NEXT ]";

  // Save state that the app might have set
  SelectCallback prevSelect = onSelectCb;
  BackCallback   prevBack   = onBackCb;
  int8_t         prevSelIdx = selectedIndex;

  // Render
  clearScreen(BLACK);
  setTitle(ttl ? ttl : "");
  setSubTitle(sub ? sub : "");
  setMenu(itemsBuf, count);
  setSelectedIndex(0);

  bool done = false;
  bool ok   = false;

  // Modal input loop using encoder directly
  while (!done) {
    if (!enc) break;
    enc->update();

    // Navigation
    bool moved = false;
    if (!invertDir_) {
      if (enc->wasTurnedCW()) {
        if (selectedIndex < (int8_t)count - 1) { setSelectedIndex(selectedIndex + 1); moved = true; }
      }
      if (enc->wasTurnedCCW()) {
        if (selectedIndex > 0) { setSelectedIndex(selectedIndex - 1); moved = true; }
      }
    } else {
      if (enc->wasTurnedCW()) {
        if (selectedIndex > 0) { setSelectedIndex(selectedIndex - 1); moved = true; }
      }
      if (enc->wasTurnedCCW()) {
        if (selectedIndex < (int8_t)count - 1) { setSelectedIndex(selectedIndex + 1); moved = true; }
      }
    }

    // Accept on Select only if on final item
    if (enc->wasPressedB()) {
      if (selectedIndex == (int8_t)(count - 1)) {
        ok = true;
        done = true;
      }
      // otherwise ignore presses on text rows
    }

    // Back to exit (if allowed)
    if (!disableBack && enc->wasPressedA()) {
      ok = false;
      done = true;
    }

    // Step subtitle marquee if needed
    maybeStartOrStepSubMarquee();

    delay(5);
  }

  // Restore callbacks (visual content is intentionally kept; callers typically rebuild their screens)
  onSelectCb = prevSelect;
  onBackCb   = prevBack;
  selectedIndex = prevSelIdx; // selection index will be reset when caller rebuilds

  return ok;
}