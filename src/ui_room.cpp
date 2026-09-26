// Room detail: one 24 h chart, the metric picked by three tabs (stacked small
// charts are unreadable at 240 px). CO2 gets its 800 / 1200 band lines.

#include <Arduino.h>

#include "config.h"
#include "ui_common.h"

enum Metric { M_TEMP, M_HUM, M_CO2, M_COUNT };
static const HistKey KEYS[M_COUNT] = {H_TEMP, H_HUM, H_CO2};
static const char *const TAB_INIT[M_COUNT] = {"溫度", "濕度", "CO2"};

// Chart geometry (screen coordinates): card 4,78 312x120.
static constexpr int CX = 48, CY = 86, CW = 260, CH = 92;

static Series s_hist[M_COUNT];
static uint32_t s_seen[M_COUNT];
static int s_metric = M_TEMP;
static bool s_dirty = true;

static lv_obj_t *tabs[M_COUNT], *co2_status, *chart, *y_hi, *y_lo, *line_warn, *line_alert;
static lv_obj_t *f_left, *f_right, *empty_lbl;
static lv_chart_series_t *ser;

static void on_tab(lv_event_t *e) {
  s_metric = (int)(intptr_t)lv_event_get_user_data(e);
  s_dirty = true;
}

lv_obj_t *room_build() {
  lv_obj_t *scr = mk_screen();
  mk_header(scr, "室內環境");
  co2_status = mk_label(scr, &font_noto_16, C_DIM, "");
  lv_obj_align(co2_status, LV_ALIGN_TOP_RIGHT, -8, 9);

  mk_tabs(scr, 4, 38, 312, 36, TAB_INIT, M_COUNT, tabs, on_tab);

  mk_card(scr, 4, 78, 312, 120, nullptr);
  line_warn = mk_box(scr, CX, CY, CW, 1, C_WARN);
  lv_obj_set_style_bg_opa(line_warn, LV_OPA_50, 0);
  line_alert = mk_box(scr, CX, CY, CW, 1, C_ALERT);
  lv_obj_set_style_bg_opa(line_alert, LV_OPA_50, 0);
  chart = mk_chart(scr, CX, CY, CW, CH, HIST_MAX);
  ser = lv_chart_add_series(chart, C_ORANGE, LV_CHART_AXIS_PRIMARY_Y);
  y_hi = mk_label(scr, &lv_font_montserrat_14, C_DIM, "");
  lv_obj_set_pos(y_hi, 10, CY - 4);
  y_lo = mk_label(scr, &lv_font_montserrat_14, C_DIM, "");
  lv_obj_set_pos(y_lo, 10, CY + CH - 12);
  lv_obj_t *t = mk_label(scr, &lv_font_montserrat_14, C_DIM, "-24h");
  lv_obj_set_pos(t, CX, CY + CH + 2);
  t = mk_label(scr, &lv_font_montserrat_14, C_DIM, "-12h");
  lv_obj_set_pos(t, CX + CW / 2 - 14, CY + CH + 2);
  t = mk_label(scr, &lv_font_montserrat_14, C_DIM, "now");
  lv_obj_set_pos(t, CX + CW - 26, CY + CH + 2);
  empty_lbl = mk_label(scr, &font_noto_16, C_DIM, "--");
  lv_obj_set_pos(empty_lbl, CX + CW / 2 - 8, CY + CH / 2 - 10);

  lv_obj_t *c = mk_card(scr, 4, 202, 312, 34, nullptr);
  lv_obj_set_style_pad_ver(c, 0, 0);
  f_left = mk_label(c, &font_noto_16, C_DIM, "");
  lv_obj_align(f_left, LV_ALIGN_LEFT_MID, 0, 0);
  f_right = mk_label(c, &font_noto_16, C_DIM, "");
  lv_obj_align(f_right, LV_ALIGN_RIGHT_MID, 0, 0);
  return scr;
}

