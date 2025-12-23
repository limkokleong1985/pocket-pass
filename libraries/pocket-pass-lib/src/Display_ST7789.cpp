// Display_ST7789.cpp (TFT_eSPI-backed, no-LEDC version)

#include "Display_ST7789.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>

// Global TFT instance
static TFT_eSPI tft = TFT_eSPI();

// Track logical width/height based on rotation
static uint16_t g_width  = LCD_WIDTH;   // 172
static uint16_t g_height = LCD_HEIGHT;  // 320
static uint8_t  g_rot    = 0;           // 0..3
static uint8_t  g_text_rot = 0;         // separate text rotation
const uint16_t extraSpacing = TEXT_EXTRA_SPACING;
uint16_t LCD_Width()  { return g_width; }
uint16_t LCD_Height() { return g_height; }

// No-op low-levels to preserve ABI (TFT_eSPI manages SPI)
void LCD_WriteCommand(uint8_t) {}
void LCD_WriteData(uint8_t) {}
void LCD_WriteData_Word(uint16_t) {}
void LCD_addWindow(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t*) {}

void LCD_Reset(void) {
  // tft handles reset in init()
}

void LCD_SetOrientation(uint8_t rot) {
  g_rot = (rot & 3);
  tft.setRotation(g_rot);
  if (g_rot == 0 || g_rot == 2) {
    g_width  = LCD_WIDTH;
    g_height = LCD_HEIGHT;
  } else {
    g_width  = LCD_HEIGHT;
    g_height = LCD_WIDTH;
  }
  setTextRotation(g_rot); // keep text aligned with screen
}

void setTextRotation(uint8_t rot) { g_text_rot = (rot & 3); }
uint8_t getTextRotation() { return g_text_rot; }

void LCD_Init(void) {
  Backlight_Init();

  tft.init();
  #ifdef ENABLE_TFT_DMA
    tft.initDMA();
  #endif

  tft.setTextFont(1);   // built-in 8px font
  tft.setTextSize(1);   // scale 1 (we scale in calls)
  tft.setSwapBytes(false);

  LCD_SetOrientation(ORIENT_PORTRAIT);

  // If your panel expects inversion:
  tft.invertDisplay(true);

  tft.fillScreen(BLACK);
}

void LCD_SetCursor(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend) {
  if (Xend >= g_width)  Xend = g_width - 1;
  if (Yend >= g_height) Yend = g_height - 1;
  tft.setWindow(Xstart, Ystart, Xend, Yend);
}

void LCD_Clear(uint16_t color) {
  tft.fillScreen(color);
}

void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
  if (x >= g_width || y >= g_height) return;
  tft.drawPixel(x, y, color);
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (w == 0 || h == 0) return;
  if (x >= g_width || y >= g_height) return;
  if (x + w > g_width)  w = g_width - x;
  if (y + h > g_height) h = g_height - y;
  tft.fillRect(x, y, w, h, color);
}

// ---------------- Backlight (simple GPIO on/off, no PWM) ----------------
static uint8_t g_backlight_percent = 100;

void Backlight_Init(void) {
  pinMode(EXAMPLE_PIN_NUM_BK_LIGHT, OUTPUT);
  digitalWrite(EXAMPLE_PIN_NUM_BK_LIGHT, HIGH); // default ON
  g_backlight_percent = 100;
}

void Set_Backlight(uint8_t Light) {
  // 0 -> OFF, 1..100 -> ON
  g_backlight_percent = Light;
  if (Light == 0) digitalWrite(EXAMPLE_PIN_NUM_BK_LIGHT, LOW);
  else            digitalWrite(EXAMPLE_PIN_NUM_BK_LIGHT, HIGH);
}

// ---------------- Primitives ----------------
void LCD_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  tft.drawLine(x0, y0, x1, y1, color);
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (w == 0 || h == 0) return;
  tft.drawRect(x, y, w, h, color);
}

