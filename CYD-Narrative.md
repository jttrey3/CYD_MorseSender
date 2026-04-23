# CYD Morse Sender — Project Narrative

*A full account of the design, implementation, problems, and solutions from start to finish.*

---

## Background and Goal

The project began on March 29, 2026. The goal was to build a handheld Morse code transmitter
using an **ESP32-2432S028R** — a low-cost ESP32 development board sold under the informal name
"Cheap Yellow Display" (CYD). The board has a 320×240 ILI9341 color touchscreen and an
XPT2046 resistive touch controller.

The intended functionality:
- A graphical interface with **seven programmable message buttons** on a Send screen
- A **Config screen** with a WPM speed slider and a slot selector for editing messages
- A **Keyboard screen** for typing message text into a slot
- When a button is pressed, the stored message is converted to Morse code and transmitted by
  blinking the board's onboard blue LED

The GUI had already been designed in **SquareLine Studio 1.5.1** targeting LVGL 9.1, and the
resulting generated source files were in a GitHub repository. The request was to plan and then
implement all the logic needed to bring that static UI to life.

---

## The Build Plan

Before writing any code, the existing codebase was fetched from GitHub and analyzed. The
SquareLine-generated files defined three screens, all the widget objects, and stub event
handlers, but the sketch had no working touch input, no message storage, and no Morse logic
whatsoever. The touchscreen read function had a single hardcoded line — `touched = false` —
meaning the display would render but nothing could be tapped.

A detailed eight-phase plan was written and saved to `output/BUILD_PLAN.md`:

| Phase | Goal |
|-------|------|
| 1 | Fix touchscreen input (initialize XPT2046, calibrate coordinates) |
| 2 | Persistent message storage via ESP32 NVS (Preferences library) |
| 3 | Config screen: wire WPM slider and slot dropdown |
| 4 | Keyboard screen: capture typed text and save to a slot |
| 5 | Morse code engine: lookup table + Paris-standard timing |
| 6 | Non-blocking LED transmission via a `millis()`-driven state machine |
| 7 | Wire send buttons to trigger transmission |
| 8 | Integration and polish: Stop button, empty-slot guard, end-to-end test |

The recommended implementation order was not strictly sequential: Phase 1 first (touch must
work before anything else), then 2, then a partial Phase 7 with serial logging to prove the
Morse engine worked before the full UI was wired up, then 5 → 6 → 7 (full) → 3 → 4 → 8.

---

## Phase 1 — Fixing Touchscreen Input

### The first hurdle: LVGL version mismatch

The very first attempt to compile the sketch immediately produced errors. The original code
used the LVGL 8 API (`lv_disp_draw_buf_t`, `lv_disp_drv_t`, `lv_disp_drv_init()`), but the
installed library was LVGL 9. In LVGL 9 the display subsystem was redesigned: buffers are
set on a display object with `lv_display_set_buffers()`, and the driver registration functions
were removed entirely. The entire display and input initialization block had to be rewritten
from scratch for the LVGL 9 API.

### The second hurdle: missing lv_conf.h

After the API rewrite, the next compile error was:

```
lv_conf.h: No such file or directory
```

LVGL requires a configuration header to be present at a specific location: *next to* the
`lvgl/` library folder inside the Arduino libraries directory, not inside it. A custom
`lv_conf.h` was created enabling the Montserrat fonts (sizes 10, 12, 14, 16) used by the
SquareLine-generated screens, the LVGL default dark theme, and all the widget types referenced
by the generated `ui_helpers.c` boilerplate.

An additional assembler conflict surfaced: the config file included `<stdint.h>`, which caused
errors when the ESP32 toolchain processed it as assembly. This was fixed by wrapping the
include in `#ifndef __ASSEMBLER__` guards.

### The third hurdle: black screen after upload

With the code compiling and uploading cleanly, the display stayed completely black. The serial
monitor confirmed the sketch was running — it printed the calibration prompt — so the CPU was
alive. The problem was twofold:

1. **Backlight not enabled.** The CYD's backlight is on GPIO 21. The TFT_eSPI library does not
   drive the backlight automatically unless configured to do so. An explicit
   `digitalWrite(21, HIGH)` was added to `setup()`.

2. **TFT_eSPI not configured for HSPI.** The CYD display SPI bus uses the ESP32's HSPI
   peripheral (GPIOs 12/13/14/15), not the default VSPI. The `User_Setup.h` inside the
   TFT_eSPI library folder needed `#define USE_HSPI_PORT` uncommented and the correct pin
   numbers set.

After those two fixes the display lit up — but showed a pattern of vertical color bars.

### The fourth hurdle: wrong rotation

The vertical bars turned out to be the seven green send buttons rendering in portrait
orientation, rotated 90 degrees. The TFT_eSPI rotation argument was `0` (portrait). Changing
it to `3` (landscape, USB port on the right side of the board) produced the correct layout.
The XPT2046 touch controller also has an independent rotation setting that must match the
display; once both were set to rotation 3, touch coordinates mapped correctly to screen pixels.

### Calibration

With touch physically working, raw ADC values from the XPT2046 were captured in a diagnostic
mode by tapping all four corners of the screen. Those four corner measurements — saved in
`output/CALIBRATION.md` — were used to derive the linear mapping constants that convert raw
0–4095 ADC readings to 0–319 (X) and 0–239 (Y) screen coordinates. After calibration all
three navigation sidebar buttons reliably transitioned between screens.

**Phase 1 was complete.** All three screens navigated correctly via touch.

---

## Phase 2 — Persistent Message Storage

This phase was straightforward. The Arduino ESP32 core includes the `Preferences` library,
which provides a simple key-value store backed by the ESP32's non-volatile flash (NVS). Seven
string keys (`"msg0"` through `"msg6"`) hold the button messages. A RAM buffer
`char g_messages[7][64]` is populated at boot by `loadAllMessages()`, which falls back to
default text ("Button 1" through "Button 7") for any key not yet written. `saveMessage(slot,
text)` writes a single slot to NVS, and `getMessage(slot)` returns the pointer from the RAM
buffer.

After `ui_init()` in `setup()`, each of the seven send button labels is set from the loaded
messages. Serial logging confirmed all seven slots reading correctly, and a power-cycle test
confirmed persistence.

**Phase 2 complete.**

---

## Phases 5, 6, and 7 — Morse Engine and LED Transmission

These three phases were built together as a unit since they are tightly coupled.

### morse.h — the encoding engine

A standalone header file `morse.h` was created containing a `const char*` lookup table for
A–Z and 0–9 using standard ITU Morse encoding. A `textToMorse()` function walks an input
string and appends `DOT`, `DASH`, `LETTER_GAP`, and `WORD_GAP` symbols to an output buffer.
Timing follows the Paris standard: one "unit" = `1200 / wpm` milliseconds, with a dot being
1 unit, a dash 3 units, a within-letter gap 1 unit, a between-letter gap 3 units, and a
between-word gap 7 units.

### Non-blocking transmission

The most important design constraint was that `delay()` must never be used for Morse timing.
The LVGL timer handler must run every few milliseconds to process touch events and update the
display; any blocking delay would freeze the UI during transmission.

The solution was a state machine driven entirely by `millis()`. Three global variables track
state: the current symbol array (`g_morseSeq`), the current index (`g_morseIndex`), and the
deadline for the next transition (`g_morseStartMs`). A `morseUpdate()` function is called on
every `loop()` iteration. If the current time has passed the deadline it advances the state
— turning the LED on or off — and sets the next deadline. No busy-waiting, no delays.

The LED used is the CYD's onboard blue LED on GPIO 17 (active low on this board, meaning
`LOW` turns it on).

### Wiring the send buttons

