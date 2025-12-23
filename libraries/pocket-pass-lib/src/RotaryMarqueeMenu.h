// RotaryMarqueeMenu.h
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Display_ST7789.h>
#include "SimpleRotaryController.h"

class RotaryMarqueeMenu {
public:
  // Geometry/layout config (tweakable before begin)
  uint16_t startX = 15, startY = 10;
  uint16_t mStartX = 35, mStartY = 60, mGap = 30;

  // Visible rows
  uint8_t visibleRows = 4;

  // Encoder config (applied by caller; here only stored for convenience)
  uint8_t edgesPerDetent = 4;
  bool invertEncoderDirection = false;

  // Row marquee (menu items) config
  uint8_t  marqueeThreshold      = 21;     // characters for row marquee
  uint16_t marqueeStartDelayMs   = 1000;   // ms
  uint16_t marqueeStepIntervalMs = 400;    // ms

  // Subtitle marquee config
  uint8_t  subMarqueeThreshold      = 25;   // characters before subtitle scrolls
  uint16_t subMarqueeStartDelayMs   = 1000; // ms
  uint16_t subMarqueeStepIntervalMs = 400;  // ms
  const char* SUB_MARQUEE_GAP = "          "; // 10 spaces

  // Callbacks
  typedef void (*SelectCallback)(uint8_t index, const char* label);
  typedef void (*BackCallback)();
  void setOnSelect(SelectCallback cb) { onSelectCb = cb; }
  void setOnBack(BackCallback cb) { onBackCb = cb; }

  // Life-cycle: pass an already-initialized encoder
  void begin(SimpleRotaryController& encoder,
             uint8_t lcdOrientation = 3, uint8_t backlight = 80);

  // Call in loop()
  void loop();

  // Data setters
  void setMenu(const char* items[], uint8_t count);
  void setTitle(const char* t);
  void setSubTitle(const char* st);

  // Selection helpers
  int8_t getSelectedIndex() const { return selectedIndex; }
  void setSelectedIndex(int8_t idx);

  // Redraw header if title/subtitle changed externally
  void redrawHeader();
  // Clear the display and redraw UI/menu
  void clearScreen(uint16_t bgColor = BLACK);
  void setInvertDirection(bool inv) { invertDir_ = inv; }

  // Reusable long-text modal (wrapped info screen)
  // Returns true if user selects finalAction; false if user presses Back (when not disabled).
  bool showInfoModal(const char* title,
                     const char* subtitle,
                     const String& longText,
                     const char* finalAction = "[ NEXT ]",
                     uint8_t maxWidth = 21,
                     bool disableBack = false);

private:
  // External encoder pointer (not owned)
  SimpleRotaryController* enc = nullptr;
  bool invertDir_ = false;
  const char** menuItems = nullptr;
  uint8_t menuCount = 0;

  String title = "";
  String subTitle = "";

  // Row geometry derived
  uint16_t MENU_ROW_X    = 0;
  uint16_t MENU_TEXT_X   = 0;
  uint16_t MENU_ROW_W    = 0;
  uint16_t MENU_ROW_H    = 20;
  uint16_t MENU_CURSOR_W = 20;

  // Scrolling window state
  int8_t selectedIndex = 0;      // absolute index
  int8_t prevSelectedIndex = -1;
  int8_t topIndex = 0;

  // Row marquee (menu items)
  bool           marqueeActive = false;
  uint16_t       marqueeOffset = 0;
  unsigned long  marqueeStartTime = 0;
  unsigned long  lastMarqueeStep  = 0;
  String         marqueeLoop;
  uint16_t       marqueeLoopLen = 0;
  uint8_t        marqueeWindowChars = 21;
  const char*    MARQUEE_GAP = "                   "; // 19 spaces

  // Subtitle marquee
  bool           subMarqueeActive = false;
  uint16_t       subMarqueeOffset = 0;
  unsigned long  subMarqueeStartTime = 0;
  unsigned long  lastSubMarqueeStep  = 0;
  String         subMarqueeLoop;
  uint16_t       subMarqueeLoopLen = 0;

  // Callbacks
  SelectCallback onSelectCb = nullptr;
  BackCallback   onBackCb   = nullptr;

  // Drawing helpers
  inline uint16_t rowY(uint8_t visRow) const { return mStartY + (visRow * mGap); }
  void calcRowMetrics();
  void drawStaticUI();
  void drawVisibleWindow();
  void drawVisibleItem(uint8_t visRow, uint8_t absIndex, bool selected);
  void clearRow(uint8_t visRow, uint16_t bgColor);
  void clearTextArea(uint8_t visRow, uint16_t bgColor);
  void ensureSelectionVisible();
  void redrawSelectionInWindow(int8_t oldAbsIdx, int8_t newAbsIdx);

  // Actions
  void handleSelect(uint8_t index);
  void handleBack();

  // Row Marquee
  void resetMarquee();
  void maybeStartOrStepMarquee();
  void drawSelectedWithMarquee(uint8_t visRow);

  // Subtitle marquee
  void resetSubMarquee();
  void maybeStartOrStepSubMarquee();
  void drawSubtitleFrame();

  // Safe getters
  const char* itemAt(uint8_t idx) const {
    if (!menuItems || idx >= menuCount) return "";
    return menuItems[idx];
  }

  // Internal helpers for the modal
  static void wrapTextNoBreak(const String& text, uint8_t maxWidth, std::vector<String>& outLines);
};