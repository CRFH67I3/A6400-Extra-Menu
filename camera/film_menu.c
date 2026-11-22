#include "film_menu.h"

static const char *const stock_keys[10] = {
    "PORTRA_400", "CINESTILL_800T", "FUJI_PROVIA", "KODAK_GOLD", "TRI_X_400",
    "CLASSIC_CHROME", "AGFA_ULTRA_100", "POLAROID_669", "FUJI_ACROS", "POLAROID_PX680"
};
static const char *const stock_workers[10] = {
    "portra_400", "cinestill_800t", "fuji_provia", "kodak_gold", "tri_x_400",
    "classic_chrome", "agfa_ultra_100", "polaroid_669", "fuji_acros", "polaroid_px680"
};
static const char *const stock_labels[10] = {
    "Portra 400", "CineStill 800T", "Fuji Provia", "Kodak Gold", "Tri-X 400",
    "Classic Chrome", "Agfa Ultra 100", "Polaroid 669", "Fuji Acros", "Polaroid PX680+"
};
static const char *const grain_keys[4] = {"OFF", "LOW", "MEDIUM", "HIGH"};

static int valid(const struct film_settings *s) {
    return s && s->enabled >= 0 && s->enabled <= 1 && s->stock >= 0 && s->stock < 10 &&
           s->grain >= 0 && s->grain < 4 && s->halation >= 0 && s->halation <= 1;
}
const char *film_stock_worker_name(int stock) {
    return stock >= 0 && stock < 10 ? stock_workers[stock] : (const char *)0;
}
const char *film_stock_display_name(int stock) {
    return stock >= 0 && stock < 10 ? stock_labels[stock] : (const char *)0;
}
void film_menu_init(struct film_menu *m, const struct film_settings *settings) {
    *m = (struct film_menu){0};
    m->settings = valid(settings) ? *settings : (struct film_settings){0, 0, 2, 1};
    m->selected = m->touch_row = -1;
}
unsigned film_menu_close(struct film_menu *m) {
    if (!m->visible) return 0;
    m->visible = m->touch_ready = m->touching = 0;
    m->selected = m->touch_row = -1;
    return FILM_MENU_CLOSED;
}
unsigned film_menu_input(struct film_menu *m, unsigned now, int menu, int zoom, int other) {
    int keys_valid = (menu == 0 || menu == 1) && (zoom == 0 || zoom == 1);
    if (!keys_valid)
        m->released = m->pending = m->fired = 0;
    else if (m->have_sample && now - m->last_sample > 250u) {
        /* A native screen transition can pause our callback. Discard elapsed
         * hold time, but retain an observed release and an already-fired latch.
         * The same physical hold must supply a fresh600ms after the gap; keys
         * held since startup or an invalid cache sample still cannot summon. */
        m->pending = 0;
    }
    m->last_sample = now;
    m->have_sample = 1;
    /* The adapter still forwards this physical event to Sony. No input is
     * suppressed while hidden, and any other native action dismisses the UI. */
    if (other && m->visible) {
        m->released = m->pending = m->fired = 0;
        return film_menu_close(m);
    }
    if (!keys_valid) return 0;
    if (!menu && !zoom) {
        m->released = 1;
        m->pending = m->fired = 0;
        return 0;
    }
    if (!m->released || m->fired) return 0;
    if (!menu || !zoom || other) { m->pending = 0; return 0; }
    if (!m->pending) { m->pending = 1; m->hold_started = now; return 0; }
    if (now - m->hold_started < 600u) return 0;
    m->fired = 1;
    if (m->visible) return film_menu_close(m);
    m->visible = 1;
    m->page = FILM_MENU_HOME;
    m->selected = m->touch_row = -1;
    m->touch_ready = m->touching = 0;
    return FILM_MENU_OPENED | FILM_MENU_REDRAW;
}
int film_menu_hit(int page, int x, int y) {
    /* Persistent Hasselblad Swiss top 4-tab bar + top-right close button. */
    if (y >= 28 && y < 66) {
        if (x >= 42 && x < 150) return FILM_HIT_TAB_HOME;
        if (x >= 156 && x < 264) return FILM_HIT_TAB_STOCKS;
        if (x >= 270 && x < 378) return FILM_HIT_TAB_DETAILS;
        if (x >= 384 && x < 492) return FILM_HIT_TAB_V4;
        if (x >= 506 && x < 596) return FILM_HIT_CLOSE;
        return -1;
    }
    /* Persistent bottom-right quick graded album action button. */
    if (x >= 420 && x < 596 && y >= 411 && y < 449) {
        return FILM_HIT_ALBUM_QUICK;
    }
    if (page == FILM_MENU_HOME) {
        /* 3x2 Hasselblad precision cell grid. */
        int col = (x >= 40 && x < 218) ? 0 :
                  ((x >= 231 && x < 409) ? 1 :
                  ((x >= 423 && x < 601) ? 2 : -1));
        int row = (y >= 82 && y < 230) ? 0 :
                  ((y >= 245 && y < 393) ? 1 : -1);
        if (col < 0 || row < 0) return -1;
        static const int home_map[2][3] = {
            {0, 1, 7},
            {4, 3, FILM_HIT_TAB_V4}
        };
        return home_map[row][col];
    }
    if (page == FILM_MENU_STOCKS || page == FILM_MENU_STOCKS_P2) {
        /* Single-page 2x5 10-film Swiss palette matrix. */
        int col = (x >= 42 && x < 312) ? 0 : ((x >= 328 && x < 598) ? 1 : -1);
        if (col < 0 || y < 84 || y >= 84 + 5 * 63) return -1;
        int dy = y - 84;
        if (dy % 63 >= 55) return -1;
        int row = dy / 63;
        return FILM_HIT_STOCK_BASE + row * 2 + col;
    }
    if (page == FILM_MENU_DETAILS) {
        if (x >= 42 && x < 598) {
            if (y >= 88 && y < 180) return 0;
            if (y >= 192 && y < 284) return 1;
            if (y >= 296 && y < 388) return 5;
        }
        return -1;
    }
    if (page == FILM_MENU_V4) {
        /* 2x2 Hasselblad V4 control grid. */
        int col = (x >= 42 && x < 312) ? 0 : ((x >= 328 && x < 598) ? 1 : -1);
        int row = (y >= 82 && y < 230) ? 0 : ((y >= 245 && y < 393) ? 1 : -1);
        if (col < 0 || row < 0) return -1;
        static const int v4_map[2][2] = {
            {0, 1},
            {3, 4}
        };
        return v4_map[row][col];
    }
    return -1;
}
int film_menu_set_stock(struct film_menu *m, int stock) {
    if (stock < 0 || stock >= 10) return -1;
    if (m->settings.stock == stock) return 0;
    m->settings.stock = stock;
    return FILM_MENU_SETTINGS_CHANGED;
}
static unsigned activate(struct film_menu *m, int row) {
    unsigned change = FILM_MENU_REDRAW;
    if (row == FILM_HIT_CLOSE) return film_menu_close(m);
    if (row == FILM_HIT_TAB_HOME) { m->page = FILM_MENU_HOME; return change; }
    if (row == FILM_HIT_TAB_STOCKS) { m->page = FILM_MENU_STOCKS; return change; }
    if (row == FILM_HIT_TAB_DETAILS) { m->page = FILM_MENU_DETAILS; return change; }
    if (row == FILM_HIT_TAB_V4) { m->page = FILM_MENU_V4; return change; }
    if (row == FILM_HIT_ALBUM_QUICK) return change | FILM_MENU_ALBUM_BROWSE;
    if (m->page == FILM_MENU_HOME) {
        if (row == 0) {
            m->settings.enabled ^= 1;
            change |= FILM_MENU_SETTINGS_CHANGED;
        } else if (row == 1) {
            m->page = FILM_MENU_STOCKS;
        } else if (row == 4) {
            m->page = FILM_MENU_DETAILS;
        } else if (row == 7) {
            change |= FILM_MENU_HIRES_TOGGLED;
        } else if (row == 3) {
#ifdef A6400_CHARO_SHUTTER
            change |= FILM_MENU_SOUND_TOGGLED;
#else
            m->page = FILM_MENU_V4;
#endif
        }
    } else if (m->page == FILM_MENU_DETAILS) {
        if (row == 0) {
            m->settings.grain = (m->settings.grain + 1) & 3;
            change |= FILM_MENU_SETTINGS_CHANGED;
        } else if (row == 1) {
            m->settings.halation ^= 1;
            change |= FILM_MENU_SETTINGS_CHANGED;
        } else if (row == 5) {
            if (m->settings.grain != 2 || m->settings.halation != 1) {
                m->settings.grain = 2;
                m->settings.halation = 1;
                change |= FILM_MENU_SETTINGS_CHANGED;
            }
        }
    } else if (m->page == FILM_MENU_V4) {
        if (row == 0) change |= FILM_MENU_POPUP_TOGGLED;
        else if (row == 1) change |= FILM_MENU_ALBUM_TOGGLED;
        else if (row == 3) change |= FILM_MENU_TOUCH_TOGGLED;
        else if (row == 4) change |= FILM_MENU_ALBUM_BROWSE;
    } else if (row >= FILM_HIT_STOCK_BASE && row < FILM_HIT_STOCK_BASE + 10) {
        int stock = row - FILM_HIT_STOCK_BASE;
        change |= (unsigned)film_menu_set_stock(m, stock);
        m->page = FILM_MENU_HOME;
    }
    return change;
}
unsigned film_menu_touch(struct film_menu *m, unsigned now, int down, int x, int y) {
    (void)now;
    if (!m->visible) return 0;
    if (!down) {
        m->touch_ready = 1;
        if (!m->touching) return 0;
        int row = m->touch_row, cancelled = m->touch_cancelled;
        m->touching = 0;
        m->selected = m->touch_row = -1;
        /* Released native samples use 32767 sentinels; activate the last valid
         * pressed target, provided the gesture never left that target. */
        return !cancelled && row >= 0 ? activate(m, row) : FILM_MENU_REDRAW;
    }
    if (!m->touch_ready) return 0;
    int row = x >= 0 && x < 640 && y >= 0 && y < 480 ? film_menu_hit(m->page, x, y) : -1;
    if (!m->touching) {
        m->touching = 1;
        m->touch_row = row;
        m->touch_cancelled = row < 0;
    } else if (row != m->touch_row) m->touch_cancelled = 1;
    m->selected = m->touch_cancelled ? -1 : row;
    return FILM_MENU_REDRAW;
}

