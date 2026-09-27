// Hourly forecast (long-press the outdoor weather on the home header): the next
// 12 hours from HA's weather.get_forecasts, as a 6 x 2 grid. Each cell: hour,
// a colour chip in the condition's sky colour, a short condition label, the
// temperature, and rain in mm when there is any. The current hour is outlined.

#include <Arduino.h>

#include "ui_common.h"

static constexpr int COLS = 6, CELL_W = 49, CELL_H = 96, GAP = 4, X0 = 3;
static const int ROW_Y[2] = {40, 140};

struct Cell {
  lv_obj_t *box, *chip, *hour, *cond, *temp, *rain;
};
static Cell s_cells[FC_MAX];
static lv_obj_t *s_summary, *s_empty;
static Forecast s_fc;
static uint32_t s_seen = 0;
static bool s_dirty = true;

lv_obj_t *forecast_build() {
  // Built on every open (ui_show): force a full reload of the cached forecast.
  s_seen = 0;
  s_dirty = true;
  lv_obj_t *scr = mk_screen();
  mk_header(scr, "逐時預報");
  s_summary = mk_label(scr, &font_noto_16, C_DIM, "");
  lv_obj_align(s_summary, LV_ALIGN_TOP_RIGHT, -8, 9);

  for (int i = 0; i < FC_MAX; i++) {
    Cell &c = s_cells[i];
    int x = X0 + (i % COLS) * (CELL_W + GAP), y = ROW_Y[i / COLS];
    c.box = mk_card(scr, x, y, CELL_W, CELL_H, nullptr);
    lv_obj_set_style_pad_all(c.box, 0, 0);
    c.chip = mk_box(c.box, 6, 6, CELL_W - 14, 4, C_DIM);
    lv_obj_set_style_radius(c.chip, 2, 0);
    c.hour = mk_label(c.box, &lv_font_montserrat_14, C_DIM, "");
    lv_obj_align(c.hour, LV_ALIGN_TOP_MID, 0, 13);
    c.cond = mk_label(c.box, &font_noto_16_bold, C_TEXT, "");
    lv_obj_align(c.cond, LV_ALIGN_TOP_MID, 0, 32);
    c.temp = mk_label(c.box, &lv_font_montserrat_20, C_TEXT, "");
    lv_obj_align(c.temp, LV_ALIGN_TOP_MID, 0, 52);
    c.rain = mk_label(c.box, &lv_font_montserrat_14, C_BLUE, "");
    lv_obj_align(c.rain, LV_ALIGN_TOP_MID, 0, 76);
  }
  s_empty = mk_label(scr, &font_noto_16, C_DIM, "--");
  lv_obj_center(s_empty);
  return scr;
}

static void load() {
  set_hidden(s_empty, s_fc.n > 0);
  int lo = 99, hi = -99;
  float rain_total = 0;
  char buf[24];
  for (int i = 0; i < FC_MAX; i++) {
    Cell &c = s_cells[i];
    bool on = i < s_fc.n;
    set_hidden(c.box, !on);
    if (!on) continue;
    snprintf(buf, sizeof(buf), "%02d:00", s_fc.hour[i]);
    set_text(c.hour, buf);
    set_text(c.cond, sky_short_label(s_fc.cond[i]));
    set_bg(c.chip, sky_cond_color(s_fc.cond[i]));
    snprintf(buf, sizeof(buf), "%d°", s_fc.temp[i]);
    set_text(c.temp, buf);
    if (s_fc.rain[i] >= 0.1f) snprintf(buf, sizeof(buf), "%.1fmm", s_fc.rain[i]);
    else buf[0] = 0;
    set_text(c.rain, buf);
    // The first row is the current hour: outline it.
    lv_obj_set_style_border_color(c.box, i == 0 ? C_ACCENT : C_BORDER, 0);
    lo = min(lo, (int)s_fc.temp[i]);
    hi = max(hi, (int)s_fc.temp[i]);
    rain_total += s_fc.rain[i];
  }
  if (!s_fc.n) {
    set_text(s_summary, "");
  } else if (rain_total >= 0.1f) {
    snprintf(buf, sizeof(buf), "%d-%d°  雨 %.1fmm", lo, hi, rain_total);
    set_text(s_summary, buf);
  } else {
    snprintf(buf, sizeof(buf), "%d-%d°  無雨", lo, hi);
    set_text(s_summary, buf);
  }
}

void forecast_refresh() {
  if (net_forecast(s_fc, s_seen)) s_dirty = true;
  if (s_dirty) {
    load();
    s_dirty = false;
  }
}