void LCD_DrawCircle(int16_t xc, int16_t yc, int16_t r, uint16_t color) {
  if (r < 0) return;
  tft.drawCircle(xc, yc, r, color);
}

void LCD_FillCircle(int16_t xc, int16_t yc, int16_t r, uint16_t color) {
  if (r < 0) return;
  tft.fillCircle(xc, yc, r, color);
}

// ---------------- Text helpers ----------------
// We temporarily change display rotation for per-text rotation, then restore.

void drawCharRot(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size, uint8_t rot) {
  uint8_t prevRot = g_rot;
  tft.setRotation(rot & 3);

  tft.setTextFont(1);
  tft.setTextSize(size);
  tft.setTextColor(color, bg);
  tft.setTextDatum(TL_DATUM);

  char buf[2] = {c, 0};
  tft.drawString(buf, x, y);

  tft.setRotation(prevRot);
}

void drawStringRot(uint16_t x, uint16_t y, const char* str,
                   uint16_t color, uint16_t bg, uint8_t size, bool wrap, uint8_t rot) {
  (void)wrap; // wrapping handled by drawStringWrapWidth
  uint8_t prevRot = g_rot;
  tft.setRotation(rot & 3);

  tft.setTextFont(1);
  tft.setTextSize(size);
  tft.setTextColor(color, bg);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(str, x, y);

  tft.setRotation(prevRot);
}

void drawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
  drawCharRot(x, y, c, color, bg, size, g_text_rot);
}

void drawString(uint16_t x, uint16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size, bool wrap) {
  drawStringRot(x, y, str, color, bg, size, wrap, g_text_rot);
}

void measureTextSingleLine(const char* str, uint8_t size, uint16_t& w, uint16_t& h) {
  uint8_t prevRot = g_rot;
  uint8_t rot = g_text_rot;
  tft.setRotation(rot & 3);

  if (!str || !*str) { w = 0; h = (uint16_t)(size * 8); tft.setRotation(prevRot); return; }
  tft.setTextFont(1);
  tft.setTextSize(size);
  w = (uint16_t)tft.textWidth(str);
  h = (uint16_t)tft.fontHeight();

  tft.setRotation(prevRot);
}

void drawStringWithPaddingRot(uint16_t x, uint16_t y,
                              const char* str,
                              uint16_t fgColor, uint16_t bgColor,
                              uint8_t size,
                              uint16_t padX, uint16_t padY,
                              bool wrap, uint8_t rot)
{
  (void)wrap;
  uint8_t prevRot = g_rot;
  tft.setRotation(rot & 3);

  tft.setTextFont(1);
  tft.setTextSize(size);

  uint16_t textW = (uint16_t)tft.textWidth(str);
  uint16_t textH = (uint16_t)tft.fontHeight();

  uint16_t boxX = (x > padX) ? (uint16_t)(x - padX) : 0;
  uint16_t boxY = (y > padY) ? (uint16_t)(y - padY) : 0;
  uint16_t boxW = (uint16_t)(textW + 2 * padX);
  uint16_t boxH = (uint16_t)(textH + 2 * padY);
  tft.fillRect(boxX, boxY, boxW, boxH, bgColor);

  tft.setTextColor(fgColor, bgColor);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(str, x, y);

  tft.setRotation(prevRot);
}

void drawStringWithPadding(uint16_t x, uint16_t y,
                           const char* str,
                           uint16_t fgColor, uint16_t bgColor,
                           uint8_t size,
                           uint16_t padX, uint16_t padY,
                           bool wrap) {
  drawStringWithPaddingRot(x, y, str, fgColor, bgColor, size, padX, padY, wrap, g_text_rot);
}

