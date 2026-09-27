// Power detail: 24 h line chart (house measured + PC estimate on one axis, so
// the PC's share of the draw is visible) or 7-day kWh bars, picked by tabs.

#include <Arduino.h>

#include "config.h"
#include "ui_common.h"

static constexpr int CX = 48, CY = 82, CW = 260, CH = 88;   // line chart
static constexpr int BX = 20, BY = 98, BW = 288, BH = 72;   // bar chart
static const char *const TABS[2] = {"24h 功率", "7天用電"};
static const char *const WEEKDAY[7] = {"日", "一", "二", "三", "四", "五", "六"};

static Series s_house, s_pc, s_daily;
static uint32_t s_seen_house, s_seen_pc, s_seen_daily;
static int s_view = 0;
static bool s_dirty = true;

static lv_obj_t *tabs[2], *today, *line_view, *bar_view, *chart, *bars, *y_hi, *y_lo;
static lv_obj_t *bar_val[7], *bar_day[7], *f_house, *f_pc;
static lv_chart_series_t *ser_house, *ser_pc, *ser_daily;

static void on_tab(lv_event_t *e) {
  s_view = (int)(intptr_t)lv_event_get_user_data(e);
  s_dirty = true;
}

// Transparent full-card container so a whole view can be hidden at once.
static lv_obj_t *mk_view(lv_obj_t *scr) {
  lv_obj_t *v = lv_obj_create(scr);
  lv_obj_remove_style_all(v);
  lv_obj_set_pos(v, 0, 0);
  lv_obj_set_size(v, 320, 240);
  lv_obj_remove_flag(v, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE);
  return v;
}

