//Display_ST7789.h

#pragma once
#include <Arduino.h>
#include <stdint.h>

// =========================
// Panel / Orientation defines
// =========================
#ifndef LCD_WIDTH
#define LCD_WIDTH  172
#endif
#ifndef LCD_HEIGHT
#define LCD_HEIGHT 320
#endif

// Orientation constants (must match your .cpp usage)
#ifndef ORIENT_PORTRAIT
#define ORIENT_PORTRAIT 0
#endif
#ifndef ORIENT_LANDSCAPE
#define ORIENT_LANDSCAPE 1
#endif
#ifndef ORIENT_PORTRAIT_REV
#define ORIENT_PORTRAIT_REV 2
#endif
#ifndef ORIENT_LANDSCAPE_REV
#define ORIENT_LANDSCAPE_REV 3
#endif

// Example backlight GPIO (set to your actual pin)
#ifndef EXAMPLE_PIN_NUM_BK_LIGHT
#define EXAMPLE_PIN_NUM_BK_LIGHT 46
#endif

// Basic colors (RGB565)
#ifndef BLACK
#define BLACK   0x0000
#endif
#ifndef WHITE
#define WHITE   0xFFFF
#endif
#ifndef RED
#define RED     0xF800
#endif
#ifndef GREEN
#define GREEN   0x07E0
#endif
#ifndef BLUE
#define BLUE    0x001F
#endif

// =========================
// Public API (as used in Display_ST7789.cpp)
// =========================

// Global logical width/height accessors
uint16_t LCD_Width();
uint16_t LCD_Height();

// Low-level no-ops kept for ABI compatibility
void LCD_WriteCommand(uint8_t cmd);
void LCD_WriteData(uint8_t data);
void LCD_WriteData_Word(uint16_t data);
void LCD_addWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t* ptr);

void LCD_Reset(void);
void LCD_Init(void);

// Orientation control
void LCD_SetOrientation(uint8_t rot);
void setTextRotation(uint8_t rot);
uint8_t getTextRotation();

// Backlight (simple on/off GPIO)
void Backlight_Init(void);
void Set_Backlight(uint8_t Light); // 0=OFF, 1..100=ON

// Primitives
void LCD_SetCursor(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend);
void LCD_Clear(uint16_t color);
void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void LCD_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void LCD_DrawCircle(int16_t xc, int16_t yc, int16_t r, uint16_t color);
void LCD_FillCircle(int16_t xc, int16_t yc, int16_t r, uint16_t color);

// Text helpers
void drawCharRot(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size, uint8_t rot);
void drawStringRot(uint16_t x, uint16_t y, const char* str,
                   uint16_t color, uint16_t bg, uint8_t size, bool wrap, uint8_t rot);

void drawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
void drawString(uint16_t x, uint16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size, bool wrap);

void measureTextSingleLine(const char* str, uint8_t size, uint16_t& w, uint16_t& h);

void drawStringWithPaddingRot(uint16_t x, uint16_t y,
                              const char* str,
                              uint16_t fgColor, uint16_t bgColor,
                              uint8_t size,
                              uint16_t padX, uint16_t padY,
                              bool wrap, uint8_t rot);

void drawStringWithPadding(uint16_t x, uint16_t y,
                           const char* str,
                           uint16_t fgColor, uint16_t bgColor,
                           uint8_t size,
                           uint16_t padX, uint16_t padY,
                           bool wrap);

// Word-wrap (line spacing applied internally in .cpp)
void drawStringWrapWidth(uint16_t x, uint16_t y,
                         const char* str,
                         uint16_t color, uint16_t bg,
                         uint8_t size,
                         uint16_t maxWidthPx);

void drawStringWrapWidth(uint16_t x, uint16_t y,
                         const char* str,
                         uint16_t color, uint16_t bg,
                         uint8_t size,
                         uint16_t maxWidthPx,
                         uint8_t rot);

// Scrolled word-wrap (add vertical cutout and scroll position)
// IMPORTANT: Provide both overloads — one without rot, and one with rot.
// Do NOT place these in extern "C" — we need C++ overloading here.

void drawStringWrapWidthScrolled(uint16_t x, uint16_t y,
                                 const char* str,
                                 uint16_t color, uint16_t bg,
                                 uint8_t size,
                                 uint16_t maxWidthPx,
                                 uint16_t maxHeightPx,
                                 uint16_t scrollPosY);

void drawStringWrapWidthScrolled(uint16_t x, uint16_t y,
                                 const char* str,
                                 uint16_t color, uint16_t bg,
                                 uint8_t size,
                                 uint16_t maxWidthPx,
                                 uint16_t maxHeightPx,
                                 uint16_t scrollPosY,
                                 uint8_t rot);

// Measurement helper (total wrapped height) — useful for clamping scroll
uint16_t measureWrappedTextHeight(const char* str,
                                  uint8_t size,
                                  uint16_t maxWidthPx);

void drawStringWrapWidthScrolledTailAware(uint16_t x, uint16_t y,
                                          const char* str,
                                          uint16_t color, uint16_t bg,
                                          uint8_t size,
                                          uint16_t maxWidthPx,
                                          uint16_t maxHeightPx,
                                          uint16_t& ioScrollPosY,
                                          uint8_t visibleLines);

#ifndef TEXT_EXTRA_SPACING
#define TEXT_EXTRA_SPACING 6
#endif
