#pragma once

#include "net.h"

// Builds all screens and loads the home screen. Core 1 only.
void ui_init();

// Repaints the visible screen from the model and flushes pending commands.
// Call periodically from loop() (core 1).
void ui_refresh(const Model &m);

// True when something needs the user's attention (drain fault, empty tank,
// CO2 at the alert band). Used to lift the idle backlight during the day.
bool ui_has_alert(const Model &m);

// True between NIGHT_START_MIN and NIGHT_END_MIN local time. False while the
// clock is not yet synced.
bool ui_is_night();

// Back to the home screen (called when the panel goes idle).
void ui_go_home();

// Idle layer on the home screen (big clock, basic info) instead of the cards.
void ui_set_idle(bool idle);

// Display colour inversion, owned by main.cpp (it holds the TFT driver). The
// colour-check page toggles it so both states can be compared on the panel.
void display_set_inverted(bool inverted);
bool display_is_inverted();
