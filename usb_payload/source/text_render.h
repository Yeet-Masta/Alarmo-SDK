/**
 * Text rendering for the Nintendo Alarmo.
 * Created in 2024.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "mcufont.h"

/**
 * Initialize the text rendering system
 */
void TEXT_Init(void);

/**
 * Draw text directly to the screen
 * 
 * @param font The font to use
 * @param x The x position (left)
 * @param y The y position (top)
 * @param text The text to render
 * @param r Red color component (0-255)
 * @param g Green color component (0-255)
 * @param b Blue color component (0-255)
 */
void TEXT_DrawString(const struct mf_font_s *font, int16_t x, int16_t y, 
                     const char *text, uint8_t r, uint8_t g, uint8_t b);

/**
 * Draw text to a buffer
 * 
 * @param buffer The framebuffer to draw to
 * @param font The font to use
 * @param x The x position (left)
 * @param y The y position (top)
 * @param text The text to render
 * @param r Red color component (0-255)
 * @param g Green color component (0-255)
 * @param b Blue color component (0-255)
 */
void TEXT_DrawStringToBuffer(uint8_t *buffer, const struct mf_font_s *font, 
                            int16_t x, int16_t y, const char *text,
                            uint8_t r, uint8_t g, uint8_t b);

/**
 * Draw aligned text to a buffer
 * 
 * @param buffer The framebuffer to draw to
 * @param font The font to use
 * @param x The reference x position (depends on alignment)
 * @param y The y position (top)
 * @param align The text alignment (MF_ALIGN_LEFT, MF_ALIGN_CENTER, MF_ALIGN_RIGHT)
 * @param text The text to render
 * @param r Red color component (0-255)
 * @param g Green color component (0-255)
 * @param b Blue color component (0-255)
 */
void TEXT_DrawAlignedToBuffer(uint8_t *buffer, const struct mf_font_s *font,
                             int16_t x, int16_t y, enum mf_align_t align,
                             const char *text, uint8_t r, uint8_t g, uint8_t b);

/**
 * Measure text width
 * 
 * @param font The font to use
 * @param text The text to measure
 * @return The width of the text in pixels
 */
int16_t TEXT_MeasureString(const struct mf_font_s *font, const char *text);

/**
 * Wrap text to a given width
 * 
 * @param buffer The framebuffer to draw to
 * @param font The font to use
 * @param x The x position (left)
 * @param y The y position (top)
 * @param width The maximum width for text wrapping
 * @param text The text to render
 * @param r Red color component (0-255)
 * @param g Green color component (0-255)
 * @param b Blue color component (0-255)
 * @return The height of the rendered text block
 */
int16_t TEXT_DrawWrappedToBuffer(uint8_t *buffer, const struct mf_font_s *font,
                              int16_t x, int16_t y, int16_t width,
                              const char *text, uint8_t r, uint8_t g, uint8_t b);

/**
 * Get the default font
 * 
 * @return Pointer to the default font
 */
const struct mf_font_s* TEXT_GetDefaultFont(void);

/**
 * Get the specified font
 * 
 * @param name The name of the font to get
 * @return Pointer to the font, or NULL if not found
 */
const struct mf_font_s* TEXT_GetFont(const char *name);
