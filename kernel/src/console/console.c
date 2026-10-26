#include "console.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limine.h>

#include <flanterm/flanterm.h>
#include <flanterm/flanterm_backends/fb.h>

#include "../boot/limine_requests.h"

static struct flanterm_context *terminal_context;
static bool terminal_ready;

static bool framebuffer_is_supported(const struct limine_framebuffer *fb) {
    if (fb == NULL) {
        return false;
    }

    /* This Flanterm backend handles 32-bit RGB framebuffers. */
    if (fb->memory_model != LIMINE_FRAMEBUFFER_RGB || fb->bpp != 32) {
        return false;
    }

    if (fb->address == NULL || fb->width == 0 || fb->height == 0) {
        return false;
    }

    if ((fb->pitch % sizeof(uint32_t)) != 0 || fb->pitch < fb->width * sizeof(uint32_t)) {
        return false;
    }

    return true;
}

bool console_init(void) {
    if (terminal_ready) {
        return true;
    }

    if (limine_framebuffer_request.response == NULL ||
        limine_framebuffer_request.response->framebuffer_count == 0 ||
        limine_framebuffer_request.response->framebuffers == NULL) {
        return false;
    }

    struct limine_framebuffer *fb = NULL;
    uint64_t fb_index = 0;

    /* Pick the first framebuffer the Flanterm FB backend supports. */
    for (uint64_t i = 0;
         i < limine_framebuffer_request.response->framebuffer_count;
         ++i) {
        struct limine_framebuffer *candidate =
            limine_framebuffer_request.response->framebuffers[i];

        if (framebuffer_is_supported(candidate)) {
            fb = candidate;
            fb_index = i;
            break;
        }
    }

    if (fb == NULL) {
        return false;
    }

    /*
     * The init-params response is optional. It lets us match Limine's colors
     * and font. With Flanterm's built-in bump allocator the background canvas
     * is intentionally ignored, so pass NULL for canvas in either case.
     */
    struct limine_flanterm_fb_init_params *params = NULL;
    if (limine_flanterm_params_request.response != NULL &&
        limine_flanterm_params_request.response->entries != NULL &&
        fb_index < limine_flanterm_params_request.response->entry_count) {
        struct limine_flanterm_fb_init_params *candidate =
            limine_flanterm_params_request.response->entries[fb_index];
        if (candidate != NULL && candidate->font_width != 0 && candidate->font_height != 0) {
            params = candidate;
        }
    }

    terminal_context = flanterm_fb_init(
        NULL, NULL,
        (uint32_t *)fb->address,
        (size_t)fb->width,
        (size_t)fb->height,
        (size_t)fb->pitch,
        fb->red_mask_size, fb->red_mask_shift,
        fb->green_mask_size, fb->green_mask_shift,
        fb->blue_mask_size, fb->blue_mask_shift,
        NULL,
        params != NULL ? params->ansi_colours : NULL,
        params != NULL ? params->ansi_bright_colours : NULL,
        params != NULL ? &params->default_bg : NULL,
        params != NULL ? &params->default_fg : NULL,
        params != NULL ? &params->default_bg_bright : NULL,
        params != NULL ? &params->default_fg_bright : NULL,
        params != NULL ? params->font : NULL,
        params != NULL ? (size_t)params->font_width : 0,
        params != NULL ? (size_t)params->font_height : 0,
        params != NULL ? (size_t)params->font_spacing : 0,
        params != NULL ? (size_t)params->font_scale_x : 0,
        params != NULL ? (size_t)params->font_scale_y : 0,
        params != NULL ? (size_t)params->margin : 0,
        params != NULL ? (int)params->rotation : FLANTERM_FB_ROTATE_0,
        true
    );

    if (terminal_context == NULL) {
        return false;
    }

    terminal_ready = true;
    return true;
}

bool console_is_ready(void) {
    return terminal_ready;
}

void console_write_n(const char *text, size_t length) {
    if (!terminal_ready || terminal_context == NULL || text == NULL || length == 0) {
        return;
    }

    flanterm_write(terminal_context, text, length);
}

void console_write(const char *text) {
    if (text == NULL) {
        return;
    }

    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }

    console_write_n(text, length);
}

void console_write_u64(uint64_t value) {
    char buffer[20];
    size_t pos = sizeof(buffer);

    do {
        buffer[--pos] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && pos != 0);

    console_write_n(&buffer[pos], sizeof(buffer) - pos);
}

void console_write_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char buffer[16];
    size_t pos = sizeof(buffer);

    do {
        buffer[--pos] = digits[value & 0x0f];
        value >>= 4;
    } while (value != 0 && pos != 0);

    console_write("0x");
    console_write_n(&buffer[pos], sizeof(buffer) - pos);
}
