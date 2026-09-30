#pragma once

// nl-pi: mosquitto on 1883, Home Assistant publishes to it via mqtt_statestream.
#define MQTT_HOST "10.0.0.20"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "nldeskcyd"
#define OTA_HOSTNAME "nldeskcyd"

// HA side (configuration.yaml): mqtt_statestream base_topic.
#define TOPIC_BASE "cyd/nl/"
// HA side (automations.yaml, id cyd_nl_command).
#define TOPIC_CMD_LIGHTBAR "cyd/nl/cmd/lightbar"
// HA side (automations.yaml, id cyd_nl_basil_command).
#define TOPIC_CMD_BASIL "cyd/nl/cmd/basil"
// Our own availability (LWT) and diagnostics.
#define TOPIC_PANEL "cyd/nl/panel/"

// Home location, rounded to ~10 km (city level): only used to place the sun and moon on
// the sky background.
#define HOME_LAT 53.2
#define HOME_LON 6.6

#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"

// Night = the user is asleep in the same room. Matches the basil grow light's
// dark period (00:30-07:30), which was set for the same reason.
#define NIGHT_START_MIN (0 * 60 + 30)
#define NIGHT_END_MIN (7 * 60 + 30)

// Backlight duty (0-255). The in-use level follows the front LDR (see
// main.cpp): full in bright daylight, BL_AUTO_MIN in a dim room, BL_DARK with
// the lights off. By day the idle screen runs at half of it. Night
// (00:30-07:30) is off until touched; a touch then uses the same LDR level, so
// a dark bedroom gets BL_DARK instead of a blast (user, 2026-09-30).
#define BL_ACTIVE 255      // in use, bright daylight
#define BL_AUTO_MIN 64     // in use, dim room (idle: half of it)
#define BL_DARK 32         // in use, lights off (a touch at night)
#define BL_NIGHT_IDLE 0    // night: off until touched; alerts do not wake it

// Front LDR raw (higher = darker) mapped to the backlight, on a log scale.
// From a 3-day log in the printed case (2026-09-27..30): daylight 0-150,
// lamp-lit or overcast 200-900, dusk / curtains closed 1000-3800, dark 4095.
#define LDR_BRIGHT 100     // at or below: BL_ACTIVE
#define LDR_DIM 3500       // BL_AUTO_MIN
#define LDR_DARK 4000      // at or above (the ADC saturates at 4095): BL_DARK
#define LDR_SMOOTH 0.06f   // EMA weight per 500 ms sample (~8 s time constant)
#define IDLE_TIMEOUT_MS 30000

// Home sky frame rate while the panel is in use (see README: measured ~15 ms
// per frame, so the limit is LVGL rendering, not the SPI bus).
#define SKY_FPS_ACTIVE 30
// Idle screen: the sky is the show, so clouds move fast (x SKY_CLOUD_IDLE) and
// need enough frames to glide. In use they crawl (x SKY_CLOUD_ACTIVE) so the
// background does not pull the eye from the cards. User, 2026-09-28.
#define SKY_FPS_IDLE 12
#define SKY_CLOUD_ACTIVE 0.4f
#define SKY_CLOUD_IDLE 2.5f

// Thresholds.
#define CO2_WARN 800
#define CO2_ALERT 1200

// Electricity price (EUR/kWh) for the cost figures. Mirrors
// number_energy_price in HA's energy config (.storage/energy) -- change both.
#define PRICE_EUR_KWH 0.272f
