/* Weststar SDL Browser — gfx: image surfaces and PNG output.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gfx-private.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* Returned for a failed create, so callers can check the status as with
 * Cairo instead of handling NULL. Never freed. */
gfx_surface_t *
gfx_surface_nil(gfx_status_t status)
{
    static gfx_surface_t nil_memory = { .ref = -1, .status = GFX_STATUS_NO_MEMORY,
                                        .format = GFX_FORMAT_INVALID,
                                        .dev_sx = 1, .dev_sy = 1 };
    static gfx_surface_t nil_size = { .ref = -1, .status = GFX_STATUS_INVALID_SIZE,
                                      .format = GFX_FORMAT_INVALID,
                                      .dev_sx = 1, .dev_sy = 1 };
    static gfx_surface_t nil_format = { .ref = -1, .status = GFX_STATUS_INVALID_FORMAT,
                                        .format = GFX_FORMAT_INVALID,
                                        .dev_sx = 1, .dev_sy = 1 };
    switch (status) {
    case GFX_STATUS_INVALID_SIZE:   return &nil_size;
    case GFX_STATUS_INVALID_FORMAT: return &nil_format;
    default:                        return &nil_memory;
    }
}

static int
bytes_per_pixel(gfx_format_t format)
{
    return format == GFX_FORMAT_A8 ? 1 : 4;
}

int
gfx_format_stride_for_width(gfx_format_t format, int width)
{
    if (format != GFX_FORMAT_ARGB32 && format != GFX_FORMAT_RGB24 &&
        format != GFX_FORMAT_A8)
        return -1;
    if (width < 0 || width > (INT_MAX - 3) / 4)
        return -1;
    return (width * bytes_per_pixel(format) + 3) & ~3;
}

static gfx_surface_t *
surface_new(gfx_format_t format, int width, int height)
{
    gfx_surface_t *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->ref = 1;
    s->format = format;
    s->width = width;
    s->height = height;
    s->dev_sx = s->dev_sy = 1;
    return s;
}

gfx_surface_t *
gfx_image_surface_create(gfx_format_t format, int width, int height)
{
    int stride = gfx_format_stride_for_width(format, width);
    if (stride < 0)
        return gfx_surface_nil(format == GFX_FORMAT_INVALID ?
                               GFX_STATUS_INVALID_FORMAT :
                               GFX_STATUS_INVALID_SIZE);
    if (height < 0 || (height > 0 && (size_t)stride > SIZE_MAX / (size_t)height))
        return gfx_surface_nil(GFX_STATUS_INVALID_SIZE);
    gfx_surface_t *s = surface_new(format, width, height);
    if (!s)
        return gfx_surface_nil(GFX_STATUS_NO_MEMORY);
    s->stride = stride;
    size_t bytes = (size_t)stride * (size_t)height;
    s->data = calloc(bytes ? bytes : 1, 1);
    if (!s->data) {
        free(s);
        return gfx_surface_nil(GFX_STATUS_NO_MEMORY);
    }
    s->owns_data = 1;
    return s;
}

gfx_surface_t *
gfx_image_surface_create_for_data(unsigned char *data, gfx_format_t format,
                                  int width, int height, int stride)
{
    int min_stride = gfx_format_stride_for_width(format, width);
    if (min_stride < 0)
        return gfx_surface_nil(GFX_STATUS_INVALID_FORMAT);
    if (height < 0 || stride < width * bytes_per_pixel(format) ||
        (stride & 3) != 0)
        return gfx_surface_nil(GFX_STATUS_INVALID_SIZE);
    gfx_surface_t *s = surface_new(format, width, height);
    if (!s)
        return gfx_surface_nil(GFX_STATUS_NO_MEMORY);
    s->stride = stride;
    s->data = data;
    return s;
}

gfx_surface_t *
gfx_surface_reference(gfx_surface_t *s)
{
    if (s && s->ref > 0)
        s->ref++;
    return s;
}

