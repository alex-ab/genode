/*
  Copyright (C) 1997-2020 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/* based on sdl2/test/testyuv_cvt.c */

#include <qemu_emul.h>

typedef enum
{
    SDL_YUV_CONVERSION_JPEG,        /**< Full range JPEG */
    SDL_YUV_CONVERSION_BT601,       /**< BT.601 (the default) */
    SDL_YUV_CONVERSION_BT709,       /**< BT.709 */
    SDL_YUV_CONVERSION_AUTOMATIC    /**< BT.601 for SD content, BT.709 for HD content */
} SDL_YUV_CONVERSION_MODE;

enum {
	SDL_PIXELFORMAT_YV12,
	SDL_PIXELFORMAT_IYUV,
	SDL_PIXELFORMAT_NV12,
	SDL_PIXELFORMAT_NV21,
	SDL_PIXELFORMAT_YUY2,
	SDL_PIXELFORMAT_UYVY,
	SDL_PIXELFORMAT_YVYU,
};

typedef uint8_t  Uint8;
typedef uint32_t Uint32;

#define SDL_assert assert
#define SDL_floorf floorf

extern float floorf(float x);



static float clip3(float x, float y, float z)
{
    return ((z < x) ? x : ((z > y) ? y : z));
}

static void RGBtoYUV(Uint8 * rgb, int *yuv, SDL_YUV_CONVERSION_MODE mode, int monochrome, int luminance)
{
	/* Genode: actually in memory we have BGRA ! */
	uint8_t const r = rgb[2];
	uint8_t const g = rgb[1];
	uint8_t const b = rgb[0];

    if (mode == SDL_YUV_CONVERSION_JPEG) {
        /* Full range YUV */
        yuv[0] = (int)(0.299 * r + 0.587 * g + 0.114 * b);
        yuv[1] = (int)((b - yuv[0]) * 0.565 + 128);
        yuv[2] = (int)((r - yuv[0]) * 0.713 + 128);
    } else {
        // This formula is from Microsoft's documentation:
        // https://msdn.microsoft.com/en-us/library/windows/desktop/dd206750(v=vs.85).aspx
        // L = Kr * R + Kb * B + (1 - Kr - Kb) * G
        // Y =                   floor(2^(M-8) * (219*(L-Z)/S + 16) + 0.5);
        // U = clip3(0, (2^M)-1, floor(2^(M-8) * (112*(B-L) / ((1-Kb)*S) + 128) + 0.5));
        // V = clip3(0, (2^M)-1, floor(2^(M-8) * (112*(R-L) / ((1-Kr)*S) + 128) + 0.5));
        float S, Z, R, G, B, L, Kr, Kb, Y, U, V;

        if (mode == SDL_YUV_CONVERSION_BT709) {
            /* BT.709 */
            Kr = 0.2126f;
            Kb = 0.0722f;
        } else {
            /* BT.601 */
            Kr = 0.299f;
            Kb = 0.114f;
        }

        S = 255.0f;
        Z = 0.0f;
        R = r;
        G = g;
        B = b;
        L = Kr * R + Kb * B + (1 - Kr - Kb) * G;
        Y = (Uint8)SDL_floorf((219*(L-Z)/S + 16) + 0.5f);
        U = (Uint8)clip3(0, 255, SDL_floorf((112.0f*(B-L) / ((1.0f-Kb)*S) + 128) + 0.5f));
        V = (Uint8)clip3(0, 255, SDL_floorf((112.0f*(R-L) / ((1.0f-Kr)*S) + 128) + 0.5f));

        yuv[0] = (Uint8)Y;
        yuv[1] = (Uint8)U;
        yuv[2] = (Uint8)V;
    }

    if (monochrome) {
        yuv[1] = 128;
        yuv[2] = 128;
    }

    if (luminance != 100) {
        yuv[0] = yuv[0] * luminance / 100;
        if (yuv[0] > 255)
            yuv[0] = 255;
    }
}

