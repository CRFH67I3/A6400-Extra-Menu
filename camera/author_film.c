#include "author_film.h"
#include "author_curves.h"

static unsigned char clamp(int value) {
    return (unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value);
}

static const unsigned char cube_curves[4][3][17] = {
    /* 5: classic_chrome (fitted from .cube) */
    {
        {3, 6, 19, 36, 57, 74, 96, 118, 138, 157, 174, 188, 201, 213, 224, 231, 246},
        {5, 5, 15, 35, 52, 73, 93, 114, 134, 152, 169, 182, 195, 206, 217, 227, 239},
        {0, 10, 26, 43, 58, 76, 93, 114, 133, 152, 169, 184, 199, 212, 223, 232, 244},
    },
    /* 6: agfa_ultra_100 (fitted from .cube) */
    {
        {5, 15, 21, 32, 48, 70, 92, 112, 131, 149, 166, 182, 197, 213, 228, 241, 253},
        {1, 6, 11, 19, 36, 57, 80, 106, 129, 152, 172, 189, 205, 219, 233, 244, 252},
        {12, 21, 27, 37, 53, 70, 89, 109, 129, 148, 165, 180, 193, 206, 220, 238, 254},
    },
    /* 7: polaroid_669 (fitted from .cube) */
    {
        {10, 23, 29, 41, 57, 77, 98, 115, 132, 150, 165, 180, 194, 208, 220, 232, 232},
        {36, 42, 45, 50, 60, 74, 91, 109, 125, 143, 161, 180, 197, 211, 226, 235, 247},
        {92, 92, 92, 92, 92, 92, 92, 105, 123, 144, 165, 185, 201, 215, 225, 231, 237},
    },
    /* 9: polaroid_px680 (fitted from .cube) */
    {
        {58, 58, 58, 58, 58, 60, 83, 109, 133, 159, 178, 192, 203, 213, 221, 227, 234},
        {55, 55, 56, 58, 59, 64, 77, 104, 132, 155, 171, 184, 195, 204, 211, 216, 223},
        {57, 57, 57, 57, 59, 74, 94, 117, 135, 148, 160, 168, 174, 187, 195, 200, 204},
    },
};

static const int cube_matrix[4][12] = {
    {247, -27, 66, 46, 153, 78, 38, 17, 229, 19, 3205, 870},
    {218, 121, -72, -61, 311, -33, -6, 49, 225, -3929, 697, -5668},
    {233, 13, 29, 16, 162, 93, 6, -14, 234, -3579, 3262, 6106},
    {165, 18, 70, 52, 114, 57, 26, 24, 110, 9582, 12128, 9929},
};

static const unsigned char acros_curve[17] = {
    4, 12, 31, 55, 79, 101, 122, 141, 158, 173, 187, 200, 213, 225, 236, 247, 252
};

static unsigned char interp17(const unsigned char k[17], int i) {
    int idx = (i * 16) / 255;
    if (idx >= 16) return k[16];
    int rem = (i * 16) % 255;
    return (unsigned char)((k[idx] * (255 - rem) + k[idx + 1] * rem + 127) / 255);
}

static int color_slot(int preset) {
    return preset == 5 ? 0 : preset == 6 ? 1 : preset == 7 ? 2 : preset == 9 ? 3 : -1;
}

int film_init(struct film_state *s, int preset, int grain) {
    if (!s || preset < 0 || preset > 9 || grain < 0 || grain > 3) return -1;
    s->preset = preset;
    s->grain_amp = grain == 1 ? 12 : grain == 2 ? 22 : grain == 3 ? 38 : 0;
    s->rng = 0xA6400F17u;
    int slot = color_slot(preset);
    for (int i = 0; i < 256; ++i) {
        if (slot >= 0) {
            s->red[i] = interp17(cube_curves[slot][0], i);
            s->green[i] = interp17(cube_curves[slot][1], i);
            s->blue[i] = interp17(cube_curves[slot][2], i);
            continue;
        }
        if (preset == 8) {
            unsigned char v = interp17(acros_curve, i);
            s->red[i] = s->green[i] = s->blue[i] = v;
            continue;
        }
        int r = i, g = i, b = i;
        switch (preset) {
        case 0:
            r = ((i * 276) >> 8) + 6;
            g = ((i * 261) >> 8) + 2;
            b = ((i * 241) >> 8) + 4;
            r = film_curve_toe[clamp(r)];
            g = film_curve_toe[clamp(g)];
            b = film_curve_toe[clamp(b)];
            break;
        case 1:
            r = (i * 243) >> 8;
            g = (i * 251) >> 8;
            b = ((i * 286) >> 8) + 5;
            r = film_curve_toe[clamp(r)];
            g = film_curve_toe[clamp(g)];
            b = film_curve_toe[clamp(b)];
            break;
        case 2:
            r = (film_curve_shoulder[i] * 246) >> 8;
            g = (film_curve_shoulder[i] * 271) >> 8;
            b = (film_curve_shoulder[i] * 276) >> 8;
            break;
        case 3:
            r = ((i * 294) >> 8) + 8;
            g = ((i * 266) >> 8) + 4;
            b = (i * 225) >> 8;
            r = film_curve_toe[clamp(r)];
            g = film_curve_toe[clamp(g)];
            b = film_curve_toe[clamp(b)];
            break;
        default:
            r = g = b = film_curve_shoulder[i];
            break;
        }
        s->red[i] = clamp(r); s->green[i] = clamp(g); s->blue[i] = clamp(b);
    }
    return 0;
}