static int equal(const char *text, unsigned n, const char *expected) {
    unsigned i = 0;
    while (i < n && expected[i] && text[i] == expected[i]) ++i;
    return i == n && !expected[i];
}
static int choice(const char *p, unsigned n, const char *const *choices, unsigned count) {
    for (unsigned i = 0; i < count; ++i) if (equal(p, n, choices[i])) return (int)i;
    return -1;
}
int film_settings_parse(struct film_settings *result, const char *text, unsigned length) {
    if (!text || !result || !length || length > 127) return -1;
    struct film_settings s = {0};
    unsigned seen = 0, at = 0;
    while (at < length) {
        unsigned first = at, split = at;
        while (at < length && text[at] != '\n') {
            if (!text[at]) return -1;
            ++at;
        }
        unsigned end = at;
        if (end > first && text[end - 1] == '\r') --end;
        while (split < end && text[split] != '=') ++split;
        if (split == end) return -1;
        const char *value = text + split + 1;
        unsigned n = end - split - 1, bit;
        if (equal(text + first, split - first, "ENABLED")) {
            bit = 1; s.enabled = equal(value, n, "0") ? 0 : equal(value, n, "1") ? 1 : -1;
        } else if (equal(text + first, split - first, "STOCK")) {
            bit = 2; s.stock = choice(value, n, stock_keys, 10);
        } else if (equal(text + first, split - first, "GRAIN")) {
            bit = 4; s.grain = choice(value, n, grain_keys, 4);
        } else if (equal(text + first, split - first, "HALATION")) {
            bit = 8; s.halation = equal(value, n, "OFF") ? 0 : equal(value, n, "ON") ? 1 : -1;
        } else return -1;
        if (seen & bit) return -1;
        seen |= bit;
        if (at < length) ++at;
    }
    if (seen != 15 || !valid(&s)) return -1;
    *result = s;
    return 0;
}
int film_settings_format(const struct film_settings *s, char *out, unsigned capacity) {
    if (!valid(s) || !out || !capacity) return -1;
    const char *parts[] = {"ENABLED=", s->enabled ? "1" : "0", "\nSTOCK=", stock_keys[s->stock],
        "\nGRAIN=", grain_keys[s->grain], "\nHALATION=", s->halation ? "ON" : "OFF", "\n"};
    unsigned at = 0;
    for (unsigned i = 0; i < sizeof parts / sizeof *parts; ++i)
        for (const char *p = parts[i]; *p; ++p) {
            if (at + 1 >= capacity) { out[0] = 0; return -1; }
            out[at++] = *p;
        }
    out[at] = 0;
    return (int)at;
}