The SquareLine-generated `ui_Send_Screen.c` file was modified to register
`ui_event_SendButton` on all seven buttons using `LV_EVENT_SHORT_CLICKED`. A single shared
handler identifies which button was tapped by comparing `lv_event_get_target()` against each
button object, then calls `onSendButton(slot)` implemented in `ui.ino`.

`onSendButton()` checks that a transmission isn't already in progress (`!g_morseTx`) and if
clear, calls `startMorseText(getMessage(slot))`.

**Phases 5, 6, and 7 complete.** Messages transmitted correctly via LED.

---

## Phase 3 — Config Screen Wiring

The Config screen slider and dropdown were wired to their respective handlers. The slider
event calls `onSpeedSlider(wpm)`, which stores the value in `g_wpm` and also updates
`ui_SpeedValueLabel` to show the current number. WPM is also persisted to NVS on change so
the last-used speed survives a reboot.

A bug specific to LVGL 9.1 was discovered here: when a slider's value is at its maximum and
the slider widget's `pad_top` style is non-zero while `pad_right` is zero, LVGL crashes in
the draw path. A one-line workaround was applied in the SquareLine-generated
`ui_Config_Screen.c` that increments `pad_right` by 1 when `pad_top` is non-zero.

**Phase 3 complete.**

---

## Phase 4 — Keyboard Screen and Dual-Mode Operation

This was the most complex phase. The Keyboard screen needed to work in two distinct modes:

1. **Edit mode** — arriving via the Config screen's Set button. The text area should be
   pre-filled with the current message for that slot. On confirm (checkmark), save the text
   to NVS and update the button label. Return to Config screen.

2. **Send mode** — arriving via the Keyboard nav tab on either the Send or Config screen.
   The text area should be blank. On confirm, transmit the typed text immediately as Morse.
   Return to Send screen.

### Implementation

A boolean `g_kbdEditMode` flag distinguishes the two modes. Two new C functions were added to
`ui.ino` and declared `extern` in `ui.c`:

- `onKeyboardNavButton()` — called when the Keyboard tab is tapped; sets `g_kbdEditMode = false`
- `isKbdEditMode()` — called from `ui.c` event handlers to decide navigation destination