void
gfx_surface_destroy(gfx_surface_t *s)
{
    if (!s || s->ref <= 0)
        return;
    if (--s->ref > 0)
        return;
    if (s->owns_data)
        free(s->data);
    free(s);
}

gfx_status_t
gfx_surface_status(const gfx_surface_t *s)
{
    return s ? s->status : GFX_STATUS_NO_MEMORY;
}

unsigned char *
gfx_image_surface_get_data(gfx_surface_t *s)
{
    return s ? s->data : NULL;
}

gfx_format_t
gfx_image_surface_get_format(const gfx_surface_t *s)
{
    return s ? s->format : GFX_FORMAT_INVALID;
}

int
gfx_image_surface_get_width(const gfx_surface_t *s)
{
    return s ? s->width : 0;
}

int
gfx_image_surface_get_height(const gfx_surface_t *s)
{
    return s ? s->height : 0;
}

int
gfx_image_surface_get_stride(const gfx_surface_t *s)
{
    return s ? s->stride : 0;
}

void
gfx_surface_flush(gfx_surface_t *s)
{
    (void)s;
}

void
gfx_surface_mark_dirty(gfx_surface_t *s)
{
    if (s && s->ref > 0)
        s->opacity = 0;
}

int
gfx_surface_is_opaque(gfx_surface_t *s)
{
    if (!s || !s->data)
        return 0;
    if (s->format == GFX_FORMAT_RGB24)
        return 1;
    if (s->format != GFX_FORMAT_ARGB32)
        return 0;
    if (s->opacity == 0) {
        int opaque = 1;
        for (int y = 0; y < s->height && opaque; y++) {
            const uint32_t *row = (const uint32_t *)(const void *)
                (s->data + (size_t)y * (size_t)s->stride);
            for (int x = 0; x < s->width; x++)
                if ((row[x] >> 24) != 0xff) {
                    opaque = 0;
                    break;
                }
        }
        if (s->ref > 0)
            s->opacity = opaque ? 1 : 2;
        return opaque;
    }
    return s->opacity == 1;
}

void
gfx_surface_set_device_offset(gfx_surface_t *s, double x, double y)
{
    if (!s || s->ref <= 0)
        return;
    s->dev_x = x;
    s->dev_y = y;
}

void
gfx_surface_get_device_offset(const gfx_surface_t *s, double *x, double *y)
{
    if (x) *x = s ? s->dev_x : 0;
    if (y) *y = s ? s->dev_y : 0;
}

void
gfx_surface_set_device_scale(gfx_surface_t *s, double sx, double sy)
{
    if (!s || s->ref <= 0 || sx == 0 || sy == 0)
        return;
    s->dev_sx = sx;
    s->dev_sy = sy;
}

void
gfx_surface_get_device_scale(const gfx_surface_t *s, double *sx, double *sy)
{
    if (sx) *sx = s ? s->dev_sx : 1;
    if (sy) *sy = s ? s->dev_sy : 1;
}

/* ---- PNG --------------------------------------------------------------- */

static void
put_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static gfx_status_t
write_chunk(gfx_write_func_t write, void *closure, const char *type,
            const unsigned char *data, uint32_t len)
{
    unsigned char head[8], tail[4];
    put_be32(head, len);
    memcpy(head + 4, type, 4);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, head + 4, 4);
    if (len)
        crc = crc32(crc, data, len);
    put_be32(tail, (uint32_t)crc);
    if (write(closure, head, 8) != GFX_STATUS_SUCCESS)
        return GFX_STATUS_WRITE_ERROR;
    if (len && write(closure, data, len) != GFX_STATUS_SUCCESS)
        return GFX_STATUS_WRITE_ERROR;
    if (write(closure, tail, 4) != GFX_STATUS_SUCCESS)
        return GFX_STATUS_WRITE_ERROR;
    return GFX_STATUS_SUCCESS;
}

/* Writes ARGB32 as 8-bit RGBA with straight alpha, RGB24 as RGB and A8 as
 * 8-bit greyscale, as Cairo does. */