static void ConvertRGBtoPlanar2x2(Uint32 format, Uint8 *src, int pitch, Uint8 *out, int w, int h, SDL_YUV_CONVERSION_MODE mode, int monochrome, int luminance, unsigned const rgb_size)
{
    int x, y;
    int yuv[4][3];
    Uint8 *Y1, *Y2, *U, *V;
    Uint8 *rgb1, *rgb2;
    int rgb_row_advance = (pitch - w*rgb_size) + pitch;
    int UV_advance;

    rgb1 = src;
    rgb2 = src + pitch;

    Y1 = out;
    Y2 = Y1 + w;
    switch (format) {
    case SDL_PIXELFORMAT_YV12:
        V = (Y1 + h * w);
        U = V + ((h + 1)/2)*((w + 1)/2);
        UV_advance = 1;
        break;
    case SDL_PIXELFORMAT_IYUV:
        U = (Y1 + h * w);
        V = U + ((h + 1)/2)*((w + 1)/2);
        UV_advance = 1;
        break;
    case SDL_PIXELFORMAT_NV12:
        U = (Y1 + h * w);
        V = U + 1;
        UV_advance = 2;
        break;
    case SDL_PIXELFORMAT_NV21:
        V = (Y1 + h * w);
        U = V + 1;
        UV_advance = 2;
        break;
    default:
        SDL_assert(!"Unsupported planar YUV format");
        return;
    }

    for (y = 0; y < (h - 1); y += 2) {
        for (x = 0; x < (w - 1); x += 2) {
            RGBtoYUV(rgb1, yuv[0], mode, monochrome, luminance);
            rgb1 += rgb_size;
            *Y1++ = (Uint8)yuv[0][0];

            RGBtoYUV(rgb1, yuv[1], mode, monochrome, luminance);
            rgb1 += rgb_size;
            *Y1++ = (Uint8)yuv[1][0];

            RGBtoYUV(rgb2, yuv[2], mode, monochrome, luminance);
            rgb2 += rgb_size;
            *Y2++ = (Uint8)yuv[2][0];

            RGBtoYUV(rgb2, yuv[3], mode, monochrome, luminance);
            rgb2 += rgb_size;
            *Y2++ = (Uint8)yuv[3][0];

            *U = (Uint8)SDL_floorf((yuv[0][1] + yuv[1][1] + yuv[2][1] + yuv[3][1])/4.0f + 0.5f);
            U += UV_advance;

            *V = (Uint8)SDL_floorf((yuv[0][2] + yuv[1][2] + yuv[2][2] + yuv[3][2])/4.0f + 0.5f);
            V += UV_advance;
        }
        /* Last column */
        if (x == (w - 1)) {
            RGBtoYUV(rgb1, yuv[0], mode, monochrome, luminance);
            rgb1 += rgb_size;
            *Y1++ = (Uint8)yuv[0][0];

            RGBtoYUV(rgb2, yuv[2], mode, monochrome, luminance);
            rgb2 += rgb_size;
            *Y2++ = (Uint8)yuv[2][0];

            *U = (Uint8)SDL_floorf((yuv[0][1] + yuv[2][1])/2.0f + 0.5f);
            U += UV_advance;

            *V = (Uint8)SDL_floorf((yuv[0][2] + yuv[2][2])/2.0f + 0.5f);
            V += UV_advance;
        }
        Y1 += w;
        Y2 += w;
        rgb1 += rgb_row_advance;
        rgb2 += rgb_row_advance;
    }
    /* Last row */
    if (y == (h - 1)) {
        for (x = 0; x < (w - 1); x += 2) {
            RGBtoYUV(rgb1, yuv[0], mode, monochrome, luminance);
            rgb1 += rgb_size;
            *Y1++ = (Uint8)yuv[0][0];

            RGBtoYUV(rgb1, yuv[1], mode, monochrome, luminance);
            rgb1 += rgb_size;
            *Y1++ = (Uint8)yuv[1][0];

            *U = (Uint8)SDL_floorf((yuv[0][1] + yuv[1][1])/2.0f + 0.5f);
            U += UV_advance;

            *V = (Uint8)SDL_floorf((yuv[0][2] + yuv[1][2])/2.0f + 0.5f);
            V += UV_advance;
        }
        /* Last column */
        if (x == (w - 1)) {
            RGBtoYUV(rgb1, yuv[0], mode, monochrome, luminance);
            *Y1++ = (Uint8)yuv[0][0];

            *U = (Uint8)yuv[0][1];
            U += UV_advance;

            *V = (Uint8)yuv[0][2];
            V += UV_advance;
        }
    }
}

