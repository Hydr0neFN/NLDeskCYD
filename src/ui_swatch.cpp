// Panel colour check (long-press the clock on the home screen). Shows every
// palette colour with its hex value, plus a toggle for display inversion, so
// one photo of the panel settles whether the colours are right.
//
// Why it exists: PCDeskCYD only ever ran a light theme on this board; the dark
// palette's first photo showed near-black as saturated blue.

#include <Arduino.h>

#include "ui.h"
#include "ui_common.h"

static lv_obj_t *inv_lbl;

struct Swatch {
  uint32_t rgb;
  const char *name;
};
static const Swatch SWATCHES[] = {
    {0x000000, "000000"}, {0x0B0E14, "0B0E14"}, {0x161B22, "161B22"}, {0x30363D, "30363D"},
    {0x484F58, "484F58"}, {0x8B949E, "8B949E"}, {0xE6EDF3, "E6EDF3"}, {0xFFFFFF, "FFFFFF"},
    {0xD29922, "D29922"}, {0xF85149, "F85149"}, {0x3FB950, "3FB950"}, {0x58A6FF, "58A6FF"},
};

static void on_invert(lv_event_t *) { display_set_inverted(!display_is_inverted()); }

lv_obj_t *swatch_build() {
  lv_obj_t *scr = mk_screen();
  mk_header(scr, "螢幕校色");
  lv_obj_t *b = mk_button(scr, 200, 2, 116, 32, on_invert, nullptr);
  inv_lbl = mk_label(b, &lv_font_montserrat_14, C_TEXT, "");
  lv_obj_center(inv_lbl);

  for (int i = 0; i < 12; i++) {
    int x = 4 + (i % 4) * 79, y = 38 + (i / 4) * 67;
    lv_obj_t *s = mk_box(scr, x, y, 75, 63, lv_color_hex(SWATCHES[i].rgb));
    lv_obj_set_style_border_color(s, lv_color_hex(0x808080), 0);
    lv_obj_set_style_border_width(s, 1, 0);
    // label colour: black on light swatches, white on dark ones
    uint32_t c = SWATCHES[i].rgb;
    int lum = ((c >> 16) & 0xFF) * 3 + ((c >> 8) & 0xFF) * 6 + (c & 0xFF);
    lv_obj_t *l = mk_label(s, &lv_font_montserrat_14, lum > 1000 ? lv_color_black() : lv_color_white(),
                           SWATCHES[i].name);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -2);
  }
  return scr;
}

void swatch_refresh() { set_text(inv_lbl, display_is_inverted() ? "INVERT ON" : "INVERT OFF"); }