gfx_status_t
gfx_surface_write_to_png_stream(gfx_surface_t *s, gfx_write_func_t write,
                                void *closure)
{
    if (!s || s->status != GFX_STATUS_SUCCESS)
        return s ? s->status : GFX_STATUS_NO_MEMORY;
    if (s->width <= 0 || s->height <= 0)
        return GFX_STATUS_INVALID_SIZE;
    int channels = s->format == GFX_FORMAT_ARGB32 ? 4 :
                   s->format == GFX_FORMAT_RGB24 ? 3 : 1;
    size_t row_bytes = 1 + (size_t)s->width * (size_t)channels;
    size_t raw_len = row_bytes * (size_t)s->height;
    unsigned char *raw = malloc(raw_len);
    if (!raw)
        return GFX_STATUS_NO_MEMORY;
    for (int y = 0; y < s->height; y++) {
        unsigned char *o = raw + (size_t)y * row_bytes;
        const unsigned char *row = s->data + (size_t)y * (size_t)s->stride;
        *o++ = 0;   /* filter: none */
        for (int x = 0; x < s->width; x++) {
            if (s->format == GFX_FORMAT_A8) {
                *o++ = row[x];
                continue;
            }
            uint32_t p;
            memcpy(&p, row + (size_t)x * 4, 4);
            unsigned a = s->format == GFX_FORMAT_ARGB32 ? p >> 24 : 255;
            unsigned r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
            if (a != 255 && a != 0) {
                r = (r * 255 + a / 2) / a;
                g = (g * 255 + a / 2) / a;
                b = (b * 255 + a / 2) / a;
            } else if (a == 0) {
                r = g = b = 0;
            }
            *o++ = (unsigned char)(r > 255 ? 255 : r);
            *o++ = (unsigned char)(g > 255 ? 255 : g);
            *o++ = (unsigned char)(b > 255 ? 255 : b);
            if (channels == 4)
                *o++ = (unsigned char)a;
        }
    }
    uLongf zlen = compressBound((uLong)raw_len);
    unsigned char *z = malloc(zlen);
    if (!z) {
        free(raw);
        return GFX_STATUS_NO_MEMORY;
    }
    int zerr = compress2(z, &zlen, raw, (uLong)raw_len, 6);
    free(raw);
    if (zerr != Z_OK) {
        free(z);
        return GFX_STATUS_NO_MEMORY;
    }

    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    unsigned char ihdr[13];
    put_be32(ihdr, (uint32_t)s->width);
    put_be32(ihdr + 4, (uint32_t)s->height);
    ihdr[8] = 8;
    ihdr[9] = channels == 4 ? 6 : channels == 3 ? 2 : 0;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    gfx_status_t st = write(closure, sig, sizeof sig);
    if (st == GFX_STATUS_SUCCESS)
        st = write_chunk(write, closure, "IHDR", ihdr, sizeof ihdr);
    if (st == GFX_STATUS_SUCCESS)
        st = write_chunk(write, closure, "IDAT", z, (uint32_t)zlen);
    if (st == GFX_STATUS_SUCCESS)
        st = write_chunk(write, closure, "IEND", NULL, 0);
    free(z);
    return st == GFX_STATUS_SUCCESS ? st : GFX_STATUS_WRITE_ERROR;
}

static gfx_status_t
file_write(void *closure, const unsigned char *data, unsigned int length)
{
    return fwrite(data, 1, length, closure) == length ?
           GFX_STATUS_SUCCESS : GFX_STATUS_WRITE_ERROR;
}

gfx_status_t
gfx_surface_write_to_png(gfx_surface_t *s, const char *filename)
{
    FILE *f = fopen(filename, "wb");
    if (!f)
        return GFX_STATUS_WRITE_ERROR;
    gfx_status_t st = gfx_surface_write_to_png_stream(s, file_write, f);
    if (fclose(f) != 0 && st == GFX_STATUS_SUCCESS)
        st = GFX_STATUS_WRITE_ERROR;
    return st;
}
