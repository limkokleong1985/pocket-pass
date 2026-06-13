// Display_ST7789.cpp (TFT_eSPI-backed with optional off-screen framebuffer)

#include "Display_ST7789.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite framebuffer = TFT_eSprite(&tft);

static uint16_t g_width  = LCD_WIDTH;
static uint16_t g_height = LCD_HEIGHT;
static uint8_t  g_rot = 0;
static uint8_t  g_text_rot = 0;
static bool     g_has_framebuffer = false;
static bool     g_frame_dirty = false;
static uint16_t g_frame_hold_depth = 0;
const uint16_t extraSpacing = TEXT_EXTRA_SPACING;

uint16_t LCD_Width()  { return g_width; }
uint16_t LCD_Height() { return g_height; }

static void markFrameDirty() {
  g_frame_dirty = true;
}

static void destroyFramebuffer() {
  if (!g_has_framebuffer) return;
  framebuffer.deleteSprite();
  g_has_framebuffer = false;
}

static void ensureFramebuffer() {
  destroyFramebuffer();

  framebuffer.setColorDepth(16);
  framebuffer.setSwapBytes(false);
  g_has_framebuffer = framebuffer.createSprite(g_width, g_height) != nullptr;
  if (g_has_framebuffer) {
    framebuffer.fillSprite(BLACK);
    markFrameDirty();
  }
}

template <typename TCanvas>
static void configureTextCanvas(TCanvas& canvas,
                                uint16_t color,
                                uint16_t bg,
                                uint8_t size) {
  canvas.setTextFont(1);
  canvas.setTextSize(size);
  canvas.setTextColor(color, bg);
  canvas.setTextDatum(TL_DATUM);
}

template <typename TCanvas>
static void drawCharImpl(TCanvas& canvas,
                         uint16_t x,
                         uint16_t y,
                         char c,
                         uint16_t color,
                         uint16_t bg,
                         uint8_t size) {
  configureTextCanvas(canvas, color, bg, size);
  char buf[2] = {c, 0};
  canvas.drawString(buf, x, y);
}

template <typename TCanvas>
static void drawStringImpl(TCanvas& canvas,
                           uint16_t x,
                           uint16_t y,
                           const char* str,
                           uint16_t color,
                           uint16_t bg,
                           uint8_t size) {
  configureTextCanvas(canvas, color, bg, size);
  canvas.drawString(str, x, y);
}

template <typename TCanvas>
static void measureTextSingleLineImpl(TCanvas& canvas,
                                      const char* str,
                                      uint8_t size,
                                      uint16_t& w,
                                      uint16_t& h) {
  if (!str || !*str) {
    w = 0;
    h = (uint16_t)(size * 8);
    return;
  }
  canvas.setTextFont(1);
  canvas.setTextSize(size);
  w = (uint16_t)canvas.textWidth(str);
  h = (uint16_t)canvas.fontHeight();
}

template <typename TCanvas>
static void drawStringWithPaddingImpl(TCanvas& canvas,
                                      uint16_t x,
                                      uint16_t y,
                                      const char* str,
                                      uint16_t fgColor,
                                      uint16_t bgColor,
                                      uint8_t size,
                                      uint16_t padX,
                                      uint16_t padY) {
  canvas.setTextFont(1);
  canvas.setTextSize(size);

  uint16_t textW = (uint16_t)canvas.textWidth(str);
  uint16_t textH = (uint16_t)canvas.fontHeight();

  uint16_t boxX = (x > padX) ? (uint16_t)(x - padX) : 0;
  uint16_t boxY = (y > padY) ? (uint16_t)(y - padY) : 0;
  uint16_t boxW = (uint16_t)(textW + 2 * padX);
  uint16_t boxH = (uint16_t)(textH + 2 * padY);
  canvas.fillRect(boxX, boxY, boxW, boxH, bgColor);

  canvas.setTextColor(fgColor, bgColor);
  canvas.setTextDatum(TL_DATUM);
  canvas.drawString(str, x, y);
}

