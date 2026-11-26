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

static inline int grain_step_scaled(unsigned *rng, const int *bell_scaled, int lum) {
    unsigned x = *rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *rng = x;
    int rnd = (int)(x & 255u) - 128;
    return (rnd * bell_scaled[lum]) >> 15;
}
void film_pixels(struct film_state *s, unsigned char *pixels, unsigned long count) {
    int preset = s->preset;
    int grain_amp = s->grain_amp;
    unsigned rng = s->rng;
    const unsigned char *red = s->red;
    const unsigned char *green = s->green;
    const unsigned char *blue = s->blue;
    int slot = color_slot(preset);
    unsigned char *end = pixels + count * 3u;
    int bell_scaled[256];
    if (grain_amp) for (int bi = 0; bi < 256; ++bi) bell_scaled[bi] = grain_amp * film_grain_bell[bi];

    if (preset == 4) {
        for (unsigned char *p = pixels; p < end; p += 3) {
            int lum = (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
            int v = red[lum];
            if (grain_amp) v += (int)grain_step_scaled(&rng, bell_scaled, lum);
            unsigned char c = clamp(v);
            p[0] = c; p[1] = c; p[2] = c;
        }
    } else if (preset == 8) {
        for (unsigned char *p = pixels; p < end; p += 3) {
            int r0 = p[0], g0 = p[1], b0 = p[2];
            int lum = (77 * r0 + 150 * g0 + 29 * b0) >> 8;
            int alum = clamp((79 * r0 + 97 * g0 + 80 * b0) >> 8);
            int v = red[alum];
            if (grain_amp) v += (int)grain_step_scaled(&rng, bell_scaled, lum);
            unsigned char c = clamp(v);
            p[0] = c; p[1] = c; p[2] = c;
        }
    } else if (slot >= 0) {
        const int *m = cube_matrix[slot];
        int m0 = m[0], m1 = m[1], m2 = m[2], m3 = m[3], m4 = m[4], m5 = m[5];
        int m6 = m[6], m7 = m[7], m8 = m[8], m9 = m[9] + 128, m10 = m[10] + 128, m11 = m[11] + 128;
        if (grain_amp) {
            for (unsigned char *p = pixels; p < end; p += 3) {
                int r0 = p[0], g0 = p[1], b0 = p[2];
                int lum = (77 * r0 + 150 * g0 + 29 * b0) >> 8;
                int zr = clamp((m0 * r0 + m1 * g0 + m2 * b0 + m9) >> 8);
                int zg = clamp((m3 * r0 + m4 * g0 + m5 * b0 + m10) >> 8);
                int zb = clamp((m6 * r0 + m7 * g0 + m8 * b0 + m11) >> 8);
                int noise = (int)grain_step_scaled(&rng, bell_scaled, lum);
                p[0] = clamp(red[zr] + noise);
                p[1] = clamp(green[zg] + noise);
                p[2] = clamp(blue[zb] + noise);
            }
        } else {
            for (unsigned char *p = pixels; p < end; p += 3) {
                int r0 = p[0], g0 = p[1], b0 = p[2];
                int zr = clamp((m0 * r0 + m1 * g0 + m2 * b0 + m9) >> 8);
                int zg = clamp((m3 * r0 + m4 * g0 + m5 * b0 + m10) >> 8);
                int zb = clamp((m6 * r0 + m7 * g0 + m8 * b0 + m11) >> 8);
                p[0] = red[zr]; p[1] = green[zg]; p[2] = blue[zb];
            }
        }
    } else if (preset == 2) {
        for (unsigned char *p = pixels; p < end; p += 3) {
            int r0 = p[0], g0 = p[1], b0 = p[2];
            int lum = (77 * r0 + 150 * g0 + 29 * b0) >> 8;
            int r = red[r0], g = green[g0], b = blue[b0];
            int l2 = (77 * r + 150 * g + 29 * b) >> 8;
            r = l2 + (((r - l2) * 307) >> 8);
            g = l2 + (((g - l2) * 307) >> 8);
            b = l2 + (((b - l2) * 307) >> 8);
            if (grain_amp) {
                int noise = (int)grain_step_scaled(&rng, bell_scaled, lum);
                r += noise; g += noise; b += noise;
            }
            p[0] = clamp(r); p[1] = clamp(g); p[2] = clamp(b);
        }
    } else if (preset == 1) {
        for (unsigned char *p = pixels; p < end; p += 3) {
            int r0 = p[0], g0 = p[1], b0 = p[2];
            int lum = (77 * r0 + 150 * g0 + 29 * b0) >> 8;
            int r = red[r0], g = green[g0], b = blue[b0];
            if (lum > 195) r += ((lum - 195) * 160) >> 8;
            if (grain_amp) {
                int noise = (int)grain_step_scaled(&rng, bell_scaled, lum);
                r += noise; g += noise; b += noise;
            }
            p[0] = clamp(r); p[1] = clamp(g); p[2] = clamp(b);
        }
    } else {
        if (grain_amp) {
            for (unsigned char *p = pixels; p < end; p += 3) {
                int r0 = p[0], g0 = p[1], b0 = p[2];
                int lum = (77 * r0 + 150 * g0 + 29 * b0) >> 8;
                int noise = (int)grain_step_scaled(&rng, bell_scaled, lum);
                p[0] = clamp(red[r0] + noise);
                p[1] = clamp(green[g0] + noise);
                p[2] = clamp(blue[b0] + noise);
            }
        } else {
            for (unsigned char *p = pixels; p < end; p += 3) {
                p[0] = red[p[0]]; p[1] = green[p[1]]; p[2] = blue[p[2]];
            }
        }
    }
    s->rng = rng;
}

void film_mask_sample(unsigned char *mask, int width, int height, int first_y,
                      const unsigned char *pixels, int rows) {
    int gw = width >> 2, gh = height >> 2;
    int y = (first_y & 3) ? (4 - (first_y & 3)) : 0;
    for (; y < rows; y += 4) {
        int gy = (first_y + y) >> 2;
        if (gy >= gh) break;
        const unsigned char *row = pixels + (unsigned long)y * (unsigned)width * 3u;
        unsigned char *dst = mask + (unsigned long)gy * (unsigned)gw;
        for (int x = 0; x < gw; ++x) {
            const unsigned char *p = row + (unsigned)x * 12u;
            int lum = (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
            dst[x] = (unsigned char)(lum > 195 ? (lum - 195) * 4 : 0);
        }
    }
}

void film_mask_blur(unsigned char *mask, unsigned char *tmp, int gw, int gh) {
    static const unsigned blur_recip[8] = {
        0u, 65536u, 32768u, 21846u, 16384u, 13108u, 10923u, 9363u
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < gh; ++y) {
            const unsigned char *mrow = mask + y * gw;
            unsigned char *trow = tmp + y * gw;
            if (gw >= 8) {
                int s = mrow[0] + mrow[1] + mrow[2] + mrow[3];
                trow[0] = (unsigned char)((unsigned)(s * 16384) >> 16);
                s += mrow[4];
                trow[1] = (unsigned char)((unsigned)(s * 13108) >> 16);
                s += mrow[5];
                trow[2] = (unsigned char)((unsigned)(s * 10923) >> 16);
                s += mrow[6];
                trow[3] = (unsigned char)((unsigned)(s * 9363) >> 16);
                for (int x = 4; x < gw - 3; ++x) {
                    s += mrow[x + 3] - mrow[x - 4];
                    trow[x] = (unsigned char)((unsigned)(s * 9363) >> 16);
                }
                s -= mrow[gw - 7];
                trow[gw - 3] = (unsigned char)((unsigned)(s * 10923) >> 16);
                s -= mrow[gw - 6];
                trow[gw - 2] = (unsigned char)((unsigned)(s * 13108) >> 16);
                s -= mrow[gw - 5];
                trow[gw - 1] = (unsigned char)((unsigned)(s * 16384) >> 16);
            } else {
                for (int x = 0; x < gw; ++x) {
                    int x0 = x < 3 ? 0 : x - 3;
                    int x1 = x + 3 >= gw ? gw - 1 : x + 3;
                    int sum = 0;
                    for (int k = x0; k <= x1; ++k) sum += mrow[k];
                    trow[x] = (unsigned char)((unsigned)(sum * (int)blur_recip[x1 - x0 + 1]) >> 16);
                }
            }
        }
        for (int bx = 0; bx < gw; bx += 64) {
            int cols = gw - bx;
            if (cols > 64) cols = 64;
            int col_sum[64];
            int init_end = gh > 4 ? 4 : gh;
            for (int dx = 0; dx < cols; ++dx) col_sum[dx] = 0;
            for (int k = 0; k < init_end; ++k) {
                const unsigned char *tr = tmp + k * gw + bx;
                for (int dx = 0; dx < cols; ++dx) col_sum[dx] += tr[dx];
            }
            for (int y = 0; y < gh; ++y) {
                if (y > 0) {
                    if (y + 3 < gh) {
                        const unsigned char *add_row = tmp + (y + 3) * gw + bx;
                        for (int dx = 0; dx < cols; ++dx) col_sum[dx] += add_row[dx];
                    }
                    if (y - 4 >= 0) {
                        const unsigned char *sub_row = tmp + (y - 4) * gw + bx;
                        for (int dx = 0; dx < cols; ++dx) col_sum[dx] -= sub_row[dx];
                    }
                }
                int y0 = y < 3 ? 0 : y - 3;
                int y1 = y + 3 >= gh ? gh - 1 : y + 3;
                unsigned recip = blur_recip[y1 - y0 + 1];
                unsigned char *mrow = mask + y * gw + bx;
                for (int dx = 0; dx < cols; ++dx) {
                    mrow[dx] = (unsigned char)((unsigned)(col_sum[dx] * (int)recip) >> 16);
                }
            }
        }
    }
}

void film_halo_apply(unsigned char *pixels, int width, int rows, int first_y,
                     const unsigned char *mask, int gw, int gh, int preset) {
    int strength = preset == 1 ? 150 : 95;
    int rem = width - (gw << 2);
    for (int y = 0; y < rows; ++y) {
        int gy = (first_y + y) >> 2;
        if (gy >= gh) gy = gh - 1;
        unsigned char *p = pixels + (unsigned long)y * (unsigned)width * 3u;
        const unsigned char *mrow = mask + gy * gw;
        for (int gx = 0; gx < gw; ++gx) {
            int glow = mrow[gx];
            if (glow > 2) {
                int dr = (glow * strength) >> 7, dg = (dr * 90) >> 8;
                p[0] = clamp(p[0] + dr); p[1] = clamp(p[1] + dg);
                p[3] = clamp(p[3] + dr); p[4] = clamp(p[4] + dg);
                p[6] = clamp(p[6] + dr); p[7] = clamp(p[7] + dg);
                p[9] = clamp(p[9] + dr); p[10] = clamp(p[10] + dg);
            }
            p += 12;
        }
        if (rem > 0) {
            int glow = mrow[gw - 1];
            if (glow > 2) {
                int dr = (glow * strength) >> 7, dg = (dr * 90) >> 8;
                for (int r = 0; r < rem; ++r, p += 3) {
                    p[0] = clamp(p[0] + dr); p[1] = clamp(p[1] + dg);
                }
            }
        }
    }
}
