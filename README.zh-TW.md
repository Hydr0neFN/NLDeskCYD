# NLDeskCYD

[English](README.md)

專為套房（one-room studio）設計的桌面儀表板，運作於 ESP32-2432S028「Cheap Yellow Display」（320x240 電阻式觸控螢幕）。為 [PCDeskCYD](https://github.com/Hydr0neFN/PCDeskCYD) 的姊妹專案：採用相同的開發板初始化設定（觸控使用獨立的 VSPI 匯流排），但這塊 USB-C 開發板以 **ST7789** 驅動（關閉反相、BGR 順序）：`ILI9341_2_DRIVER` 雖能填滿整個面板，但寫入的 Gamma 表錯誤，會使所有深色偏藍；並採用不同的 UI 與傳輸機制。

## 資料路徑

```
[Home Assistant on nl-pi]
  mqtt_statestream ──> cyd/nl/<domain>/<object_id>/state   (retained)
  cyd-hist.timer   ──> cyd/nl/hist/<key>                   (retained, 5 min)
  automation cyd_nl_weather_publish ──> cyd/nl/weather     (retained, 10 min)
                                    ──> cyd/nl/forecast    (retained, 10 min)
  automation cyd_nl_command        <── cyd/nl/cmd/lightbar
  automation cyd_nl_basil_command  <── cyd/nl/cmd/basil
                     ▲ MQTT (mosquitto 10.0.0.20:1883, user "cyd")
[CYD] core 0: WiFi + PubSubClient + ArduinoOTA + SNTP  (src/net.cpp)
      core 1: LVGL 9.5 UI                              (src/ui*.cpp)
```

裝置上未存放任何 Home Assistant 權杖（token）；只有在 HA 的 `configuration.yaml` 中 `mqtt_statestream` 底下列出的實體才會傳出 HA。圖表歷史資料來自 nl-pi 上的 `/opt/cyd-hist/cyd_hist.py`，以唯讀方式讀取 recorder 的統計資料表。

## 頁面

| 頁面   | 開啟方式             | 內容 |
|--------|----------------------|------|
| 首頁   | —                    | 動態天氣天空背景、時間／日期、室外天氣或警示膠囊、4 張卡片 |
| 室內   | 室內卡片             | 24 小時溫度／濕度／CO2 圖表（分頁切換）、氣壓 |
| 掛燈   | 掛燈卡片             | 開／關、15 階亮度與色溫調整、預設模式 |
| 用電   | 用電卡片             | 24 小時全屋 vs 電腦瓦數、7 天 kWh 長條圖 |
| 羅勒   | 羅勒卡片             | 48 小時土壤／水箱圖表、植物燈、按兩次確認的澆水／清除異常 |
| 螢幕校色 | 長按時鐘           | 調色盤色票＋螢幕反相切換（設定會儲存） |
| 逐時預報 | 長按室外天氣       | 未來 12 小時：時間、天氣、溫度、雨量（HA `weather.get_forecasts` → retained `cyd/nl/forecast`） |

## 天空背景

首頁背景是動態天空，採用與使用者投影機喚醒頁面相同的天氣條件分組與漸層表：白天／黃昏／夜晚漸層、飄動的雲朵、降雨、降雪、霧帶以及隱約的閃電。太陽與月亮沿真實軌跡移動，由裝置端依住家位置（`include/config.h` 中的 `HOME_LAT` / `HOME_LON`，四捨五入至城市層級）以低精度星曆（ephemeris）計算：方位角由東到西對應螢幕由左到右，仰角對應高度，地平線在螢幕底緣，因此升起的天體會在左下方呈現大半圓，並於右下方落下。太陽與月亮只在晴朗或晴時多雲的天氣繪製。動畫在使用時以 30 fps 運作，微亮閒置模式為 4 fps，夜間完全停止。

面板實測（`cyd/nl/panel/perf`，2026-09-27）：30 fps 時每幀約 8 ms，其中 SPI 傳輸不到 1 ms，瓶頸不在匯流排，所以 DMA 或把 SPI 拉到 80 MHz 幾乎沒有幫助。讓 LVGL 軟體渲染跨兩顆核心（`LV_OS_FREERTOS`、2 個 draw unit）在這種小範圍重繪上反而更慢，還多用 19 KB heap，因此關閉。真正有效的是節拍：天空由 `lv_timer` 在每次刷新前推進一格，移動速度以「每秒」而非「每幀」計算，`LV_DEF_REFR_PERIOD` 設為 16 ms。做 A/B 測試時，對 `cyd/nl/panel/bench` 發布 `秒數:fps`，天空就會以該幀率執行（夜間也可以，背光不變）；`秒數:fps:full` 則會每幀重繪整個畫面。首頁整屏重繪在 80 MHz SPI 下約 130 ms（其中 SPI 約 19 ms；40 MHz 時為 34 ms）。

天空圖層：雨絲長度等於約 60 ms 內落下的距離（動態模糊），越快的雨滴代表越近，也越亮；三層雲以不同的大小、透明度與速度飄移，形成視差；月亮依太陽與月亮的黃經差呈現真實月相，做法是一個天空色的圓，並裁切在月面範圍內。

背光：觸控時全亮；白天閒置時微亮（有警示時亮度較高）；00:30–07:30 完全關閉。觸碰微亮的螢幕只會將其喚醒。

## 建置

```
cp include/secrets.h.example include/secrets.h   # fill in
cp secrets.ini.example secrets.ini               # same OTA password
pio run -e cyd -t upload --upload-port COMx      # first flash (hold BOOT if auto-reset fails)
pio run -e cyd_ota -t upload                     # every flash after that
```

Arduino-ESP32 core 2.0.x（platform espressif32 7.x）：LEDC 使用 channel API。

## 字型

CJK 標籤使用 Noto Sans TC 子集（一般與粗體），由 `src/*.cpp` 中實際出現的字元產生。修改任何 CJK 文字後請執行：`python tools/gen_fonts.py`（需要 node 與 fontTools）。