void drawStringWrapWidth(uint16_t x, uint16_t y,
                         const char* str,
                         uint16_t color, uint16_t bg,
                         uint8_t size,
                         uint16_t maxWidthPx,
                         uint8_t rot)
{
  if (!str || !*str || maxWidthPx == 0) return;

  uint8_t prevRot = g_rot;
  tft.setRotation(rot & 3);

  tft.setTextFont(1);
  tft.setTextSize(size);
  tft.setTextColor(color, bg);
  tft.setTextDatum(TL_DATUM);

  const uint16_t baseLineH = (uint16_t)tft.fontHeight();
  const uint16_t extraSpacing = 6; // tweak to taste; use 2*size if you want it to scale
  const uint16_t lineH = (uint16_t)(baseLineH + extraSpacing);

  char lineBuf[256];
  uint16_t lineLen = 0; // number of chars in lineBuf
  uint16_t lineW = 0;   // current line width in pixels

  auto flush_line = [&]() {
    if (lineLen == 0) return;
    lineBuf[lineLen] = '\0';
    tft.drawString(lineBuf, x, y);
    y = (uint16_t)(y + lineH);
    lineLen = 0;
    lineW = 0;
  };

  auto charWidth = [&](char c) -> int16_t {
    char tmp[2] = { c, 0 };
    return (int16_t)tft.textWidth(tmp);
  };

  auto wordWidth = [&](const char* start, const char* end) -> int16_t {
    // [start, end) word range
    int16_t w = 0;
    for (const char* p = start; p < end; ++p) {
      w += charWidth(*p);
    }
    return w;
  };

  const char* p = str;
  while (*p) {
    // Handle explicit newline
    if (*p == '\n') {
      flush_line();
      ++p;
      continue;
    }

    // Skip any leading spaces if we're at start of a line to avoid indenting wrapped lines
    if (lineLen == 0) {
      while (*p == ' ') ++p;
      if (!*p) break;
      if (*p == '\n') { ++p; continue; }
    }

    // Extract next token: either a run of non-space (a word) or a single space
    const char* tokenStart = p;
    bool isSpaceToken = false;

    if (*p == ' ') {
      // Collapse spaces to a single space token
      while (*p == ' ') ++p;
      isSpaceToken = true;
    } else {
      // A word: sequence of non-space, non-newline
      while (*p && *p != ' ' && *p != '\n') ++p;
    }
    const char* tokenEnd = p;

    // Compute token width
    int16_t tokenW = 0;
    if (isSpaceToken) {
      tokenW = charWidth(' ');
    } else {
      tokenW = wordWidth(tokenStart, tokenEnd);
    }

    // If the token is a space and the line is empty, skip it
    if (isSpaceToken && lineLen == 0) {
      continue;
    }

    // If token fits, append it
    if (lineW + tokenW <= (int)maxWidthPx || lineLen == 0) {
      // Append token (or as much as fits if it's a too-long word on empty line)
      if (!isSpaceToken) {
        // Word token
        for (const char* q = tokenStart; q < tokenEnd; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) {
            // Wrap before placing this char
            flush_line();
          }
          // If still too wide at start, force char (very long glyph)
          if (cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = *q;
            lineW = (uint16_t)(lineW + cw);
          } else {
            // Buffer full: flush and retry this char
            flush_line();
            // Place char on new line
            if (lineLen < sizeof(lineBuf) - 1) {
              lineBuf[lineLen++] = *q;
              lineW = (uint16_t)cw;
            }
          }
        }
      } else {
        // Space token: append a single space advance (no visible glyph)
        int16_t sw = charWidth(' ');
        // If space would overflow, wrap first (unless at line start)
        if (lineLen > 0 && lineW + sw > (int)maxWidthPx) {
          flush_line();
        } else if (sw > (int)maxWidthPx && lineLen > 0) {
          flush_line();
        } else {
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = ' ';
            lineW = (uint16_t)(lineW + sw);
          } else {
            flush_line();
          }
        }
      }
    } else {
      // Token doesn't fit and line not empty: wrap before placing the token
      flush_line();

      // Place token on the next line (handle very long word by character splitting)
      if (!isSpaceToken) {
        for (const char* q = tokenStart; q < tokenEnd; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = *q;
            lineW = (uint16_t)(lineW + cw);
          } else {
            flush_line();
            if (lineLen < sizeof(lineBuf) - 1) {
              lineBuf[lineLen++] = *q;
              lineW = (uint16_t)cw;
            }
          }
        }
      } else {
        // Leading space on new line: skip it to avoid indenting
      }
    }
  }

  // Flush any remaining text
  if (lineLen > 0) {
    flush_line();
  }

  tft.setRotation(prevRot);
}