static void load_chart() {
  const Series &s = s_hist[s_metric];
  int lo = 0, hi = 0;
  bool any = s.n && series_range(s, lo, hi);
  set_hidden(empty_lbl, any);
  set_hidden(chart, !any);

  lv_color_t col = s_metric == M_TEMP ? C_ORANGE : (s_metric == M_HUM ? C_BLUE : C_OK);
  lv_chart_set_series_color(chart, ser, col);

  // Axis: temperature auto +-1 C around the data, humidity at least 30-70 %,
  // CO2 fixed 400..1600 (grows only if the data exceeds it).
  if (s_metric == M_TEMP) nice_range(lo, hi, 5, 10, 20);  // x10 units
  else if (s_metric == M_HUM) { lo = min(lo - 2, 30); hi = max(hi + 2, 70); nice_range(lo, hi, 0, 10, 20); }
  else { lo = min(400, lo); hi = max(1600, hi + 100); nice_range(lo, hi, 0, 100, 400); }
  lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);
  if (any) chart_load(chart, ser, s);

  char buf[16];
  if (s_metric == M_TEMP) {
    snprintf(buf, sizeof(buf), "%d°", hi / 10);
    set_text(y_hi, buf);
    snprintf(buf, sizeof(buf), "%d°", lo / 10);
    set_text(y_lo, buf);
  } else {
    snprintf(buf, sizeof(buf), s_metric == M_HUM ? "%d%%" : "%d", hi);
    set_text(y_hi, buf);
    snprintf(buf, sizeof(buf), s_metric == M_HUM ? "%d%%" : "%d", lo);
    set_text(y_lo, buf);
  }

  bool co2 = s_metric == M_CO2;
  if (co2) {
    chart_hline(line_warn, CX, CY, CW, CH, CO2_WARN, lo, hi);
    chart_hline(line_alert, CX, CY, CW, CH, CO2_ALERT, lo, hi);
  } else {
    set_hidden(line_warn, true);
    set_hidden(line_alert, true);
  }

  // 24 h mean for the footer
  long sum = 0;
  int n = 0;
  for (int i = 0; i < s.n; i++) {
    if (s.v[i] != HIST_NONE) {
      sum += s.v[i];
      n++;
    }
  }
  char f[32];
  if (!n) f[0] = '\0';
  else if (s_metric == M_TEMP) snprintf(f, sizeof(f), "24h 均 %.1f°C", sum / (float)n / 10.0f);
  else if (s_metric == M_HUM) snprintf(f, sizeof(f), "24h 均 %ld%%", sum / n);
  else snprintf(f, sizeof(f), "24h 均 %ld ppm", sum / n);
  set_text(f_right, f);
}

void room_refresh(const Model &m, bool live) {
  for (int i = 0; i < M_COUNT; i++) {
    if (net_hist(KEYS[i], s_hist[i], s_seen[i]) && i == s_metric) s_dirty = true;
  }
  if (s_dirty) {
    tabs_select(tabs, M_COUNT, s_metric);
    load_chart();
    s_dirty = false;
  }

  char buf[24], num[12];
  fmt(num, sizeof(num), m.temp, 1);
  snprintf(buf, sizeof(buf), "溫度 %s°", num);
  set_text(lv_obj_get_child(tabs[M_TEMP], 0), buf);
  fmt(num, sizeof(num), m.hum, 0);
  snprintf(buf, sizeof(buf), "濕度 %s%%", num);
  set_text(lv_obj_get_child(tabs[M_HUM], 0), buf);
  fmt(num, sizeof(num), m.co2, 0);
  snprintf(buf, sizeof(buf), "CO2 %s", num);
  set_text(lv_obj_get_child(tabs[M_CO2], 0), buf);

  if (!live || isnan(m.co2)) {
    set_text(co2_status, "");
  } else if (m.co2 >= CO2_ALERT) {
    set_text(co2_status, "CO2 過高 請開窗");
    set_text_color(co2_status, C_ALERT);
  } else if (m.co2 >= CO2_WARN) {
    set_text(co2_status, "CO2 偏高");
    set_text_color(co2_status, C_WARN);
  } else {
    set_text(co2_status, "空氣良好");
    set_text_color(co2_status, C_OK);
  }

  fmt(num, sizeof(num), m.pressure, 0);
  snprintf(buf, sizeof(buf), "氣壓 %s hPa", num);
  set_text(f_left, buf);
}
