# CYD Morse Sender v0.4

An ESP32-based Morse code sender for the **ESP32-2432S028R** ("Cheap Yellow Display").

- 7 programmable message buttons
- Configurable WPM speed slider
- Onboard keyboard for composing and programming messages
- Three-screen LVGL 9.x UI (Send / Config / Keyboard)

## Hardware

- **Board**: ESP32-2432S028R (CYD) — ILI9341 display, XPT2046 resistive touch
- **LED**: Built-in RGB LED (GPIO 4/16/17) — used as TX indicator

## Arduino Sketch

Open `ui/ui.ino` in the Arduino IDE. The sketch folder is `ui/`.

## Required Libraries

Install all of these via the Arduino IDE Library Manager before building:

| Library | Version | Notes |
|---------|---------|-------|
| LVGL_CYD | 1.2.2 | Installs TFT_eSPI, lvgl, spi_lcd_read automatically |
| lvgl | 9.5.0 | Installed by LVGL_CYD |
| TFT_eSPI | 2.5.43 | Installed by LVGL_CYD |
| XPT2046_Touchscreen | 1.4 | Install separately |
| spi_lcd_read | 1.0.0 | Installed by LVGL_CYD |

## Configuration Files

Two config files must be placed correctly **after** installing the libraries.
Both are provided in the `config/` folder of this repo.

### 1. `lv_conf.h`

Copy `config/lv_conf.h` to your Arduino libraries folder, **next to** (not inside) the `lvgl/` folder:

```
Documents/Arduino/libraries/lv_conf.h        ← here
Documents/Arduino/libraries/lvgl/            ← lvgl library folder
```

This file enables the correct fonts, widgets, color depth, and memory settings for the CYD.

### 2. `TFT_eSPI/User_Setup.h`

Copy `config/TFT_eSPI_User_Setup.h` to your TFT_eSPI library folder, replacing the existing `User_Setup.h`:

```
Documents/Arduino/libraries/TFT_eSPI/User_Setup.h
```

This file configures the ILI9341 driver with the correct SPI pins for the CYD (HSPI: MISO=12, MOSI=13, SCLK=14, CS=15, DC=2).

## Board Settings (Arduino IDE)

- **Board**: ESP32-2432S028R  (search "CYD" in board manager)
- **Flash size**: 4MB
- **Partition scheme**: Default

## Known Issues / Hard-Won Notes

- **Do not use `lv_obj_add_state(LV_STATE_DISABLED)`** on multiple buttons in a loop. In LVGL 9.1, this triggers the theme transition system and causes heap fragmentation leading to a crash after ~2 TX cycles. Use `lv_obj_clear_flag(LV_OBJ_FLAG_CLICKABLE)` / `lv_obj_add_flag(LV_OBJ_FLAG_CLICKABLE)` instead.
- **Do not register button event callbacks with `LV_EVENT_ALL`** — they intercept LVGL-internal draw events. Use `LV_EVENT_SHORT_CLICKED`.
- **Do not start a TX state machine from inside an LVGL event callback** (i.e., from within `lv_timer_handler()`). Queue the action via a flag and start it from `loop()`.
- Display is on HSPI; touch is on VSPI. Both use separate SPI buses.