template <typename TCanvas>
static void drawStringWrapWidthImpl(TCanvas& canvas,
                                    uint16_t x,
                                    uint16_t y,
                                    const char* str,
                                    uint16_t color,
                                    uint16_t bg,
                                    uint8_t size,
                                    uint16_t maxWidthPx) {
  if (!str || !*str || maxWidthPx == 0) return;

  configureTextCanvas(canvas, color, bg, size);

  const uint16_t baseLineH = (uint16_t)canvas.fontHeight();
  const uint16_t lineH = (uint16_t)(baseLineH + 6);

  char lineBuf[256];
  uint16_t lineLen = 0;
  uint16_t lineW = 0;

  auto flush_line = [&]() {
    if (lineLen == 0) return;
    lineBuf[lineLen] = '\0';
    canvas.drawString(lineBuf, x, y);
    y = (uint16_t)(y + lineH);
    lineLen = 0;
    lineW = 0;
  };

  auto charWidth = [&](char c) -> int16_t {
    char tmp[2] = { c, 0 };
    return (int16_t)canvas.textWidth(tmp);
  };

  auto wordWidth = [&](const char* start, const char* end) -> int16_t {
    int16_t w = 0;
    for (const char* p = start; p < end; ++p) w += charWidth(*p);
    return w;
  };

  const char* p = str;
  while (*p) {
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
    if (isSpaceToken && lineLen == 0) continue;

    if (lineW + tokenW <= (int)maxWidthPx || lineLen == 0) {
      if (!isSpaceToken) {
        for (const char* q = tokenStart; q < tokenEnd; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) flush_line();
          if (cw > (int)maxWidthPx && lineLen > 0) flush_line();
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
        } else if (lineLen < sizeof(lineBuf) - 1) {
          lineBuf[lineLen++] = ' ';
          lineW = (uint16_t)(lineW + sw);
        } else {
          flush_line();
        }
      }
    } else {
      flush_line();
      if (!isSpaceToken) {
        for (const char* q = tokenStart; q < tokenEnd; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) flush_line();
          if (cw > (int)maxWidthPx && lineLen > 0) flush_line();
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
      }
    }
  }

  if (lineLen > 0) flush_line();
}

template <typename TCanvas>
static void drawStringWrapWidthScrolledImpl(TCanvas& canvas,
                                            uint16_t x,
                                            uint16_t y,
                                            const char* str,
                                            uint16_t color,
                                            uint16_t bg,
                                            uint8_t size,
                                            uint16_t maxWidthPx,
                                            uint16_t maxHeightPx,
                                            uint16_t scrollPosY) {
  if (!str || !*str || maxWidthPx == 0 || maxHeightPx == 0) return;

  configureTextCanvas(canvas, color, bg, size);

  const uint16_t baseLineH = (uint16_t)canvas.fontHeight();
  const uint16_t lineH = (uint16_t)(baseLineH + 6);
  const int32_t viewTop = (int32_t)scrollPosY;
  const int32_t viewBot = (int32_t)scrollPosY + (int32_t)maxHeightPx;

  char lineBuf[256];
  uint16_t lineLen = 0;
  uint16_t lineW = 0;
  int32_t layoutY = 0;

  auto charWidth = [&](char c) -> int16_t {
    char tmp[2] = { c, 0 };
    return (int16_t)canvas.textWidth(tmp);
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
    canvas.drawString(textLine, x, screenY);
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
    lineW = 0;
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
    if (isSpaceToken && lineLen == 0) continue;

    if (lineW + tokenW <= (int)maxWidthPx || lineLen == 0) {
      if (!isSpaceToken) {
        for (const char* q = tokenStart; q < tokenEnd; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) flush_line();
          if (cw > (int)maxWidthPx && lineLen > 0) flush_line();
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
        } else if (lineLen < sizeof(lineBuf) - 1) {
          lineBuf[lineLen++] = ' ';
          lineW = (uint16_t)(lineW + sw);
        } else {
          flush_line();
        }
      }
    } else {
      flush_line();
      if (!isSpaceToken) {
        for (const char* q = tokenStart; q < tokenEnd; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) flush_line();
          if (cw > (int)maxWidthPx && lineLen > 0) flush_line();
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
      }
    }
  }

  if (lineLen > 0) flush_line();
}

