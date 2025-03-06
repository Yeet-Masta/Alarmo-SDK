/**
 * ST7789 LCD code for the Nintendo Alarmo.
 * Created in 2024 by GaryOderNichts.
 */
#pragma once

#include <stdint.h>

#define SCREEN_HEIGHT 240
#define SCREEN_WIDTH 320

void LCD_Init(void);

void LCD_RDID(uint32_t* outId);

void LCD_DISPON(void);

void LCD_SetBrightness(float brightness);

void LCD_DrawRect(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint8_t r, uint8_t g, uint8_t b);

void LCD_DrawScreenBuffer(const uint8_t *buffer, uint32_t size);

void LCD_DrawRectToBuffer(uint8_t* buffer, uint32_t x0, uint32_t y0, uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b);
void LCD_ClearBuffer(uint8_t* buffer, uint8_t r, uint8_t g, uint8_t b);
void LCD_DrawCircleToBuffer(uint8_t* buffer, int32_t centerX, int32_t centerY, int32_t radius, uint8_t r, uint8_t g, uint8_t b);