void drawStringWrapWidth(uint16_t x, uint16_t y,
                         const char* str,
                         uint16_t color, uint16_t bg,
                         uint8_t size,
                         uint16_t maxWidthPx)
{
  drawStringWrapWidth(x, y, str, color, bg, size, maxWidthPx, g_text_rot);
}

// Word-wrapped draw with vertical cutout and scrolling.
// Renders a window of height maxHeightPx, scrolled by scrollPosY (both in pixels).
// - x, y: top-left anchor where the viewport begins.
// - scrollPosY: how many pixels to scroll downward into the laid-out text (0 = top).
// - maxHeightPx: viewport height in pixels; lines outside are not drawn.
// Notes:
// - Keeps same word-wrap, line spacing, and rotation behavior.
// - Does not modify global rotation after it returns.
// - If you need the total laid-out height (to clamp scroll), see the variant that returns it.
void drawStringWrapWidthScrolled(uint16_t x, uint16_t y,
                                 const char* str,
                                 uint16_t color, uint16_t bg,
                                 uint8_t size,
                                 uint16_t maxWidthPx,
                                 uint16_t maxHeightPx,
                                 uint16_t scrollPosY,
                                 uint8_t rot)
{
  if (!str || !*str || maxWidthPx == 0 || maxHeightPx == 0) return;

  uint8_t prevRot = g_rot;
  tft.setRotation(rot & 3);

  tft.setTextFont(1);
  tft.setTextSize(size);
  tft.setTextColor(color, bg);
  tft.setTextDatum(TL_DATUM);

  const uint16_t baseLineH = (uint16_t)tft.fontHeight();
  const uint16_t extraSpacing = 6; // adjust if you want different inter-line spacing
  const uint16_t lineH = (uint16_t)(baseLineH + extraSpacing);

  const int32_t viewTop = (int32_t)scrollPosY;
  const int32_t viewBot = (int32_t)scrollPosY + (int32_t)maxHeightPx;

  char lineBuf[256];
  uint16_t lineLen = 0;
  uint16_t lineW   = 0;
  int32_t layoutY  = 0;

  auto charWidth = [&](char c) -> int16_t {
    char tmp[2] = { c, 0 };
    return (int16_t)tft.textWidth(tmp);
  };
  auto wordWidth = [&](const char* start, const char* end) -> int16_t {
    int16_t w = 0;
    for (const char* p = start; p < end; ++p) w += charWidth(*p);
    return w;
  };
  auto draw_if_visible = [&](const char* textLine, int32_t lineBaselineY) {
    int32_t top = lineBaselineY;
    int32_t bot = lineBaselineY + (int32_t)lineH;
    if (bot <= viewTop || top >= viewBot) return;
    int16_t screenY = (int16_t)(y + (lineBaselineY - (int32_t)scrollPosY));
    tft.drawString(textLine, x, screenY);
  };
  auto flush_line = [&]() {
    if (lineLen == 0) {
      layoutY += lineH;
      return;
    }
    lineBuf[lineLen] = '\0';
    draw_if_visible(lineBuf, layoutY);
    layoutY += lineH;
    lineLen = 0;
    lineW   = 0;
  };

  const char* p = str;
  while (*p) {
    if (layoutY > viewBot) break;

    if (*p == '\n') {
      flush_line();
      ++p;
      continue;
    }

    if (lineLen == 0) {
      while (*p == ' ') ++p;
      if (!*p) break;
      if (*p == '\n') { ++p; continue; }
    }

    const char* tokenStart = p;
    bool isSpaceToken = false;

    if (*p == ' ') {
      while (*p == ' ') ++p;
      isSpaceToken = true;
    } else {
      while (*p && *p != ' ' && *p != '\n') ++p;
    }
    const char* tokenEnd = p;

    int16_t tokenW = isSpaceToken ? charWidth(' ') : wordWidth(tokenStart, tokenEnd);

    if (isSpaceToken && lineLen == 0) {
      continue;
    }

    if (lineW + tokenW <= (int)maxWidthPx || lineLen == 0) {
      if (!isSpaceToken) {
        for (const char* q = tokenStart; q < tokenEnd; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = *q;
            lineW = (uint16_t)(lineW + cw);
          } else {
            flush_line();
            if (lineLen < sizeof(lineBuf) - 1) {
              lineBuf[lineLen++] = *q;
              lineW = (uint16_t)cw;
            }
          }
        }
      } else {
        int16_t sw = charWidth(' ');
        if (lineLen > 0 && lineW + sw > (int)maxWidthPx) {
          flush_line();
        } else if (sw > (int)maxWidthPx && lineLen > 0) {
          flush_line();
        } else {
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = ' ';
            lineW = (uint16_t)(lineW + sw);
          } else {
            flush_line();
          }
        }
      }
    } else {
      flush_line();
      if (!isSpaceToken) {
        for (const char* q = tokenStart; q < tokenEnd; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = *q;
            lineW = (uint16_t)(lineW + cw);
          } else {
            flush_line();
            if (lineLen < sizeof(lineBuf) - 1) {
              lineBuf[lineLen++] = *q;
              lineW = (uint16_t)cw;
            }
          }
        }
      } else {
        // skip leading spaces on new line
      }
    }
  }

  if (lineLen > 0) {
    flush_line();
  }

  tft.setRotation(prevRot);
}