`onSetButton(slot)` (called when Config's Set button is tapped) sets `g_kbdEditMode = true`
and records the target slot.

`onKeyboardScreenLoad()` reads the mode flag: in edit mode it pre-fills the text area with
the current slot's stored message; in send mode it clears the text area.

`onKeyboardConfirm(text)` also branches on the flag: in edit mode it saves to NVS and updates
the button label; in send mode it stores the text in a `g_pendingTxText` buffer for deferred
transmission (more on this below).

The `ui_event_Keyboard1` handler in `ui.c` was updated to call `isKbdEditMode()` and navigate
back to Config or Send screen accordingly, on both READY and CANCEL events.

---

## The Freeze Bugs — A Debugging Chronicle

After integrating the keyboard send mode, a series of hard-to-reproduce freezes appeared.
Each was diagnosed and fixed in turn.

### Freeze 1: Transmitting from inside an LVGL event callback

After typing a message on the Keyboard screen and tapping the checkmark, the LED went solid
and the device froze. The Stop button never appeared. The freeze was consistent when sending
via the keyboard path but not when sending via the numbered buttons.

**Root cause:** `onKeyboardConfirm()` was originally calling `startMorseText()` directly.
`onKeyboardConfirm()` is invoked from `ui_event_Keyboard1`, which is called from within
`lv_timer_handler()`'s event dispatch. Starting the Morse state machine — which among other
things calls LVGL functions to update button states — from inside the dispatch loop caused
LVGL's internal state to become inconsistent, leading to a deadlock.

**Fix:** `onKeyboardConfirm()` no longer calls `startMorseText()` directly. Instead it stores
the text in a `g_pendingTxText` buffer. In `loop()`, after `lv_timer_handler()` returns,
a check for `g_pendingTxText[0] != '\0'` triggers the actual transmission start. This
deferred-start pattern keeps all Morse initiation outside LVGL's dispatch loop.

### Freeze 2: LV_EVENT_ALL on buttons intercepting draw events

After fixing the deferred start, buttons 2 and 3 worked but button 4 still froze after being
pressed following a Stop. Investigation showed that the send buttons and Stop button were
registered with `LV_EVENT_ALL` in the SquareLine-generated `ui_Send_Screen.c`. In LVGL 9,
`LV_EVENT_ALL` causes the callback to be invoked for every internal event, including draw
events fired during `lv_timer_handler()`. This caused the event handler to fire at unexpected
times and corrupt state.

**Fix:** Changed all send button and Stop button registrations from `LV_EVENT_ALL` to
`LV_EVENT_SHORT_CLICKED` in `ui_Send_Screen.c`. This immediately resolved the spurious
callback firings.

### Freeze 3: The two-call lv_timer_handler pattern

Removing the Stop button from the display timer forcing (which was an earlier attempted fix)
caused the Stop button to stop appearing during transmission. The `loop()` had been calling
`lv_timer_handler()` once per iteration, but LVGL's input device timer and display refresh
timer were running at different phases, sometimes causing one to be skipped.

**Fix:** Split into two `lv_timer_handler()` calls per loop iteration:

```cpp
lv_timer_ready(lv_indev_get_read_timer(g_indev));
lv_timer_handler();   // call-1: processes touch/input events

lv_timer_ready(lv_display_get_refr_timer(g_disp));
lv_timer_handler();   // call-2: forces display refresh
```

This guaranteed that both the input device and the display refresh ran every loop cycle,
independent of their internal timer phases. The Stop button then appeared correctly during
transmission.

### Freeze 4: LVGL 9.1 heap fragmentation crash (the root cause)

After all the above fixes, a very reproducible pattern emerged: buttons 2 and 3 would send
successfully, but button 4 would always freeze. Restarting the device and repeating the same
sequence — 2, 3, 4 — produced the same freeze every time, on three consecutive trials.

The freeze symptom was: LED goes solid, Stop button never appears, device unresponsive.

Serial debug checkpoints with `Serial.flush()` were added to three specific points:
1. Just before `startMorseText()` is called
2. Just after call-1 of `lv_timer_handler()` returns
3. Just after call-2 of `lv_timer_handler()` returns

All three messages printed for button 4's transmission. This proved definitively that the
freeze was **not** inside either `lv_timer_handler()` call. It was happening in the *next*
loop iteration, in the `g_txUiPending == 1` block — specifically inside
`setSendButtonsEnabled(false)`.

**Root cause identified:** `setSendButtonsEnabled()` was calling
`lv_obj_add_state(btn, LV_STATE_DISABLED)` on nine buttons in a loop. In LVGL 9.1, adding
`LV_STATE_DISABLED` triggers the default theme's transition system, which **heap-allocates an
`lv_anim_t` object** for each button. With nine buttons and two state changes per TX cycle
(disable at start, re-enable at end), that is 18 allocations and 18 frees per cycle. After
two complete TX cycles (36 alloc/free cycles), the ESP32 heap was fragmented enough that the
37th allocation — for button 4's TX start — crashed inside `lv_anim_t` allocation.

**Fix:** Replace all `lv_obj_add/remove_state(LV_STATE_DISABLED)` calls with
`lv_obj_clear/add_flag(LV_OBJ_FLAG_CLICKABLE)`:

```cpp
static void setSendButtonsEnabled(bool enabled)
{
    lv_obj_t * const btns[] = {
        ui_SendButton1, ui_SendButton2, ui_SendButton3,
        ui_SendButton4, ui_SendButton5, ui_SendButton6,
        ui_SendButton7, ui_ConfigButton, ui_KeyboardButton
    };
    const int btnCount = sizeof(btns) / sizeof(btns[0]);
    for (int i = 0; i < btnCount; i++) {
        if (enabled)
            lv_obj_add_flag(btns[i], LV_OBJ_FLAG_CLICKABLE);
        else
            lv_obj_clear_flag(btns[i], LV_OBJ_FLAG_CLICKABLE);
    }
}
```

Flag changes do **not** trigger LVGL's theme transition system. No animation is allocated.
No heap churn. The tradeoff is that buttons no longer visually gray out during transmission
(they stay green but ignore touches), but the red Stop button provides sufficient visual
feedback. Visual dimming could be re-added later using `lv_obj_set_style_opa()`, which also
does not trigger transitions.

After this fix, the user ran buttons 2, 3, and 4 repeatedly, navigated across all three
screens, and reported: **"success! I have tried button 2, 3, 4 with no hang. I tried several
other things on various screens and then back to send screen with no hangs."**

---

## Final loop() Structure

The final `loop()` reflects all the lessons learned:

```cpp
void loop()
{
    morseUpdate();

    // Process UI state changes that were queued from inside LVGL event dispatch
    if (g_txUiPending == 1) {
        g_txUiPending = 0;
        setSendButtonsEnabled(false);
        setStopButtonActive(true);
    } else if (g_txUiPending == -1) {
        g_txUiPending = 0;
        setSendButtonsEnabled(true);
        setStopButtonActive(false);
    }

    // Deferred TX start — never start from inside lv_timer_handler()
    if (g_pendingTxText[0] != '\0' && !g_morseTx) {
        startMorseText(g_pendingTxText);
        g_pendingTxText[0] = '\0';
    }

    // Two-call pattern: force input read, then force display refresh
    lv_timer_ready(lv_indev_get_read_timer(g_indev));
    lv_timer_handler();
    lv_timer_ready(lv_display_get_refr_timer(g_disp));
    lv_timer_handler();

    delay(5);
}
```

---

## Project Status at Completion

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Touchscreen initialization and calibration | Complete |
| 2 | Persistent message storage (NVS) | Complete |
| 3 | Config screen: WPM slider, slot dropdown | Complete |
| 4 | Keyboard screen: edit mode and send mode | Complete |
| 5 | Morse code engine | Complete |
| 6 | Non-blocking LED transmission state machine | Complete |
| 7 | Send button wiring and Stop button | Complete |
| 8 | Integration, freeze fixes, end-to-end testing | Complete |

---

## Key Rules Learned (Apply to Future LVGL 9 Projects)

1. **Never use `lv_obj_add/remove_state(LV_STATE_DISABLED)` on multiple objects in a loop.**
   It allocates heap memory for theme transition animations. Use
   `lv_obj_clear/add_flag(LV_OBJ_FLAG_CLICKABLE)` instead.

2. **Never start a state machine from inside an LVGL event callback.** Queue the action via a
   flag or buffer and act on it from `loop()` after `lv_timer_handler()` returns.

3. **Register button callbacks with `LV_EVENT_SHORT_CLICKED`, not `LV_EVENT_ALL`.** Using
   `LV_EVENT_ALL` causes callbacks to fire on LVGL-internal draw events.

4. **Use the two-call `lv_timer_handler()` pattern** to guarantee both the input device timer
   and the display refresh timer run every loop cycle.

5. **`lv_conf.h` must sit next to the `lvgl/` folder**, not inside it. If the `#if 1` guard
   at the top is `0`, the entire file is silently ignored.

6. **The CYD display is on HSPI** (GPIOs 12/13/14/15). `USE_HSPI_PORT` must be defined in
   `TFT_eSPI/User_Setup.h`. Backlight GPIO 21 must be driven explicitly.

---

## Future Work (Phase 9+)

Three features are planned for when the hardware arrives:

- **Speaker output** — audio sidetone while keying Morse
- **Optoisolated relay output** — for closing the key line on a real radio
- **Output selector on the Config screen** — a UI control to choose between LED, speaker,
  relay, or combinations thereof

---

*Project started March 29, 2026. Core functionality complete April 7, 2026.*
*Codebase published to https://github.com/jttrey3/CYD_MorseSender*
