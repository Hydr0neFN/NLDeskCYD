#pragma once
// Shared palette, widget helpers and page plumbing for the ui_*.cpp pages.
// Everything here runs on core 1 (LVGL) only.

#include <lvgl.h>

#include "net.h"

// --- Palette (agy design review 2026-09-26) ---------------------------------
#define C_BG lv_color_hex(0x0B0E14)
#define C_CARD lv_color_hex(0x161B22)
#define C_BORDER lv_color_hex(0x30363D)
#define C_GRID lv_color_hex(0x21262D)
#define C_TEXT lv_color_hex(0xE6EDF3)
#define C_DIM lv_color_hex(0x8B949E)
#define C_STALE lv_color_hex(0x484F58)
#define C_ACCENT lv_color_hex(0xD29922)
#define C_OK lv_color_hex(0x3FB950)
#define C_WARN lv_color_hex(0xD29922)
#define C_ALERT lv_color_hex(0xF85149)
#define C_BLUE lv_color_hex(0x58A6FF)
#define C_ORANGE lv_color_hex(0xF0883E)
#define C_CYAN lv_color_hex(0x79C0FF)
#define C_WARM lv_color_hex(0xFFB45A)
#define C_COOL lv_color_hex(0xCFE3FF)
#define C_SEG_OFF lv_color_hex(0x21262D)

enum class Page : uint8_t { HOME, LIGHT, ROOM, POWER, BASIL, SWATCH };

// Navigation (instant loads only: no TE pin, animated transitions tear).
void ui_show(Page p);
Page ui_page();

// --- Widget constructors -------------------------------------------------------
lv_obj_t *mk_screen();
lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);
// Plain filled rectangle, not clickable.
lv_obj_t *mk_box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg);
// Card with a dim CJK title in its top-left corner. Not clickable.
lv_obj_t *mk_card(lv_obj_t *parent, int x, int y, int w, int h, const char *title);
// Clickable card-styled button with pressed feedback. cb fires on CLICKED.
lv_obj_t *mk_button(lv_obj_t *parent, int x, int y, int w, int h, lv_event_cb_t cb, void *ud);
// Detail-page header: back button (-> home) and title. Returns nothing; the
// right side (x >= 200) is free for a page-specific control.
void mk_header(lv_obj_t *screen, const char *title);
// Segmented selector: n equal buttons across (x, y, w, h). Returns the button
// objects in `out`; cb receives the index as user data.
void mk_tabs(lv_obj_t *parent, int x, int y, int w, int h, const char *const *labels, int n,
             lv_obj_t **out, lv_event_cb_t cb);
void tabs_select(lv_obj_t **tabs, int n, int selected);

// Line chart styled for this palette: no point markers, horizontal grid only.
lv_obj_t *mk_chart(lv_obj_t *parent, int x, int y, int w, int h, int points);

// --- Change-guarded setters -------------------------------------------------------
// LVGL invalidates an object on every local style write even if the value is
// unchanged, and ui_refresh runs 5x/s: unguarded writes would repaint the whole
// panel over SPI continuously.
void set_text(lv_obj_t *l, const char *t);
void set_text_color(lv_obj_t *o, lv_color_t c);
void set_bg(lv_obj_t *o, lv_color_t c);
void set_bg_opa(lv_obj_t *o, lv_opa_t opa);
void set_border_w(lv_obj_t *o, int w);
void set_hidden(lv_obj_t *o, bool hidden);
void set_width(lv_obj_t *o, int w);

// "--" for NaN, otherwise printf with the given decimals.
void fmt(char *buf, size_t n, float v, int decimals);

// --- Series helpers -----------------------------------------------------------------
// Min/max over non-empty points; false if the series has none.
bool series_range(const Series &s, int &lo, int &hi);
// Loads a Series into a chart series (HIST_NONE -> gap).
void chart_load(lv_obj_t *chart, lv_chart_series_t *ser, const Series &s);
// Positions a 1 px horizontal marker `line` (a mk_box sibling of the chart) at
// value v of a chart spanning lo..hi drawn at (cx, cy, cw, ch). Hidden if v is
// outside the range.
void chart_hline(lv_obj_t *line, int cx, int cy, int cw, int ch, int v, int lo, int hi);
// Rounds lo down / hi up to multiples of `step` after padding by `pad`,
// keeping at least `min_span` between them.
void nice_range(int &lo, int &hi, int pad, int step, int min_span);
// Last non-empty value, HIST_NONE if none.
int16_t series_last(const Series &s);

// --- Pages -------------------------------------------------------------------------
lv_obj_t *home_build();
void home_refresh(const Model &m, bool live);

lv_obj_t *light_build();
void light_refresh(const Model &m);
// Light-bar state as displayed (optimistic value while a command is in flight).
bool light_disp_on(const Model &m);
int light_disp_level(const Model &m);
int light_disp_kelvin(const Model &m);
void light_toggle(const Model &m);
// Sends coalesced stepper commands and expires stale optimistic values.
void light_tick(const Model &m);

lv_obj_t *room_build();
void room_refresh(const Model &m, bool live);

lv_obj_t *power_build();
void power_refresh(const Model &m, bool live);

lv_obj_t *basil_build();
void basil_refresh(const Model &m, bool live);

// Animated weather sky behind the home screen (ui_sky.cpp).
void sky_build(lv_obj_t *scr);       // call first, so the layers sit under everything
void sky_update(const Model &m);     // reconfigures when condition or sun phase changes
void sky_set_fps(int fps);           // 0 = still sky
// (frames are driven by an internal lv_timer)
const char *sky_label(const char *ha_cond);  // "partlycloudy" -> "晴時多雲"

lv_obj_t *swatch_build();
void swatch_refresh();
