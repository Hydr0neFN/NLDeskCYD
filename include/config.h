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

// Backlight duty (0-255) per mode.
#define BL_ACTIVE 255      // touched, day or night (user's choice 2026-09-27)
#define BL_DAY_GLANCE 22   // idle during the day: dim glance mode
#define BL_DAY_ALERT 100   // idle during the day with an alert pending
#define BL_NIGHT_IDLE 0    // night: off until touched; alerts do not wake it
#define IDLE_TIMEOUT_MS 30000

// Home sky frame rate while the panel is in use (see README: measured ~15 ms
// per frame, so the limit is LVGL rendering, not the SPI bus).
#define SKY_FPS_ACTIVE 30

// Thresholds.
#define CO2_WARN 800
#define CO2_ALERT 1200
