#include "ui_common.h"

#include <Arduino.h>
#include <math.h>

lv_obj_t *mk_screen() {
  lv_obj_t *s = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s, C_BG, 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t *mk_box(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, bg, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static void style_card(lv_obj_t *c) {
  lv_obj_set_style_bg_color(c, C_CARD, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(c, C_BORDER, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_radius(c, 8, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *mk_card(lv_obj_t *parent, int x, int y, int w, int h, const char *title) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  style_card(c);
  lv_obj_set_style_pad_all(c, 8, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
  if (title && *title) {
    lv_obj_t *t = mk_label(c, &font_noto_16, C_DIM, title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, -3);
  }
  return c;
}

lv_obj_t *mk_button(lv_obj_t *parent, int x, int y, int w, int h, lv_event_cb_t cb, void *ud) {
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  style_card(b);
  // Instant touch-down feedback: resistive touch has no haptic confirmation.
  lv_obj_set_style_bg_color(b, C_BORDER, LV_STATE_PRESSED);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

static void on_back(lv_event_t *) { ui_show(Page::HOME); }

void mk_header(lv_obj_t *screen, const char *title) {
  lv_obj_t *b = mk_button(screen, 4, 2, 52, 32, on_back, nullptr);
  lv_obj_t *t = mk_label(b, &lv_font_montserrat_20, C_TEXT, LV_SYMBOL_LEFT);
  lv_obj_center(t);
  t = mk_label(screen, &font_noto_20, C_TEXT, title);
  lv_obj_set_pos(t, 64, 6);
}

void mk_tabs(lv_obj_t *parent, int x, int y, int w, int h, const char *const *labels, int n,
             lv_obj_t **out, lv_event_cb_t cb) {
  const int gap = 4;
  int bw = (w - gap * (n - 1)) / n;
  for (int i = 0; i < n; i++) {
    out[i] = mk_button(parent, x + i * (bw + gap), y, bw, h, cb, (void *)(intptr_t)i);
    lv_obj_t *l = mk_label(out[i], &font_noto_16, C_DIM, labels[i]);
    lv_obj_center(l);
  }
}

void tabs_select(lv_obj_t **tabs, int n, int selected) {
  for (int i = 0; i < n; i++) {
    bool sel = i == selected;
    set_bg(tabs[i], sel ? C_BORDER : C_CARD);
    lv_obj_set_style_border_color(tabs[i], sel ? C_DIM : C_BORDER, 0);
    set_text_color(lv_obj_get_child(tabs[i], 0), sel ? C_TEXT : C_DIM);
  }
}

lv_obj_t *mk_chart(lv_obj_t *parent, int x, int y, int w, int h, int points) {
  lv_obj_t *c = lv_chart_create(parent);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_chart_set_type(c, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(c, points);
  lv_chart_set_div_line_count(c, 3, 0);  // horizontal grid only
  lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, 0, 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_set_style_line_color(c, C_GRID, LV_PART_MAIN);
  lv_obj_set_style_line_width(c, 1, LV_PART_MAIN);
  lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
  lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);  // no point markers
  lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

void set_text(lv_obj_t *l, const char *t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

void set_text_color(lv_obj_t *o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) lv_obj_set_style_text_color(o, c, 0);
}

void set_bg(lv_obj_t *o, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), c)) lv_obj_set_style_bg_color(o, c, 0);
}

void set_bg_opa(lv_obj_t *o, lv_opa_t opa) {
  if (lv_obj_get_style_bg_opa(o, LV_PART_MAIN) != opa) lv_obj_set_style_bg_opa(o, opa, 0);
}

void set_border_w(lv_obj_t *o, int w) {
  if (lv_obj_get_style_border_width(o, LV_PART_MAIN) != w) lv_obj_set_style_border_width(o, w, 0);
}

void set_hidden(lv_obj_t *o, bool hidden) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != hidden) {
    if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

void set_width(lv_obj_t *o, int w) {
  if (lv_obj_get_style_width(o, LV_PART_MAIN) != w) lv_obj_set_width(o, w);
}

void fmt(char *buf, size_t n, float v, int decimals) {
  if (isnan(v)) strlcpy(buf, "--", n);
  else snprintf(buf, n, "%.*f", decimals, v);
}

bool series_range(const Series &s, int &lo, int &hi) {
  bool any = false;
  for (int i = 0; i < s.n; i++) {
    if (s.v[i] == HIST_NONE) continue;
    if (!any) lo = hi = s.v[i];
    lo = min(lo, (int)s.v[i]);
    hi = max(hi, (int)s.v[i]);
    any = true;
  }
  return any;
}

int16_t series_last(const Series &s) {
  for (int i = s.n - 1; i >= 0; i--) {
    if (s.v[i] != HIST_NONE) return s.v[i];
  }
  return HIST_NONE;
}

void chart_load(lv_obj_t *chart, lv_chart_series_t *ser, const Series &s) {
  static int32_t vals[HIST_MAX];
  for (int i = 0; i < s.n; i++) vals[i] = s.v[i] == HIST_NONE ? LV_CHART_POINT_NONE : s.v[i];
  if ((int)lv_chart_get_point_count(chart) != s.n) lv_chart_set_point_count(chart, s.n);
  lv_chart_set_series_values(chart, ser, vals, s.n);
  lv_chart_refresh(chart);
}

void chart_hline(lv_obj_t *line, int cx, int cy, int cw, int ch, int v, int lo, int hi) {
  if (hi <= lo || v < lo || v > hi) {
    set_hidden(line, true);
    return;
  }
  int y = cy + ch - 1 - (int)((long)(v - lo) * (ch - 1) / (hi - lo));
  lv_obj_set_pos(line, cx, y);
  set_width(line, cw);
  set_hidden(line, false);
}

static int floor_to(int v, int step) { return (v >= 0 ? v / step : -((-v + step - 1) / step)) * step; }
static int ceil_to(int v, int step) { return -floor_to(-v, step); }

void nice_range(int &lo, int &hi, int pad, int step, int min_span) {
  lo = floor_to(lo - pad, step);
  hi = ceil_to(hi + pad, step);
  if (hi - lo < min_span) {
    int extra = min_span - (hi - lo);
    lo -= extra / 2;
    hi = lo + min_span;
    lo = floor_to(lo, step);
    hi = ceil_to(hi, step);
  }
}
