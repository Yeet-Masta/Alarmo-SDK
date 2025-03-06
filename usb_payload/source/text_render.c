/**
 * Text rendering for the Nintendo Alarmo.
 * Created in 2024.
 */
#include "text_render.h"
#include "libalarmo/lcd.h"
#include <string.h>

// Font definitions - these will be provided through the font header file
// that you'll need to generate using the mcufont-bdf tool
#include "mcufont.h"

// Structure to keep track of rendering state
typedef struct {
    uint8_t *buffer;
    uint16_t width;
    uint16_t height;
    uint8_t r, g, b;
    const struct mf_font_s *font;
} render_state_t;

// Line callback state for wordwrap
typedef struct {
    render_state_t *render_state;
    int16_t x;
    int16_t y;
    int16_t height;
} line_state_t;

void TEXT_Init(void)
{
    // Nothing needed for initialization currently
    // Could be used to initialize font cache or other resources
}

// Callback for rendering pixels
static void pixel_callback(int16_t x, int16_t y, uint8_t count, uint8_t alpha, void *state)
{
    render_state_t *s = (render_state_t*)state;
    
    // Ignore if out of bounds
    if (y < 0 || x < 0 || y >= s->height) 
        return;
    
    // Convert alpha (0-255) to opacity (0.0-1.0)
    float opacity = (float)alpha / 255.0f;
    
    // Draw pixel run directly to buffer
    for (uint8_t i = 0; i < count && (x + i) < s->width; i++) {
        int32_t index = ((SCREEN_WIDTH - 1 - (x + i)) * SCREEN_HEIGHT + y) * 3;
        
        if (index + 2 < SCREEN_WIDTH * SCREEN_HEIGHT * 3) {
            // Apply alpha blending with existing pixel
            if (alpha == 255) {
                // Fully opaque, just set the color
                s->buffer[index] = s->b;
                s->buffer[index + 1] = s->g;
                s->buffer[index + 2] = s->r;
            } else if (alpha > 0) {
                // Alpha blending
                s->buffer[index] = (uint8_t)((opacity * s->b) + ((1.0f - opacity) * s->buffer[index]));
                s->buffer[index + 1] = (uint8_t)((opacity * s->g) + ((1.0f - opacity) * s->buffer[index + 1]));
                s->buffer[index + 2] = (uint8_t)((opacity * s->r) + ((1.0f - opacity) * s->buffer[index + 2]));
            }
        }
    }
}

// Callback for rendering characters
static uint8_t character_callback(int16_t x, int16_t y, mf_char character, void *state)
{
    render_state_t *s = (render_state_t*)state;
    return mf_render_character(s->font, x, y, character, pixel_callback, state);
}

// Callback for line wrapping
static bool line_callback(mf_str line, uint16_t count, void *state)
{
    line_state_t *s = (line_state_t*)state;
    
    mf_render_aligned(s->render_state->font, s->x, s->y, 
                     MF_ALIGN_LEFT, line, count, 
                     character_callback, s->render_state);
    
    s->y += s->render_state->font->line_height;
    s->height = s->y;
    
    return true;
}

void TEXT_DrawString(const struct mf_font_s *font, int16_t x, int16_t y, 
                     const char *text, uint8_t r, uint8_t g, uint8_t b)
{
    // Create a small temporary buffer for direct rendering
    uint8_t temp_buffer[SCREEN_WIDTH * SCREEN_HEIGHT * 3];
    memset(temp_buffer, 0, sizeof(temp_buffer));
    
    // Draw to the buffer
    TEXT_DrawStringToBuffer(temp_buffer, font, x, y, text, r, g, b);
    
    // Transfer to screen
    LCD_DrawScreenBuffer(temp_buffer, sizeof(temp_buffer));
}

void TEXT_DrawStringToBuffer(uint8_t *buffer, const struct mf_font_s *font, 
                            int16_t x, int16_t y, const char *text,
                            uint8_t r, uint8_t g, uint8_t b)
{
    // Set up the rendering state
    render_state_t state = {
        .buffer = buffer,
        .width = SCREEN_WIDTH,
        .height = SCREEN_HEIGHT,
        .r = r,
        .g = g,
        .b = b,
        .font = font
    };
    
    // Render the text directly (no alignment or wrapping)
    mf_render_aligned(font, x, y, MF_ALIGN_LEFT, text, 0, character_callback, &state);
}

void TEXT_DrawAlignedToBuffer(uint8_t *buffer, const struct mf_font_s *font,
                             int16_t x, int16_t y, enum mf_align_t align,
                             const char *text, uint8_t r, uint8_t g, uint8_t b)
{
    // Set up the rendering state
    render_state_t state = {
        .buffer = buffer,
        .width = SCREEN_WIDTH,
        .height = SCREEN_HEIGHT,
        .r = r,
        .g = g,
        .b = b,
        .font = font
    };
    
    // Render the text with the specified alignment
    mf_render_aligned(font, x, y, align, text, 0, character_callback, &state);
}

int16_t TEXT_MeasureString(const struct mf_font_s *font, const char *text)
{
    // Use the font's character width function to measure the text
    return mf_get_string_width(font, text, 0, true);
}

int16_t TEXT_DrawWrappedToBuffer(uint8_t *buffer, const struct mf_font_s *font,
                              int16_t x, int16_t y, int16_t width,
                              const char *text, uint8_t r, uint8_t g, uint8_t b)
{
    // Set up the rendering state
    render_state_t render_state = {
        .buffer = buffer,
        .width = SCREEN_WIDTH,
        .height = SCREEN_HEIGHT,
        .r = r,
        .g = g,
        .b = b,
        .font = font
    };
    
    // Set up the line state
    line_state_t line_state = {
        .render_state = &render_state,
        .x = x,
        .y = y,
        .height = y  // Will be updated during rendering
    };
    
    // Perform word wrapping and rendering
    mf_wordwrap(font, width, text, line_callback, &line_state);
    
    // Return the total height of the rendered text
    return line_state.height - y;
}

const struct mf_font_s* TEXT_GetDefaultFont(void)
{
    // Return the first font in the font list as default
    return mf_get_font_list()->font;
}

const struct mf_font_s* TEXT_GetFont(const char *name)
{
    // Look up a font by name
    return mf_find_font(name);
}