template <typename TCanvas>
static void measureWrappedTextHeightAndLines_Impl(TCanvas& canvas,
                                                  const char* str,
                                                  uint8_t size,
                                                  uint16_t maxWidthPx,
                                                  uint16_t& outTotalH,
                                                  uint16_t& outNumLines) {
  outTotalH = 0;
  outNumLines = 0;
  if (!str || !*str || maxWidthPx == 0) return;

  canvas.setTextFont(1);
  canvas.setTextSize(size);

  const uint16_t baseLineH = (uint16_t)canvas.fontHeight();
  const uint16_t lineH = (uint16_t)(baseLineH + extraSpacing);

  char lineBuf[256];
  uint16_t lineLen = 0;
  uint16_t lineW = 0;

  auto charWidth = [&](char c) -> int16_t {
    char tmp[2] = { c, 0 };
    return (int16_t)canvas.textWidth(tmp);
  };
  auto wordWidth = [&](const char* s, const char* e) -> int16_t {
    int16_t w = 0;
    for (const char* p = s; p < e; ++p) w += charWidth(*p);
    return w;
  };
  auto flush_line = [&]() {
    outTotalH = (uint16_t)(outTotalH + lineH);
    outNumLines = (uint16_t)(outNumLines + 1);
    lineLen = 0;
    lineW = 0;
  };

  const char* p = str;
  while (*p) {
    if (*p == '\n') { flush_line(); ++p; continue; }
    if (lineLen == 0) {
      while (*p == ' ') ++p;
      if (!*p) break;
      if (*p == '\n') { ++p; continue; }
    }

    const char* s = p;
    bool spaceTok = false;
    if (*p == ' ') { while (*p == ' ') ++p; spaceTok = true; }
    else { while (*p && *p != ' ' && *p != '\n') ++p; }
    const char* e = p;

    int16_t tokW = spaceTok ? charWidth(' ') : wordWidth(s, e);
    if (spaceTok && lineLen == 0) continue;

    if (lineW + tokW <= (int)maxWidthPx || lineLen == 0) {
      if (!spaceTok) {
        for (const char* q = s; q < e; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) flush_line();
          if (cw > (int)maxWidthPx && lineLen > 0) flush_line();
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = *q;
            lineW = (uint16_t)(lineW + cw);
          } else {
            flush_line();
            lineBuf[lineLen++] = *q;
            lineW = (uint16_t)cw;
          }
        }
      } else {
        int16_t sw = charWidth(' ');
        if (lineLen > 0 && lineW + sw > (int)maxWidthPx) {
          flush_line();
        } else if (lineLen < sizeof(lineBuf) - 1) {
          lineBuf[lineLen++] = ' ';
          lineW = (uint16_t)(lineW + sw);
        } else {
          flush_line();
        }
      }
    } else {
      flush_line();
      if (!spaceTok) {
        for (const char* q = s; q < e; ++q) {
          int16_t cw = charWidth(*q);
          if (lineW + cw > (int)maxWidthPx && lineLen > 0) flush_line();
          if (cw > (int)maxWidthPx && lineLen > 0) flush_line();
          if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = *q;
            lineW = (uint16_t)(lineW + cw);
          } else {
            flush_line();
            lineBuf[lineLen++] = *q;
            lineW = (uint16_t)cw;
          }
        }
      }
    }
  }

  if (lineLen > 0) flush_line();
}

template <typename TCanvas>
static void drawStringWrapWidthScrolledTailAwareImpl(TCanvas& canvas,
                                                     uint16_t x,
                                                     uint16_t y,
                                                     const char* str,
                                                     uint16_t color,
                                                     uint16_t bg,
                                                     uint8_t size,
                                                     uint16_t maxWidthPx,
                                                     uint16_t maxHeightPx,
                                                     uint16_t& ioScrollPosY,
                                                     uint8_t visibleLines) {
  if (!str || !*str || maxWidthPx == 0 || maxHeightPx == 0) return;

  uint16_t totalH = 0;
  uint16_t numLines = 0;
  measureWrappedTextHeightAndLines_Impl(canvas, str, size, maxWidthPx, totalH, numLines);

  uint16_t baseLineH = (uint16_t)(8u * size);
  uint16_t lineH = (uint16_t)(baseLineH + extraSpacing);

  int32_t viewH = (int32_t)maxHeightPx;
  int32_t maxScroll = (int32_t)totalH - viewH;
  if (maxScroll < 0) maxScroll = 0;

  int32_t desiredScroll = (int32_t)ioScrollPosY;
  if (visibleLines > 0) {
    uint16_t topLine = 0;
    if (numLines > visibleLines) topLine = (uint16_t)(numLines - visibleLines);
    desiredScroll = (int32_t)topLine * (int32_t)lineH;
    if (desiredScroll > maxScroll) desiredScroll = maxScroll;
    if (desiredScroll < 0) desiredScroll = 0;
  }
  ioScrollPosY = (uint16_t)desiredScroll;

  drawStringWrapWidthScrolledImpl(canvas, x, y, str, color, bg, size, maxWidthPx, maxHeightPx, ioScrollPosY);
}

void LCD_WriteCommand(uint8_t) {}
void LCD_WriteData(uint8_t) {}
void LCD_WriteData_Word(uint16_t) {}
void LCD_addWindow(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t*) {}

void LCD_Reset(void) {}