void drawStringWrapWidthScrolled(uint16_t x, uint16_t y,
                                 const char* str,
                                 uint16_t color, uint16_t bg,
                                 uint8_t size,
                                 uint16_t maxWidthPx,
                                 uint16_t maxHeightPx,
                                 uint16_t scrollPosY)
{
  drawStringWrapWidthScrolled(x, y, str, color, bg, size, maxWidthPx, maxHeightPx, scrollPosY, g_text_rot);
}

static void measureWrappedTextHeightAndLines_Internal(const char* str,
                                                      uint8_t size,
                                                      uint16_t maxWidthPx,
                                                      uint16_t& outTotalH,
                                                      uint16_t& outNumLines)
{
  outTotalH = 0;
  outNumLines = 0;
  if (!str || !*str || maxWidthPx == 0) return;

  uint8_t prevRot = g_rot;
  tft.setRotation(g_text_rot & 3);

  tft.setTextFont(1);
  tft.setTextSize(size);

  const uint16_t baseLineH = (uint16_t)tft.fontHeight();
  const uint16_t lineH = (uint16_t)(baseLineH + extraSpacing);

  char lineBuf[256];
  uint16_t lineLen = 0;
  uint16_t lineW   = 0;

  auto charWidth = [&](char c) -> int16_t {
    char tmp[2] = { c, 0 };
    return (int16_t)tft.textWidth(tmp);
  };
  auto wordWidth = [&](const char* s, const char* e)->int16_t{
    int16_t w=0; for (auto p=s; p<e; ++p) w += charWidth(*p); return w;
  };
  auto flush_line = [&]() {
    outTotalH = (uint16_t)(outTotalH + lineH);
    outNumLines = (uint16_t)(outNumLines + 1);
    lineLen = 0; lineW = 0;
  };

  const char* p = str;
  while (*p) {
    if (*p == '\n') { flush_line(); ++p; continue; }
    if (lineLen == 0) { while (*p == ' ') ++p; if (!*p) break; if (*p == '\n') { ++p; continue; } }

    const char* s = p; bool spaceTok=false;
    if (*p == ' ') { while (*p == ' ') ++p; spaceTok=true; }
    else { while (*p && *p!=' ' && *p!='\n') ++p; }
    const char* e = p;

    int16_t tokW = spaceTok ? charWidth(' ') : wordWidth(s,e);
    if (spaceTok && lineLen == 0) continue;

    if (lineW + tokW <= (int)maxWidthPx || lineLen == 0) {
      if (!spaceTok) {
        for (const char* q=s; q<e; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) { flush_line(); }
          if (cw > (int)maxWidthPx && lineLen > 0) { flush_line(); }
          if (lineLen < sizeof(lineBuf)-1) { lineBuf[lineLen++] = *q; lineW = (uint16_t)(lineW + cw); }
          else { flush_line(); lineBuf[lineLen++] = *q; lineW = (uint16_t)cw; }
        }
      } else {
        int16_t sw = charWidth(' ');
        if (lineLen > 0 && lineW + sw > (int)maxWidthPx) { flush_line(); }
        else {
          if (lineLen < sizeof(lineBuf)-1) { lineBuf[lineLen++]=' '; lineW=(uint16_t)(lineW+sw);}
          else { flush_line(); }
        }
      }
    } else {
      flush_line();
      if (!spaceTok) {
        for (const char* q=s; q<e; ++q) {
          int16_t cw=charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) { flush_line(); }
          if (cw > (int)maxWidthPx && lineLen > 0) { flush_line(); }
          if (lineLen < sizeof(lineBuf)-1) { lineBuf[lineLen++]=*q; lineW=(uint16_t)(lineW+cw); }
          else { flush_line(); lineBuf[lineLen++]=*q; lineW=(uint16_t)cw; }
        }
      }
    }
  }
  if (lineLen > 0) flush_line();

  tft.setRotation(prevRot);
}