static void ConvertRGBtoPacked4(Uint32 format, Uint8 *src, int pitch,
                                Uint8 *out, int w, int h,
                                SDL_YUV_CONVERSION_MODE mode, int monochrome,
                                int luminance, unsigned const rgb_size)
{
    int x, y;
    int yuv[2][3];
    Uint8 *Y1, *Y2, *U, *V;
    Uint8 *rgb;
    int rgb_row_advance = (pitch - w*rgb_size);

    rgb = src;

    switch (format) {
    case SDL_PIXELFORMAT_YUY2:
        Y1 = out;
        U = out+1;
        Y2 = out+2;
        V = out+3;
        break;
    case SDL_PIXELFORMAT_UYVY:
        U = out;
        Y1 = out+1;
        V = out+2;
        Y2 = out+3;
        break;
    case SDL_PIXELFORMAT_YVYU:
        Y1 = out;
        V = out+1;
        Y2 = out+2;
        U = out+3;
        break;
    default:
        SDL_assert(!"Unsupported packed YUV format");
        return;
    }

    for (y = 0; y < h; ++y) {
        for (x = 0; x < (w - 1); x += 2) {
            RGBtoYUV(rgb, yuv[0], mode, monochrome, luminance);
            rgb += rgb_size;
            *Y1 = (Uint8)yuv[0][0];
            Y1 += 4;

            RGBtoYUV(rgb, yuv[1], mode, monochrome, luminance);
            rgb += rgb_size;
            *Y2 = (Uint8)yuv[1][0];
            Y2 += 4;

            *U = (Uint8)SDL_floorf((yuv[0][1] + yuv[1][1])/2.0f + 0.5f);
            U += 4;

            *V = (Uint8)SDL_floorf((yuv[0][2] + yuv[1][2])/2.0f + 0.5f);
            V += 4;
        }
        /* Last column */
        if (x == (w - 1)) {
            RGBtoYUV(rgb, yuv[0], mode, monochrome, luminance);
            rgb += rgb_size;
            *Y2 = *Y1 = (Uint8)yuv[0][0];
            Y1 += 4;
            Y2 += 4;

            *U = (Uint8)yuv[0][1];
            U += 4;

            *V = (Uint8)yuv[0][2];
            V += 4;
        }
        rgb += rgb_row_advance;
    }
}

void ConvertRGBtoYUV(Uint32 format, Uint8 *src, int pitch, Uint8 *out,
                     int w, int h, SDL_YUV_CONVERSION_MODE mode,
                     int monochrome, int luminance, unsigned const rgb_size)
{
    switch (format)
    {
    case SDL_PIXELFORMAT_YV12:
    case SDL_PIXELFORMAT_IYUV:
    case SDL_PIXELFORMAT_NV12:
    case SDL_PIXELFORMAT_NV21:
        ConvertRGBtoPlanar2x2(format, src, pitch, out, w, h, mode, monochrome, luminance, rgb_size);
        return;
    case SDL_PIXELFORMAT_YUY2:
    case SDL_PIXELFORMAT_UYVY:
    case SDL_PIXELFORMAT_YVYU:
        ConvertRGBtoPacked4(format, src, pitch, out, w, h, mode, monochrome, luminance, rgb_size);
        return;
    default:
        SDL_assert(!"Unsupported planar YUV format");
    }
}

void convert_rgba_to_yuv(void * src, int w, int h, unsigned pitch, void * out)
{
    ConvertRGBtoYUV(SDL_PIXELFORMAT_YUY2, src, pitch, out,
                    w, h, SDL_YUV_CONVERSION_BT601,
                    0   /* monochrome */,
                    100 /* luminance */,
                    4   /* rgba size */);
}
