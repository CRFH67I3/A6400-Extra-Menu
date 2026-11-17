#ifndef A6400_AUTHOR_FILM_H
#define A6400_AUTHOR_FILM_H
#ifdef __cplusplus
extern "C" {
#endif
/* Port of DevCrashDay/a6400 ritual_daemon.c, commit de73e850044da4e5872be1ee93e4dc71684f2851.
 * This core performs no I/O or allocations. Presets follow the author's enum. */
struct film_state {
    unsigned char red[256], green[256], blue[256];
    unsigned rng;
    int preset, grain_amp;
};
int film_init(struct film_state *state, int preset, int grain);
void film_pixels(struct film_state *state, unsigned char *pixels, unsigned long count);
void film_mask_sample(unsigned char *mask, int width, int height, int first_y,
                      const unsigned char *pixels, int rows);
void film_mask_blur(unsigned char *mask, unsigned char *scratch, int gw, int gh);
void film_halo_apply(unsigned char *pixels, int width, int rows, int first_y,
                     const unsigned char *mask, int gw, int gh, int preset);
#ifdef __cplusplus
}
#endif
#endif
