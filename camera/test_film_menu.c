/* Behavior tests for the controller; no camera, framebuffer or native-input writes. */
#include "film_menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned now;
static unsigned sample(struct film_menu *m, int menu, int zoom, int other) {
    now += 100;
    return film_menu_input(m, now, menu, zoom, other);
}
static void chord(struct film_menu *m) {
    sample(m, 0, 0, 0);
    for (int i = 0; i < 7; ++i) sample(m, 1, 1, 0);
}
static unsigned tap_xy(struct film_menu *m, int x, int y) {
    film_menu_touch(m, now += 20, 0, 32767, 32767);
    film_menu_touch(m, now += 20, 1, x, y);
    return film_menu_touch(m, now += 20, 0, 32767, 32767);
}
static unsigned tap(struct film_menu *m, int row) {
    if (row >= FILM_HIT_STOCK_BASE && row < FILM_HIT_STOCK_BASE + 10) {
        int s = row - FILM_HIT_STOCK_BASE;
        int col = s & 1, r = s >> 1;
        return tap_xy(m, col ? 460 : 170, 84 + r * 63 + 26);
    }
    if (row == FILM_HIT_CLOSE) return tap_xy(m, 550, 46);
    if (row == FILM_HIT_TAB_HOME) return tap_xy(m, 96, 46);
    if (row == FILM_HIT_TAB_STOCKS) return tap_xy(m, 210, 46);
    if (row == FILM_HIT_TAB_DETAILS) return tap_xy(m, 324, 46);
    if (row == FILM_HIT_TAB_V4) return tap_xy(m, 438, 46);
    if (row == FILM_HIT_ALBUM_QUICK) return tap_xy(m, 500, 430);
    if (m->page == FILM_MENU_HOME) {
        if (row == 0) return tap_xy(m, 120, 150);
        if (row == 1) return tap_xy(m, 320, 150);
        if (row == 7) return tap_xy(m, 510, 150);
        if (row == 4) return tap_xy(m, 120, 310);
        if (row == 3) return tap_xy(m, 320, 310);
        return tap_xy(m, 510, 310);
    }
    if (m->page == FILM_MENU_STOCKS || m->page == FILM_MENU_STOCKS_P2) {
        int col = row & 1, r = row >> 1;
        return tap_xy(m, col ? 460 : 170, 84 + r * 63 + 26);
    }
    if (m->page == FILM_MENU_DETAILS) {
        int idx = (row == 5) ? 2 : row;
        return tap_xy(m, 300, 88 + idx * 104 + 40);
    }
    if (m->page == FILM_MENU_V4) {
        if (row == 0) return tap_xy(m, 170, 150);
        if (row == 1) return tap_xy(m, 460, 150);
        if (row == 3) return tap_xy(m, 170, 310);
        return tap_xy(m, 460, 310);
    }
    return 0;
}
int main(void) {
    struct film_menu m;
    struct film_settings settings = {0, 0, 2, 1}, before;
    char text[128];
    film_menu_init(&m, &settings);
    /* Held keys at startup, stale observations and single keys never summon. */
    for (int i = 0; i < 12; ++i) sample(&m, 1, 1, 0);
    assert(!m.visible);
    sample(&m, 0, 0, 0);
    for (int i = 0; i < 12; ++i) sample(&m, 1, 0, 0);
    assert(!m.visible);
    sample(&m, 0, 0, 0); sample(&m, 1, 1, 0);
    now += 1000; sample(&m, 1, 1, 0);
    for (int i = 0; i < 5; ++i) sample(&m, 1, 1, 0);
    assert(!m.visible);
    assert(sample(&m, 1, 1, 0) & FILM_MENU_OPENED);
    /* A fired continuous hold must survive a dispatch gap without toggling. */
    now += 1000; sample(&m, 1, 1, 0);
    for (int i = 0; i < 10; ++i) sample(&m, 1, 1, 0);
    assert(m.visible);
    chord(&m); assert(!m.visible);
    /* Invalid cache samples still require a new full release. */
    sample(&m, 0x7fffffff, 1, 0);
    for (int i = 0; i < 10; ++i) sample(&m, 1, 1, 0);
    assert(!m.visible);
    chord(&m);
    assert(m.visible && m.page == FILM_MENU_HOME);
    for (int i = 0; i < 20; ++i) sample(&m, 1, 1, 0);
    assert(m.visible); /* One continuous hold toggles once. */
    assert(tap(&m, 0) & FILM_MENU_SETTINGS_CHANGED);
    assert(m.settings.enabled == 1);
    assert(tap(&m, 1) & FILM_MENU_REDRAW);
    assert(m.page == FILM_MENU_STOCKS);
    assert(tap(&m, 3) & FILM_MENU_SETTINGS_CHANGED);
    assert(m.settings.stock == 3 && m.page == FILM_MENU_HOME);
    assert(tap(&m, 2) & FILM_MENU_CLOSED);
    assert(!m.visible && m.settings.enabled && m.settings.stock == 3);
    for (int i = 0; i < 3; ++i) {
        chord(&m); assert(m.visible);
        assert(sample(&m, 0, 0, 1) & FILM_MENU_CLOSED);
        assert(!m.visible && m.settings.stock == 3 && m.settings.enabled);
    }
    before = m.settings;
    assert(sample(&m, 0, 0, 1) == 0); /* Hidden native input passes untouched. */
    assert(film_menu_touch(&m, now, 1, 100, 140) == 0);
    assert(memcmp(&before, &m.settings, sizeof before) == 0);
    chord(&m);
    /* Opening with a finger down does not accidentally enable/disable. */
    film_menu_touch(&m, now += 20, 1, 100, 140);
    film_menu_touch(&m, now += 20, 0, 32767, 32767);
    assert(m.settings.enabled);
    /* Dragging outside the original hit target cancels the activation. */
    film_menu_touch(&m, now += 20, 1, 100, 140);
    film_menu_touch(&m, now += 20, 1, 100, 300);
    film_menu_touch(&m, now += 20, 0, 32767, 32767);
    assert(m.settings.enabled);
    for (int stock = 0; stock < 10; ++stock) {
        tap(&m, 1);
        assert(m.page == FILM_MENU_STOCKS);
        tap(&m, FILM_HIT_STOCK_BASE + stock);
        assert(m.settings.stock == stock && m.page == FILM_MENU_HOME);
    }
    assert(strcmp(film_stock_worker_name(m.settings.stock), "polaroid_px680") == 0);
    assert(film_stock_worker_name(-1) == NULL && film_stock_worker_name(10) == NULL);
    assert(tap(&m, 4) & FILM_MENU_REDRAW);
    assert(m.page == FILM_MENU_DETAILS);
    assert(tap(&m, 0) & FILM_MENU_SETTINGS_CHANGED);
    assert(m.settings.grain == 3);
    assert(tap(&m, 1) & FILM_MENU_SETTINGS_CHANGED);
    assert(m.settings.halation == 0);
    assert(tap(&m, 5) & FILM_MENU_SETTINGS_CHANGED);
    assert(m.settings.grain == 2 && m.settings.halation == 1);
    assert(tap(&m, FILM_HIT_TAB_V4) & FILM_MENU_REDRAW);
    assert(m.page == FILM_MENU_V4);
    assert(film_menu_hit(FILM_MENU_V4, 170, 150) == 0);
    assert(film_menu_hit(FILM_MENU_V4, 460, 150) == 1);
    assert(film_menu_hit(FILM_MENU_V4, 170, 310) == 3);
    assert(film_menu_hit(FILM_MENU_V4, 460, 310) == 4);
    assert(film_menu_hit(FILM_MENU_V4, 550, 46) == FILM_HIT_CLOSE);
    assert(film_menu_hit(FILM_MENU_V4, 500, 430) == FILM_HIT_ALBUM_QUICK);
    assert(tap_xy(&m, 170, 150) & FILM_MENU_POPUP_TOGGLED);
    assert(tap_xy(&m, 460, 150) & FILM_MENU_ALBUM_TOGGLED);
    assert(tap_xy(&m, 170, 310) & FILM_MENU_TOUCH_TOGGLED);
    assert(tap_xy(&m, 460, 310) & FILM_MENU_ALBUM_BROWSE);
    assert(tap(&m, FILM_HIT_ALBUM_QUICK) & FILM_MENU_ALBUM_BROWSE);
    assert(tap(&m, FILM_HIT_TAB_DETAILS) & FILM_MENU_REDRAW);
    assert(m.page == FILM_MENU_DETAILS);
    assert(tap(&m, FILM_HIT_TAB_HOME) & FILM_MENU_REDRAW);
    assert(m.page == FILM_MENU_HOME);
    chord(&m); assert(!m.visible); chord(&m); assert(m.visible);
    /* All real persisted choices round-trip, independent of menu visibility. */
    for (int enabled = 0; enabled < 2; ++enabled)
        for (int stock = 0; stock < 10; ++stock)
            for (int grain = 0; grain < 4; ++grain)
                for (int halo = 0; halo < 2; ++halo) {
                    struct film_settings a = {enabled, stock, grain, halo}, b = {0};
                    int n = film_settings_format(&a, text, sizeof text);
                    assert(n > 0 && film_settings_parse(&b, text, (unsigned)n) == 0);
                    assert(memcmp(&a, &b, sizeof a) == 0);
                    assert(film_settings_format(&a, text, 8) < 0);
                }
    const char *bad[] = {
        "ENABLED=1\nSTOCK=UNKNOWN\nGRAIN=MEDIUM\nHALATION=ON\n",
        "ENABLED=1\nSTOCK=PORTRA_400\nGRAIN=MEDIUM\n",
        "ENABLED=1\nSTOCK=PORTRA_400\nGRAIN=MEDIUM\nHALATION=ON\nENABLED=0\n",
        "ENABLED=1\nSTOCK=PORTRA_400;reboot\nGRAIN=MEDIUM\nHALATION=ON\n",
        "ENABLED=2\nSTOCK=PORTRA_400\nGRAIN=MEDIUM\nHALATION=ON\n"
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
        struct film_settings a = before;
        assert(film_settings_parse(&a, bad[i], (unsigned)strlen(bad[i])) < 0);
        assert(memcmp(&a, &before, sizeof a) == 0);
    }
    before = m.settings;
    assert(film_menu_set_stock(&m, 10) < 0);
    assert(memcmp(&m.settings, &before, sizeof before) == 0);
    puts("FILM_MENU_TESTS_OK persistence=80 presets=5 chord=repeatable hidden_input=untouched");
    return 0;
}