void LCD_SetOrientation(uint8_t rot) {
  g_rot = (rot & 3);
  tft.setRotation(g_rot);
  if (g_rot == 0 || g_rot == 2) {
    g_width = LCD_WIDTH;
    g_height = LCD_HEIGHT;
  } else {
    g_width = LCD_HEIGHT;
    g_height = LCD_WIDTH;
  }
  setTextRotation(g_rot);
  ensureFramebuffer();
}

void setTextRotation(uint8_t rot) { g_text_rot = (rot & 3); }
uint8_t getTextRotation() { return g_text_rot; }

void LCD_Init(void) {
  Backlight_Init();

  tft.init();
#ifdef ENABLE_TFT_DMA
  tft.initDMA();
#endif

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setSwapBytes(false);

  LCD_SetOrientation(ORIENT_PORTRAIT);
  tft.invertDisplay(true);
  tft.fillScreen(BLACK);
}

void LCD_BeginFrame(void) {
  ++g_frame_hold_depth;
}

void LCD_EndFrame(void) {
  if (g_frame_hold_depth == 0) {
    LCD_Present();
    return;
  }
  --g_frame_hold_depth;
  if (g_frame_hold_depth == 0) {
    LCD_Present();
  }
}

void LCD_Present(void) {
  if (g_frame_hold_depth > 0 || !g_frame_dirty) return;
  if (g_has_framebuffer) {
    framebuffer.pushSprite(0, 0);
  }
  g_frame_dirty = false;
}

void LCD_SetCursor(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend) {
  if (Xend >= g_width) Xend = g_width - 1;
  if (Yend >= g_height) Yend = g_height - 1;
  if (!g_has_framebuffer) {
    tft.setWindow(Xstart, Ystart, Xend, Yend);
  }
}

void LCD_Clear(uint16_t color) {
  if (g_has_framebuffer) framebuffer.fillSprite(color);
  else tft.fillScreen(color);
  markFrameDirty();
}

void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
  if (x >= g_width || y >= g_height) return;
  if (g_has_framebuffer) framebuffer.drawPixel(x, y, color);
  else tft.drawPixel(x, y, color);
  markFrameDirty();
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (w == 0 || h == 0) return;
  if (x >= g_width || y >= g_height) return;
  if (x + w > g_width) w = g_width - x;
  if (y + h > g_height) h = g_height - y;
  if (g_has_framebuffer) framebuffer.fillRect(x, y, w, h, color);
  else tft.fillRect(x, y, w, h, color);
  markFrameDirty();
}

static uint8_t g_backlight_percent = 100;

void Backlight_Init(void) {
  pinMode(EXAMPLE_PIN_NUM_BK_LIGHT, OUTPUT);
  digitalWrite(EXAMPLE_PIN_NUM_BK_LIGHT, HIGH);
  g_backlight_percent = 100;
}

void Set_Backlight(uint8_t Light) {
  g_backlight_percent = Light;
  if (Light == 0) digitalWrite(EXAMPLE_PIN_NUM_BK_LIGHT, LOW);
  else digitalWrite(EXAMPLE_PIN_NUM_BK_LIGHT, HIGH);
}

void LCD_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  if (g_has_framebuffer) framebuffer.drawLine(x0, y0, x1, y1, color);
  else tft.drawLine(x0, y0, x1, y1, color);
  markFrameDirty();
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (w == 0 || h == 0) return;
  if (g_has_framebuffer) framebuffer.drawRect(x, y, w, h, color);
  else tft.drawRect(x, y, w, h, color);
  markFrameDirty();
}

void LCD_DrawCircle(int16_t xc, int16_t yc, int16_t r, uint16_t color) {
  if (r < 0) return;
  if (g_has_framebuffer) framebuffer.drawCircle(xc, yc, r, color);
  else tft.drawCircle(xc, yc, r, color);
  markFrameDirty();
}

void LCD_FillCircle(int16_t xc, int16_t yc, int16_t r, uint16_t color) {
  if (r < 0) return;
  if (g_has_framebuffer) framebuffer.fillCircle(xc, yc, r, color);
  else tft.fillCircle(xc, yc, r, color);
  markFrameDirty();
}

void drawCharRot(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size, uint8_t rot) {
  (void)rot;
  if (g_has_framebuffer) drawCharImpl(framebuffer, x, y, c, color, bg, size);
  else drawCharImpl(tft, x, y, c, color, bg, size);
  markFrameDirty();
}