void film_pixels(struct film_state *s, unsigned char *pixels, unsigned long count) {
    int slot = color_slot(s->preset);
    const int *m = slot >= 0 ? cube_matrix[slot] : (const int *)0;
    for (unsigned long i = 0; i < count; ++i) {
        unsigned char *p = pixels + i * 3u;
        int r0 = p[0], g0 = p[1], b0 = p[2];
        int lum = (77 * r0 + 150 * g0 + 29 * b0) >> 8;
        int r, g, b;
        if (s->preset == 4) r = g = b = s->red[lum];
        else if (s->preset == 8) {
            int alum = clamp((79 * r0 + 97 * g0 + 80 * b0) >> 8);
            r = g = b = s->red[alum];
        } else if (m) {
            int zr = clamp((m[0] * r0 + m[1] * g0 + m[2] * b0 + m[9] + 128) >> 8);
            int zg = clamp((m[3] * r0 + m[4] * g0 + m[5] * b0 + m[10] + 128) >> 8);
            int zb = clamp((m[6] * r0 + m[7] * g0 + m[8] * b0 + m[11] + 128) >> 8);
            r = s->red[zr]; g = s->green[zg]; b = s->blue[zb];
        } else {
            r = s->red[r0]; g = s->green[g0]; b = s->blue[b0];
            if (s->preset == 2) {
                int l2 = (77 * r + 150 * g + 29 * b) >> 8;
                r = l2 + (((r - l2) * 307) >> 8);
                g = l2 + (((g - l2) * 307) >> 8);
                b = l2 + (((b - l2) * 307) >> 8);
            } else if (s->preset == 1 && lum > 195) r += ((lum - 195) * 160) >> 8;
        }
        if (s->grain_amp) {
            unsigned x = s->rng;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5; s->rng = x;
            int rnd = (int)(x & 255u) - 128;
            int noise = (rnd * s->grain_amp * film_grain_bell[lum]) >> 15;
            r += noise; g += noise; b += noise;
        }
        p[0] = clamp(r); p[1] = clamp(g); p[2] = clamp(b);
    }
}

void film_mask_sample(unsigned char *mask, int width, int height, int first_y,
                      const unsigned char *pixels, int rows) {
    int gw = width / 4, gh = height / 4;
    for (int y = 0; y < rows; ++y) {
        int image_y = first_y + y;
        if (image_y % 4 || image_y / 4 >= gh) continue;
        const unsigned char *row = pixels + (unsigned long)y * (unsigned)width * 3u;
        for (int x = 0; x < gw; ++x) {
            const unsigned char *p = row + (unsigned)x * 12u;
            int lum = (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
            mask[(unsigned long)(image_y / 4) * (unsigned)gw + (unsigned)x] =
                (unsigned char)(lum > 195 ? (lum - 195) * 4 : 0);
        }
    }
}

void film_mask_blur(unsigned char *mask, unsigned char *tmp, int gw, int gh) {
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < gh; ++y) for (int x = 0; x < gw; ++x) {
            int sum = 0, count = 0;
            int x0 = x < 3 ? 0 : x - 3;
            int x1 = x + 3 >= gw ? gw - 1 : x + 3;
            for (int k = x0; k <= x1; ++k) { sum += mask[y * gw + k]; ++count; }
            tmp[y * gw + x] = (unsigned char)(sum / count);
        }
        for (int y = 0; y < gh; ++y) {
            int y0 = y < 3 ? 0 : y - 3;
            int y1 = y + 3 >= gh ? gh - 1 : y + 3;
            int count = y1 - y0 + 1;
            for (int x = 0; x < gw; ++x) {
                int sum = 0;
                for (int k = y0; k <= y1; ++k) sum += tmp[k * gw + x];
                mask[y * gw + x] = (unsigned char)(sum / count);
            }
        }
    }
}

void film_halo_apply(unsigned char *pixels, int width, int rows, int first_y,
                     const unsigned char *mask, int gw, int gh, int preset) {
    int strength = preset == 1 ? 150 : 95;
    for (int y = 0; y < rows; ++y) {
        int gy = (first_y + y) / 4;
        if (gy >= gh) gy = gh - 1;
        unsigned char *row = pixels + (unsigned long)y * (unsigned)width * 3u;
        const unsigned char *mrow = mask + gy * gw;
        for (int x = 0; x < width; ++x) {
            int gx = x / 4;
            if (gx >= gw) gx = gw - 1;
            int glow = mrow[gx];
            if (glow > 2) {
                int dr = (glow * strength) >> 7, dg = (dr * 90) >> 8;
                unsigned char *p = row + (unsigned)x * 3u;
                p[0] = clamp(p[0] + dr); p[1] = clamp(p[1] + dg);
            }
        }
    }
}
