#include "studio.h"

#include <lvgl.h>
#include <src/libs/lodepng/lodepng.h>
#include <webp/decode.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { thumbnail_edge = 128, webp_limit = 512 * 1024, png_limit = 8 * 1024 * 1024 };

static bool write_png(const unsigned char *rgba, unsigned width, unsigned height)
{
    unsigned char *png = NULL;
    size_t size = 0;
    const unsigned error = lodepng_encode32(&png, &size, rgba, width, height);
    const bool written = !error && fwrite(png, 1, size, stdout) == size && fflush(stdout) == 0;
    lv_free(png);
    return written;
}

static bool convert_webp(const unsigned char *input, size_t size)
{
    enum { dimension_limit = 4096 };
    WebPDecoderConfig decoder;
    if (size > webp_limit || !WebPInitDecoderConfig(&decoder) ||
        WebPGetFeatures(input, size, &decoder.input) != VP8_STATUS_OK ||
        decoder.input.has_animation || decoder.input.width > dimension_limit ||
        decoder.input.height > dimension_limit) return false;

    const int width = decoder.input.width;
    const int height = decoder.input.height;
    const int longest = width > height ? width : height;
    if (longest > thumbnail_edge) {
        decoder.options.use_scaling = 1;
        decoder.options.scaled_width = (width * thumbnail_edge + longest - 1) / longest;
        decoder.options.scaled_height = (height * thumbnail_edge + longest - 1) / longest;
    }
    decoder.output.colorspace = MODE_RGBA;
    const bool written = WebPDecode(input, size, &decoder) == VP8_STATUS_OK &&
        write_png(decoder.output.u.RGBA.rgba, decoder.output.width, decoder.output.height);
    WebPFreeDecBuffer(&decoder.output);
    return written;
}

static bool thumbnail_png(const lv_draw_buf_t *image, unsigned width, unsigned height)
{
    unsigned left = width, top = height, right = 0, bottom = 0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            if (!image->data[y * image->header.stride + x * 4 + 3]) continue;
            if (x < left) left = x;
            if (y < top) top = y;
            if (x >= right) right = x + 1;
            if (y >= bottom) bottom = y + 1;
        }
    }
    if (right <= left || bottom <= top) return false;

    const unsigned crop_w = right - left, crop_h = bottom - top;
    const unsigned longest = crop_w > crop_h ? crop_w : crop_h;
    const unsigned scale = longest > thumbnail_edge ? thumbnail_edge : longest;
    const unsigned thumb_w = (crop_w * scale + longest - 1) / longest;
    const unsigned thumb_h = (crop_h * scale + longest - 1) / longest;
    unsigned char thumb[thumbnail_edge * thumbnail_edge * 4];
    for (unsigned y = 0; y < thumb_h; ++y) {
        const unsigned y0 = top + y * crop_h / thumb_h, y1 = top + (y + 1) * crop_h / thumb_h;
        for (unsigned x = 0; x < thumb_w; ++x) {
            const unsigned x0 = left + x * crop_w / thumb_w, x1 = left + (x + 1) * crop_w / thumb_w;
            unsigned sums[4] = {0};
            // Weight each color by alpha so transparent margins keep a clean edge.
            for (unsigned sy = y0; sy < y1; ++sy) {
                const unsigned char *row = image->data + sy * image->header.stride;
                for (unsigned sx = x0; sx < x1; ++sx) {
                    const unsigned char *pixel = row + sx * 4;
                    sums[3] += pixel[3];
                    for (unsigned channel = 0; channel < 3; ++channel) sums[channel] += pixel[channel] * (unsigned)pixel[3];
                }
            }
            unsigned char *pixel = thumb + (y * thumb_w + x) * 4;
            for (unsigned channel = 0; channel < 3; ++channel) pixel[channel] = sums[3] ? sums[channel] / sums[3] : 0;
            pixel[3] = sums[3] / ((x1 - x0) * (y1 - y0));
        }
    }
    return write_png(thumb, thumb_w, thumb_h);
}

static bool convert_png(const unsigned char *input, size_t size)
{
    enum { dimension_limit = 8192, pixel_limit = 8 * 1024 * 1024 };
    LodePNGState state;
    unsigned width = 0, height = 0;
    lodepng_state_init(&state);
    const unsigned inspected = lodepng_inspect(&width, &height, &state, input, size);
    const unsigned depth = state.info_png.color.bitdepth;
    lodepng_state_cleanup(&state);
    // Allow wide logos within the pixel budget and LVGL's 16-bit RGBA stride.
    if (inspected || depth > 8 || !width || !height ||
        width > dimension_limit || height > dimension_limit || width * height > pixel_limit) return false;

    // The separate converter process supplies workspace for full PNG decoding.
    const size_t workspace = (size_t)width * height * 10 + 2 * 1024 * 1024;
    void *memory = malloc(workspace);
    if (!memory) return false;
    lv_mem_pool_t pool = lv_mem_add_pool(memory, workspace);
    unsigned char *decoded = NULL;
    const bool written = pool && !lodepng_decode32(&decoded, &width, &height, input, size) &&
        thumbnail_png((const lv_draw_buf_t *)decoded, width, height);
    if (decoded) lv_draw_buf_destroy((lv_draw_buf_t *)decoded);
    if (pool) lv_mem_remove_pool(pool);
    free(memory);
    return written;
}

int studio_convert_artwork(void)
{
    static unsigned char input[png_limit + 1];
    const size_t size = fread(input, 1, sizeof(input), stdin);
    if (size > png_limit || ferror(stdin)) return 1;
    lv_init();
    const bool png = size >= 8 && memcmp(input, "\x89PNG\r\n\x1a\n", 8) == 0;
    const bool written = png ? convert_png(input, size) : convert_webp(input, size);
    lv_deinit();
    return written ? 0 : 1;
}