void drawStringRot(uint16_t x, uint16_t y, const char* str,
                   uint16_t color, uint16_t bg, uint8_t size, bool wrap, uint8_t rot) {
  (void)wrap;
  (void)rot;
  if (g_has_framebuffer) drawStringImpl(framebuffer, x, y, str, color, bg, size);
  else drawStringImpl(tft, x, y, str, color, bg, size);
  markFrameDirty();
}

void drawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
  drawCharRot(x, y, c, color, bg, size, g_text_rot);
}

void drawString(uint16_t x, uint16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size, bool wrap) {
  drawStringRot(x, y, str, color, bg, size, wrap, g_text_rot);
}

void measureTextSingleLine(const char* str, uint8_t size, uint16_t& w, uint16_t& h) {
  if (g_has_framebuffer) measureTextSingleLineImpl(framebuffer, str, size, w, h);
  else measureTextSingleLineImpl(tft, str, size, w, h);
}

void drawStringWithPaddingRot(uint16_t x, uint16_t y,
                              const char* str,
                              uint16_t fgColor, uint16_t bgColor,
                              uint8_t size,
                              uint16_t padX, uint16_t padY,
                              bool wrap, uint8_t rot) {
  (void)wrap;
  (void)rot;
  if (g_has_framebuffer) drawStringWithPaddingImpl(framebuffer, x, y, str, fgColor, bgColor, size, padX, padY);
  else drawStringWithPaddingImpl(tft, x, y, str, fgColor, bgColor, size, padX, padY);
  markFrameDirty();
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
                         uint8_t rot) {
  (void)rot;
  if (g_has_framebuffer) drawStringWrapWidthImpl(framebuffer, x, y, str, color, bg, size, maxWidthPx);
  else drawStringWrapWidthImpl(tft, x, y, str, color, bg, size, maxWidthPx);
  markFrameDirty();
}

void drawStringWrapWidth(uint16_t x, uint16_t y,
                         const char* str,
                         uint16_t color, uint16_t bg,
                         uint8_t size,
                         uint16_t maxWidthPx) {
  drawStringWrapWidth(x, y, str, color, bg, size, maxWidthPx, g_text_rot);
}

void drawStringWrapWidthScrolled(uint16_t x, uint16_t y,
                                 const char* str,
                                 uint16_t color, uint16_t bg,
                                 uint8_t size,
                                 uint16_t maxWidthPx,
                                 uint16_t maxHeightPx,
                                 uint16_t scrollPosY,
                                 uint8_t rot) {
  (void)rot;
  if (g_has_framebuffer) drawStringWrapWidthScrolledImpl(framebuffer, x, y, str, color, bg, size, maxWidthPx, maxHeightPx, scrollPosY);
  else drawStringWrapWidthScrolledImpl(tft, x, y, str, color, bg, size, maxWidthPx, maxHeightPx, scrollPosY);
  markFrameDirty();
}

void drawStringWrapWidthScrolled(uint16_t x, uint16_t y,
                                 const char* str,
                                 uint16_t color, uint16_t bg,
                                 uint8_t size,
                                 uint16_t maxWidthPx,
                                 uint16_t maxHeightPx,
                                 uint16_t scrollPosY) {
  drawStringWrapWidthScrolled(x, y, str, color, bg, size, maxWidthPx, maxHeightPx, scrollPosY, g_text_rot);
}

uint16_t measureWrappedTextHeight(const char* str, uint8_t size, uint16_t maxWidthPx) {
  uint16_t totalH = 0;
  uint16_t lines = 0;
  if (g_has_framebuffer) measureWrappedTextHeightAndLines_Impl(framebuffer, str, size, maxWidthPx, totalH, lines);
  else measureWrappedTextHeightAndLines_Impl(tft, str, size, maxWidthPx, totalH, lines);
  return totalH;
}

void drawStringWrapWidthScrolledTailAware(uint16_t x, uint16_t y,
                                          const char* str,
                                          uint16_t color, uint16_t bg,
                                          uint8_t size,
                                          uint16_t maxWidthPx,
                                          uint16_t maxHeightPx,
                                          uint16_t& ioScrollPosY,
                                          uint8_t visibleLines) {
  if (g_has_framebuffer) {
    drawStringWrapWidthScrolledTailAwareImpl(framebuffer, x, y, str, color, bg, size,
                                             maxWidthPx, maxHeightPx, ioScrollPosY, visibleLines);
  } else {
    drawStringWrapWidthScrolledTailAwareImpl(tft, x, y, str, color, bg, size,
                                             maxWidthPx, maxHeightPx, ioScrollPosY, visibleLines);
  }
  markFrameDirty();
}