uint16_t measureWrappedTextHeight(const char* str, uint8_t size, uint16_t maxWidthPx)
{
  uint16_t totalH=0, lines=0;
  measureWrappedTextHeightAndLines_Internal(str, size, maxWidthPx, totalH, lines);
  return totalH;
}

// Tail-aware scrolled draw: computes the ideal scroll (to show last N lines) BEFORE drawing.
// If you want to pre-scroll to tail, pass visibleLines (e.g., 4). If 0, no pre-scroll is applied.
void drawStringWrapWidthScrolledTailAware(uint16_t x, uint16_t y,
                                          const char* str,
                                          uint16_t color, uint16_t bg,
                                          uint8_t size,
                                          uint16_t maxWidthPx,
                                          uint16_t maxHeightPx,
                                          uint16_t& ioScrollPosY,
                                          uint8_t visibleLines)
{
  if (!str || !*str || maxWidthPx == 0 || maxHeightPx == 0) return;

  // Measure first
  uint16_t totalH=0, numLines=0;
  measureWrappedTextHeightAndLines_Internal(str, size, maxWidthPx, totalH, numLines);

  // Compute ideal tail scroll now, before drawing
  uint16_t baseLineH = 8u * size;
  uint16_t lineH = (uint16_t)(baseLineH + extraSpacing);

  int32_t viewH = (int32_t)maxHeightPx;
  int32_t maxScroll = (int32_t)totalH - viewH;
  if (maxScroll < 0) maxScroll = 0;

  int32_t desiredScroll = (int32_t)ioScrollPosY; // default: keep incoming
  if (visibleLines > 0) {
    uint16_t topLine = 0;
    if (numLines > visibleLines) topLine = (uint16_t)(numLines - visibleLines);
    desiredScroll = (int32_t)topLine * (int32_t)lineH;
    if (desiredScroll > maxScroll) desiredScroll = maxScroll;
    if (desiredScroll < 0) desiredScroll = 0;
  }
  ioScrollPosY = (uint16_t)desiredScroll;

  // Draw with adjusted scroll (uses current text rotation internally)
  uint8_t prevRot = g_rot;
  tft.setRotation(g_text_rot & 3);

  tft.setTextFont(1);
  tft.setTextSize(size);
  tft.setTextColor(color, bg);
  tft.setTextDatum(TL_DATUM);

  const int32_t viewTop = (int32_t)ioScrollPosY;
  const int32_t viewBot = (int32_t)ioScrollPosY + (int32_t)maxHeightPx;

  char lineBuf[256];
  uint16_t lineLen = 0;
  uint16_t lineW   = 0;
  int32_t layoutY  = 0;

  auto charWidth = [&](char c) -> int16_t {
    char tmp[2] = { c, 0 };
    return (int16_t)tft.textWidth(tmp);
  };
  auto wordWidth = [&](const char* start, const char* end) -> int16_t {
    int16_t w = 0;
    for (const char* p = start; p < end; ++p) w += charWidth(*p);
    return w;
  };
  auto draw_if_visible = [&](const char* textLine, int32_t lineBaselineY) {
    int32_t top = lineBaselineY;
    int32_t bot = lineBaselineY + (int32_t)lineH;
    if (bot <= viewTop || top >= viewBot) return;
    int16_t screenY = (int16_t)(y + (lineBaselineY - (int32_t)ioScrollPosY));
    tft.drawString(textLine, x, screenY);
  };
  auto flush_line = [&]() {
    if (lineLen == 0) {
      layoutY += lineH;
      return;
    }
    lineBuf[lineLen] = '\0';
    draw_if_visible(lineBuf, layoutY);
    layoutY += lineH;
    lineLen = 0;
    lineW   = 0;
  };

  const char* p = str;
  while (*p) {
    if (layoutY > viewBot) break;

    if (*p == '\n') {
      flush_line();
      ++p;
      continue;
    }

    if (lineLen == 0) {
      while (*p == ' ') ++p;
      if (!*p) break;
      if (*p == '\n') { ++p; continue; }
    }

    const char* tokenStart = p;
    bool isSpaceToken = false;

    if (*p == ' ') {
      while (*p == ' ') ++p;
      isSpaceToken = true;
    } else {
      while (*p && *p != ' ' && *p != '\n') ++p;
    }
    const char* tokenEnd = p;

    int16_t tokenW = isSpaceToken ? charWidth(' ') : wordWidth(tokenStart, tokenEnd);

    if (isSpaceToken && lineLen == 0) {
      continue;
    }

    if (lineW + tokenW <= (int)maxWidthPx || lineLen == 0) {
      if (!isSpaceToken) {
        for (const char* q = tokenStart; q < tokenEnd; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = *q;
            lineW = (uint16_t)(lineW + cw);
          } else {
            flush_line();
            if (lineLen < sizeof(lineBuf) - 1) {
              lineBuf[lineLen++] = *q;
              lineW = (uint16_t)cw;
            }
          }
        }
      } else {
        int16_t sw = charWidth(' ');
        if (lineLen > 0 && lineW + sw > (int)maxWidthPx) {
          flush_line();
        } else if (sw > (int)maxWidthPx && lineLen > 0) {
          flush_line();
        } else {
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = ' ';
            lineW = (uint16_t)(lineW + sw);
          } else {
            flush_line();
          }
        }
      }
    } else {
      flush_line();
      if (!isSpaceToken) {
        for (const char* q = tokenStart; q < tokenEnd; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (cw > (int)maxWidthPx && lineLen > 0) {
            flush_line();
          }
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = *q;
            lineW = (uint16_t)(lineW + cw);
          } else {
            flush_line();
            if (lineLen < sizeof(lineBuf) - 1) {
              lineBuf[lineLen++] = *q;
              lineW = (uint16_t)cw;
            }
          }
        }
      } else {
        // skip leading spaces on new line
      }
    }
  }

  if (lineLen > 0) {
    flush_line();
  }

  tft.setRotation(prevRot);
}