lv_obj_t *power_build() {
  lv_obj_t *scr = mk_screen();
  mk_header(scr, "用電");
  today = mk_label(scr, &font_noto_16, C_DIM, "");
  lv_obj_align(today, LV_ALIGN_TOP_RIGHT, -8, 9);
  mk_tabs(scr, 4, 38, 312, 32, TABS, 2, tabs, on_tab);
  mk_card(scr, 4, 74, 312, 122, nullptr);

  // 24 h view
  line_view = mk_view(scr);
  chart = mk_chart(line_view, CX, CY, CW, CH, HIST_MAX);
  ser_house = lv_chart_add_series(chart, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
  ser_pc = lv_chart_add_series(chart, C_CYAN, LV_CHART_AXIS_PRIMARY_Y);
  y_hi = mk_label(line_view, &lv_font_montserrat_14, C_DIM, "");
  lv_obj_set_pos(y_hi, 10, CY - 4);
  y_lo = mk_label(line_view, &lv_font_montserrat_14, C_DIM, "0");
  lv_obj_set_pos(y_lo, 10, CY + CH - 12);
  lv_obj_t *t = mk_label(line_view, &lv_font_montserrat_14, C_DIM, "-24h");
  lv_obj_set_pos(t, CX, CY + CH + 4);
  t = mk_label(line_view, &font_noto_16, C_ACCENT, "全屋");
  lv_obj_set_pos(t, CX + 84, CY + CH + 2);
  t = mk_label(line_view, &font_noto_16, C_CYAN, "電腦");
  lv_obj_set_pos(t, CX + 136, CY + CH + 2);
  t = mk_label(line_view, &lv_font_montserrat_14, C_DIM, "now");
  lv_obj_set_pos(t, CX + CW - 26, CY + CH + 4);

  // 7-day view
  bar_view = mk_view(scr);
  bars = mk_chart(bar_view, BX, BY, BW, BH, 7);
  lv_chart_set_type(bars, LV_CHART_TYPE_BAR);
  lv_chart_set_div_line_count(bars, 0, 0);
  lv_obj_set_style_pad_column(bars, 14, 0);
  lv_obj_set_style_radius(bars, 3, LV_PART_ITEMS);
  ser_daily = lv_chart_add_series(bars, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
  for (int i = 0; i < 7; i++) {
    int cx = BX + i * BW / 7;
    bar_val[i] = mk_label(bar_view, &lv_font_montserrat_14, C_TEXT, "");
    lv_obj_set_width(bar_val[i], BW / 7);
    lv_obj_set_style_text_align(bar_val[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(bar_val[i], cx, BY - 18);
    bar_day[i] = mk_label(bar_view, &font_noto_16, C_DIM, "");
    lv_obj_set_width(bar_day[i], BW / 7);
    lv_obj_set_style_text_align(bar_day[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(bar_day[i], cx, BY + BH + 2);
  }

  lv_obj_t *c = mk_card(scr, 4, 200, 312, 36, nullptr);
  lv_obj_set_style_pad_ver(c, 0, 0);
  f_house = mk_label(c, &font_noto_16, C_ACCENT, "");
  lv_obj_align(f_house, LV_ALIGN_LEFT_MID, 0, 0);
  f_pc = mk_label(c, &font_noto_16, C_CYAN, "");
  lv_obj_align(f_pc, LV_ALIGN_RIGHT_MID, 0, 0);
  return scr;
}

static void load_line() {
  int lo = 0, hi = 0, lo2 = 0, hi2 = 0;
  bool a = s_house.n && series_range(s_house, lo, hi);
  bool b = s_pc.n && series_range(s_pc, lo2, hi2);
  int top = max(a ? hi : 0, b ? hi2 : 0);
  top = max(200, (top * 115 / 100 + 99) / 100 * 100);
  lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, top);
  if (a) chart_load(chart, ser_house, s_house);
  if (b) chart_load(chart, ser_pc, s_pc);
  char buf[12];
  snprintf(buf, sizeof(buf), "%dW", top);
  set_text(y_hi, buf);
}

static void load_bars() {
  int n = min((int)s_daily.n, 7);
  int top = 0;
  for (int i = 0; i < n; i++) top = max(top, (int)s_daily.v[i]);
  top = max(200, (top * 115 / 100 + 199) / 200 * 200);  // kWh x100, 2 kWh steps
  lv_chart_set_axis_range(bars, LV_CHART_AXIS_PRIMARY_Y, 0, top);
  if (n) chart_load(bars, ser_daily, s_daily);
  char buf[24];
  // 7-day cost goes into the tab label, so the chart keeps its space.
  long total = 0;  // kWh x100
  for (int i = 0; i < n; i++) total += s_daily.v[i];
  if (n) snprintf(buf, sizeof(buf), "7天 €%.2f", total / 100.0f * PRICE_EUR_KWH);
  else strlcpy(buf, TABS[1], sizeof(buf));
  set_text(lv_obj_get_child(tabs[1], 0), buf);
  for (int i = 0; i < 7; i++) {
    if (i < n) {
      snprintf(buf, sizeof(buf), "%.1f", s_daily.v[i] / 100.0f);
      set_text(bar_val[i], buf);
      set_text(bar_day[i], i == n - 1 ? "今" : WEEKDAY[s_daily.dow[i] % 7]);
    } else {
      set_text(bar_val[i], "");
      set_text(bar_day[i], "");
    }
  }
}

void power_refresh(const Model &m, bool live) {
  bool line_new = net_hist(H_HOUSE, s_house, s_seen_house) | net_hist(H_PC, s_pc, s_seen_pc);
  bool bar_new = net_hist(H_DAILY, s_daily, s_seen_daily);
  if (s_dirty) {
    tabs_select(tabs, 2, s_view);
    set_hidden(line_view, s_view != 0);
    set_hidden(bar_view, s_view != 1);
  }
  if (s_view == 0 && (line_new || s_dirty)) load_line();
  if (s_view == 1 && (bar_new || s_dirty)) load_bars();
  s_dirty = false;

  // Today's house kWh: the recorder-derived bar is right from the first day;
  // the utility meter only counts from when it was created.
  float kwh = s_daily.n ? s_daily.v[s_daily.n - 1] / 100.0f : m.house_kwh;
  char buf[32], num[12];
  if (isnan(kwh)) buf[0] = '\0';
  else snprintf(buf, sizeof(buf), "今日 %.1f kWh · €%.2f", kwh, kwh * PRICE_EUR_KWH);
  set_text(today, buf);

  fmt(num, sizeof(num), m.house_w, 0);
  snprintf(buf, sizeof(buf), "全屋 %s W", num);
  set_text(f_house, buf);
  set_text_color(f_house, live ? C_ACCENT : C_STALE);
  if (isnan(m.pc_w)) {
    set_text(f_pc, "電腦 已關機");
  } else if (isnan(m.pc_kwh)) {
    snprintf(buf, sizeof(buf), "電腦 %.0f W", m.pc_w);
    set_text(f_pc, buf);
  } else {
    snprintf(buf, sizeof(buf), "電腦 %.0f W  %.2f kWh", m.pc_w, m.pc_kwh);
    set_text(f_pc, buf);
  }
  set_text_color(f_pc, live && !isnan(m.pc_w) ? C_CYAN : C_STALE);
}
