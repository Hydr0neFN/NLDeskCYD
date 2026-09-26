# NLDeskCYD

[繁體中文](README.zh-TW.md)

Desk dashboard for a one-room studio, on an ESP32-2432S028 "Cheap Yellow
Display" (320x240 resistive touch). Sibling of
[PCDeskCYD](https://github.com/Hydr0neFN/PCDeskCYD): same board bring-up
(touch on its own VSPI bus), but this USB-C board is driven as an **ST7789**
(inversion off, BGR order): `ILI9341_2_DRIVER` fills the panel but writes the
wrong gamma tables, which turns every dark colour blue,
different UI and transport.

## Data path

```
[Home Assistant on nl-pi]
  mqtt_statestream ──> cyd/nl/<domain>/<object_id>/state   (retained)
  cyd-hist.timer   ──> cyd/nl/hist/<key>                   (retained, 5 min)
  automation cyd_nl_weather_publish ──> cyd/nl/weather     (retained, 10 min)
  automation cyd_nl_command        <── cyd/nl/cmd/lightbar
  automation cyd_nl_basil_command  <── cyd/nl/cmd/basil
                     ▲ MQTT (mosquitto 10.0.0.20:1883, user "cyd")
[CYD] core 0: WiFi + PubSubClient + ArduinoOTA + SNTP  (src/net.cpp)
      core 1: LVGL 9.5 UI                              (src/ui*.cpp)
```

No Home Assistant token lives on the device; only the entities listed under
`mqtt_statestream` in HA's `configuration.yaml` leave HA. Chart history comes
from `/opt/cyd-hist/cyd_hist.py` on nl-pi, which reads the recorder's
statistics tables read-only.

## Pages

| Page   | Opened by            | Content |
|--------|----------------------|---------|
| Home   | —                    | animated weather sky, time/date, outdoor weather or alert pill, 4 cards |
| Room   | room card            | 24 h temp / humidity / CO2 chart (tabs), pressure |
| Light  | light-bar card       | on/off, 15-step brightness and colour temp, presets |
| Power  | power card           | 24 h house vs PC W, 7-day kWh bars |
| Basil  | basil card           | 48 h soil / tank chart, plant light, two-tap water / clear fault |
| Colour check | long-press the clock | palette swatches + display inversion toggle (persisted) |

## Sky background

The home screen sits on an animated sky built from the same condition groups
and gradient table as the user's projector wake page: day / dusk / night
gradients, drifting clouds, rain, snow, fog bands and a faint lightning flash.
The sun and moon follow their real path, computed on the device with a
low-precision ephemeris for the home location (`HOME_LAT` / `HOME_LON` in
`include/config.h`, rounded to city level): azimuth maps east to west onto left to
right, altitude onto height with the horizon at the bottom edge, so a rising
body appears as a large half circle at the bottom-left and sets at the
bottom-right. They are drawn only for a clear or partly cloudy sky. The
animation runs at 30 fps in use, 4 fps in the dim glance mode and stops at
night.

Measured on the panel (`cyd/nl/panel/perf`, 2026-09-27): about 8 ms per frame
at 30 fps, of which under 1 ms is SPI, so the bus is not the limit and DMA or
an 80 MHz SPI clock would buy almost nothing. Spreading LVGL's software
renderer over both cores (`LV_OS_FREERTOS`, 2 draw units) made it slower on
these small dirty areas and cost 19 KB of heap, so it is off. What mattered
was pacing: the sky advances from an `lv_timer` just before each refresh,
motion is per second rather than per frame, and `LV_DEF_REFR_PERIOD` is 16 ms.
For A/B tests, publish `secs:fps` to `cyd/nl/panel/bench` to run the sky at
that rate (even at night, backlight untouched); `secs:fps:full` also redraws
the whole screen every frame. A full redraw of the home screen costs ~130 ms
at 80 MHz SPI (~19 ms of it SPI; 34 ms at 40 MHz).

The sky layers: rain streaks are as long as the distance they fall in ~60 ms
and faster (nearer) drops are brighter; three cloud layers drift at different
speeds and sizes for parallax; the moon shows its real phase, from the
sun-moon elongation, as a sky-coloured disc clipped to the moon.

Backlight: bright while touched, dim glance when idle by day (brighter while
an alert is active), fully dark 00:30-07:30. A touch on a dim screen only
wakes it.

## Build

```
cp include/secrets.h.example include/secrets.h   # fill in
cp secrets.ini.example secrets.ini               # same OTA password
pio run -e cyd -t upload --upload-port COMx      # first flash (hold BOOT if auto-reset fails)
pio run -e cyd_ota -t upload                     # every flash after that
```

Arduino-ESP32 core 2.0.x (platform espressif32 7.x): LEDC uses the channel API.

## Fonts

CJK labels use Noto Sans TC subsets (regular and bold) generated from the
characters actually in `src/*.cpp`. After changing any CJK text:
`python tools/gen_fonts.py` (needs node and fontTools).
