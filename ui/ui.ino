/*
 * CYD Morse Sender — ui.ino
 * Phase 1: Fix Touchscreen Input
 *
 * Hardware: ESP32-2432S028R ("Cheap Yellow Display")
 *   Display : ILI9341  320×240  (HSPI — GPIO 14/13/12/15/2)
 *   Touch   : XPT2046           (VSPI — GPIO 25/32/39/33)
 *   RGB LED : R=GPIO4  G=GPIO16  B=GPIO17
 *
 * Libraries required (install via Arduino Library Manager):
 *   - lvgl              9.x      <-- this build targets LVGL 9 API
 *   - TFT_eSPI          (configure User_Setup.h — see note below)
 *   - XPT2046_Touchscreen
 *
 * NOTE — SquareLine Studio compatibility:
 *   SquareLine Studio 1.3.x exports LVGL 8.x code.
 *   If your ui/ folder was exported from SquareLine, you have two options:
 *     A) Downgrade LVGL to 8.3.x in Library Manager and use the original ui.ino
 *        from the v0.3 release (LVGL 8 API).
 *     B) Re-export from SquareLine targeting LVGL 9, then use this file.
 *   Option A is usually faster if your SquareLine project still opens.
 *
 * TFT_eSPI User_Setup.h must define:
 *   #define ILI9341_DRIVER
 *   #define TFT_MOSI 13
 *   #define TFT_SCLK 14
 *   #define TFT_CS   15
 *   #define TFT_DC    2
 *   #define TFT_RST  -1
 *   #define TFT_MISO 12
 *   #define TFT_RGB_ORDER TFT_RGB
 *   #define USE_HSPI_PORT
 *   #define SPI_FREQUENCY  40000000
 *   #define SPI_READ_FREQUENCY 20000000
 *   #define SPI_TOUCH_FREQUENCY  2500000
 *
 * -----------------------------------------------------------------------
 * DISPLAY ROTATION — confirmed via rotation-cycle test + live UI observation:
 *   setRotation(0) fills the full screen but renders upside-down and X-mirrored.
 *   setRotation(2) is 180° from that and gives correct landscape orientation
 *   with proper axis direction (confirmed: UI right-side-up, text readable).
 *   Rotations 1 and 3 produce portrait-mode (horizontal stripes test).
 *   Note: tft.width()/height() report 240×320 due to the ILI9341 being
 *   physically mounted 90° rotated on the CYD board; LVGL is configured
 *   320×240 which maps correctly to the physical pixels.
 * -----------------------------------------------------------------------
 * CALIBRATION_MODE
 *   Set to 1 the first time you flash this firmware.
 *   Open Serial Monitor at 115200 baud and tap the four corners of the
 *   screen. Record the printed raw X/Y values, fill in the four
 *   #define constants below, then set CALIBRATION_MODE back to 0.
 *   If touches register mirrored after calibration, see CALIBRATION.md
 *   troubleshooting table (swap X_MIN/X_MAX or Y_MIN/Y_MAX).
 * -----------------------------------------------------------------------
 */

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include <Preferences.h>
#include "ui.h"             // SquareLine-generated screen definitions
#include "morse.h"          // Morse engine: lookup table + buildMorseSequence()

// ---------------------------------------------------------------------------
// Calibration mode toggle
// ---------------------------------------------------------------------------
#define CALIBRATION_MODE  0   // <-- set to 1 to re-calibrate touch

// ---------------------------------------------------------------------------
// Touch calibration constants  (calibrated 2026-04-05)
// ---------------------------------------------------------------------------
#define TOUCH_X_MIN   290
#define TOUCH_X_MAX  3822
#define TOUCH_Y_MIN   270
#define TOUCH_Y_MAX  3847

// Screen dimensions
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

// ---------------------------------------------------------------------------
// XPT2046 pin definitions (VSPI bus on the CYD)
// ---------------------------------------------------------------------------
#define XPT2046_CLK   25
#define XPT2046_MISO  39
#define XPT2046_MOSI  32
#define XPT2046_CS    33
#define XPT2046_IRQ   36

// ---------------------------------------------------------------------------
// CYD backlight — GPIO 21, active HIGH.  Must be driven manually;
// TFT_eSPI will not enable it unless TFT_BL is defined in User_Setup.h.
// ---------------------------------------------------------------------------
#define CYD_BACKLIGHT_PIN 21

// ---------------------------------------------------------------------------
// Phase 2: Persistent message storage
// Phase 3: Persistent WPM setting
// ---------------------------------------------------------------------------
#define MSG_COUNT     7
#define MSG_MAX_LEN  64
#define WPM_DEFAULT  20

static char      g_messages[MSG_COUNT][MSG_MAX_LEN];
static int       g_wpm     = WPM_DEFAULT;
static int       g_editSlot   = 0;          // Phase 4: slot being edited on Keyboard screen
static bool      g_kbdEditMode = true;      // true = edit slot (from Config), false = one-shot send (from nav)
static char      g_pendingTxText[MSG_MAX_LEN] = ""; // one-shot TX queued from keyboard confirm; started in loop()
static Preferences prefs;

// Default labels shown on first boot (before the user sets anything).
static const char * const MSG_DEFAULTS[MSG_COUNT] = {
    "Button 1", "Button 2", "Button 3", "Button 4",
    "Button 5", "Button 6", "Button 7"
};

// Load all messages and WPM from NVS into RAM.
// Called once from setup() after ui_init().
void loadAllMessages()
{
    prefs.begin("morse", /*readOnly=*/true);
    for (int i = 0; i < MSG_COUNT; i++) {
        char key[6];
        snprintf(key, sizeof(key), "msg%d", i);
        String val = prefs.getString(key, MSG_DEFAULTS[i]);
        val.toCharArray(g_messages[i], MSG_MAX_LEN);
    }
    g_wpm = prefs.getInt("wpm", WPM_DEFAULT);
    prefs.end();
}

// Return the current WPM (used by Phase 5 Morse engine).
int getWpm()
{
    return g_wpm;
}

// Persist one message slot to NVS and update the RAM buffer.
// Called from Phase 4 (keyboard confirm).
void saveMessage(int slot, const char *text)
{
    if (slot < 0 || slot >= MSG_COUNT) return;
    strncpy(g_messages[slot], text, MSG_MAX_LEN - 1);
    g_messages[slot][MSG_MAX_LEN - 1] = '\0';

    prefs.begin("morse", /*readOnly=*/false);
    char key[6];
    snprintf(key, sizeof(key), "msg%d", slot);
    prefs.putString(key, g_messages[slot]);
    prefs.end();
}

// Return the stored message for a slot (points into the RAM buffer).
const char *getMessage(int slot)
{
    if (slot < 0 || slot >= MSG_COUNT) return "";
    return g_messages[slot];
}

// Push all stored labels onto the Send Screen buttons.
// Called once after loadAllMessages(); can also be called after saveMessage()
// to refresh a single button (just call lv_label_set_text directly for that).
static lv_obj_t * const *s_sendLabels[] = {
    &ui_SendButton1Label, &ui_SendButton2Label, &ui_SendButton3Label,
    &ui_SendButton4Label, &ui_SendButton5Label, &ui_SendButton6Label,
    &ui_SendButton7Label
};

void syncSendButtonLabels()
{
    for (int i = 0; i < MSG_COUNT; i++) {
        lv_label_set_text(*s_sendLabels[i], g_messages[i]);
    }
}

// Forward declaration — defined after Morse state machine globals below.
static void startMorseText(const char *text);

// ---------------------------------------------------------------------------
// Phase 4: Keyboard screen callbacks (called from ui.c via extern "C")
// ---------------------------------------------------------------------------

// Called when Set button is tapped on Config screen — stores selected slot, enters edit mode.
extern "C" void onSetButton(int slot)
{
    g_editSlot    = slot;
    g_kbdEditMode = true;   // arriving via Config → edit mode
}

// Called when a Keyboard nav button is tapped directly (not via Config Set).
// Puts the keyboard screen into one-shot send mode.
extern "C" void onKeyboardNavButton()
{
    g_kbdEditMode = false;  // arriving via nav tab → one-shot send mode
}

// Returns 1 if the keyboard is in slot-edit mode, 0 if in one-shot send mode.
// Used by ui.c to decide which screen to return to after READY/CANCEL.
extern "C" int isKbdEditMode()
{
    return g_kbdEditMode ? 1 : 0;
}

// Called when Keyboard screen loads.
// Edit mode: pre-fills textarea with the slot's current message.
// Send mode: clears the textarea for fresh input.
extern "C" void onKeyboardScreenLoad()
{
    if (g_kbdEditMode) {
        lv_textarea_set_text(ui_TextArea1, getMessage(g_editSlot));
    } else {
        lv_textarea_set_text(ui_TextArea1, "");
    }
    lv_keyboard_set_textarea(ui_Keyboard1, ui_TextArea1);
}

// Called when user taps checkmark on keyboard.
// Edit mode: saves text to the slot and updates the Send screen button label.
// Send mode: immediately starts transmitting the typed text in Morse.
extern "C" void onKeyboardConfirm(const char *text)
{
    if (g_kbdEditMode) {
        saveMessage(g_editSlot, text);
        lv_label_set_text(*s_sendLabels[g_editSlot], g_messages[g_editSlot]);
        Serial.printf("Saved slot %d: \"%s\"\n", g_editSlot + 1, g_messages[g_editSlot]);
    } else {
        // One-shot: queue the text for TX — started in loop() outside lv_timer_handler().
        strncpy(g_pendingTxText, text, MSG_MAX_LEN - 1);
        g_pendingTxText[MSG_MAX_LEN - 1] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Hardware objects
// ---------------------------------------------------------------------------
SPIClass touchscreenSPI(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS);
TFT_eSPI tft = TFT_eSPI();

// Both kept global: g_disp is needed to associate the indev with the display
// so lv_indev_read() knows which object tree to hit-test against.
// Without lv_indev_set_display(), the read callback fires but LVGL skips
// event dispatch and no button/widget ever receives a press event.
static lv_display_t *g_disp  = nullptr;
static lv_indev_t   *g_indev = nullptr;

// ---------------------------------------------------------------------------
// LVGL 9 display buffer
// IMPORTANT: must be uint8_t, NOT lv_color_t.
// In LVGL 9, lv_color_t is 4 bytes (ARGB32). If we declare lv_color_t[] and
// then tell LVGL to render RGB565, the buffer is 2× too large and pixel data
// is read at the wrong stride, producing diagonal stripe corruption.
// Using uint8_t and lv_display_set_color_format(RGB565_SWAPPED) is correct.
// ---------------------------------------------------------------------------
static const uint32_t LVGL_BUF_BYTES = SCREEN_WIDTH * (SCREEN_HEIGHT / 10) * 2; // 2 bytes per RGB565 pixel
static uint8_t buf1[LVGL_BUF_BYTES];

// ---------------------------------------------------------------------------
// LVGL 9 display flush callback
// Signature changed from v8: drv→lv_display_t*, color_p→uint8_t* px_map
// ---------------------------------------------------------------------------
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, false);
    tft.endWrite();

    lv_display_flush_ready(disp);
}

// ---------------------------------------------------------------------------
// LVGL 9 touchpad read callback  — Phase 1 fix
// Signature changed from v8: first param is lv_indev_t* (not lv_indev_drv_t*)
// ---------------------------------------------------------------------------
void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    TS_Point raw = touchscreen.getPoint();

    if (raw.z < 400) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // XPT2046 axes are swapped AND inverted relative to ILI9341 with setRotation(2).
    // raw.y → screen X  (inverted: high raw.y = left side, so map Y_MAX→0, Y_MIN→WIDTH)
    // raw.x → screen Y  (forward:  low raw.x  = top)
    int16_t screen_x = map(raw.y, TOUCH_Y_MAX, TOUCH_Y_MIN, 0, SCREEN_WIDTH  - 1);
    int16_t screen_y = map(raw.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_HEIGHT - 1);

    screen_x = constrain(screen_x, 0, SCREEN_WIDTH  - 1);
    screen_y = constrain(screen_y, 0, SCREEN_HEIGHT - 1);

#if CALIBRATION_MODE
    Serial.printf("RAW (%4d,%4d) z=%4d  ->  SCREEN (%3d,%3d)\n",
                  raw.x, raw.y, raw.z, screen_x, screen_y);
#endif

    data->state   = LV_INDEV_STATE_PRESSED;
    data->point.x = screen_x;
    data->point.y = screen_y;
}

// ---------------------------------------------------------------------------
// Phase 3: WPM slider callback (called from ui.c via extern "C")
// ---------------------------------------------------------------------------
extern "C" void onSpeedSlider(int wpm)
{
    g_wpm = wpm;
    prefs.begin("morse", /*readOnly=*/false);
    prefs.putInt("wpm", g_wpm);
    prefs.end();
}

// ---------------------------------------------------------------------------
// Phase 6: Non-blocking Morse TX state machine
// ---------------------------------------------------------------------------
static uint16_t g_morseSeq[MORSE_BUF_SIZE];
static int      g_morseCount = 0;
static int      g_morseIndex = 0;
static uint32_t g_morseStartMs = 0;
static bool     g_morseTx = false;
// Pending UI update: 1 = TX just started (disable buttons), -1 = TX just ended (enable buttons), 0 = none.
// Set by startMorse/stopMorse/morseUpdate; applied in loop() outside lv_timer_handler().
static int8_t   g_txUiPending = 0;

bool isMorseTx() { return g_morseTx; }

static void setSendButtonsEnabled(bool enabled)
{
    // Lock/unlock all interactive buttons on the Send screen during TX.
    // Nav buttons (Config, Keyboard) are included to prevent screen switches mid-TX.
    //
    // Uses lv_obj_clear/add_flag(LV_OBJ_FLAG_CLICKABLE) instead of
    // lv_obj_add/remove_state(LV_STATE_DISABLED).  State changes trigger LVGL's
    // theme transition system (heap-allocated animations); after ~2 TX cycles the
    // repeated alloc/free fragments the heap and crashes on the 3rd cycle.
    // Flag changes do not trigger theme transitions — no animation, no heap churn.
    lv_obj_t * const btns[] = {
        ui_SendButton1, ui_SendButton2, ui_SendButton3,
        ui_SendButton4, ui_SendButton5, ui_SendButton6,
        ui_SendButton7,
        ui_ConfigButton, ui_KeyboardButton
    };
    const int btnCount = sizeof(btns) / sizeof(btns[0]);
    for (int i = 0; i < btnCount; i++) {
        if (enabled)
            lv_obj_add_flag(btns[i], LV_OBJ_FLAG_CLICKABLE);
        else
            lv_obj_clear_flag(btns[i], LV_OBJ_FLAG_CLICKABLE);
    }
}

static void setStopButtonActive(bool active)
{
    // Capture the theme's original button colour on first call so we can restore it.
    static lv_color_t s_origColor;
    static bool       s_colorSaved = false;

    if (active) {
        if (!s_colorSaved) {
            s_origColor  = lv_obj_get_style_bg_color(ui_SendScreenButton, LV_PART_MAIN);
            s_colorSaved = true;
        }
        lv_obj_set_style_bg_color(ui_SendScreenButton, lv_color_hex(0xCC0000),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_SendScreenButton, LV_OPA_COVER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_remove_state(ui_SendScreenButton, LV_STATE_DISABLED);
        lv_label_set_text(ui_SendButtonLabel, "STOP");
        lv_obj_set_style_text_color(ui_SendButtonLabel, lv_color_hex(0xFFFFFF),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        if (s_colorSaved) {
            lv_obj_set_style_bg_color(ui_SendScreenButton, s_origColor,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_obj_add_state(ui_SendScreenButton, LV_STATE_DISABLED);
        lv_label_set_text(ui_SendButtonLabel, "Send");
        lv_obj_set_style_text_color(ui_SendButtonLabel, lv_color_hex(0x000000),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void stopMorse()
{
    if (!g_morseTx) return;
    digitalWrite(MORSE_TX_PIN, MORSE_TX_OFF);
    g_morseTx = false;
    g_txUiPending = -1;   // loop() will re-enable buttons
    Serial.println("TX stopped.");
}

void startMorse(int slot)
{
    if (g_morseTx) return;   // already transmitting — ignore new tap

    const char *msg = getMessage(slot);
    if (msg[0] == '\0') return;   // empty slot guard — nothing to send

    int count = buildMorseSequence(msg, g_wpm, g_morseSeq, MORSE_BUF_SIZE);
    if (count <= 0) return;

    g_morseCount   = count;
    g_morseIndex   = 0;
    g_morseStartMs = millis();
    g_morseTx      = true;
    g_txUiPending  = 1;    // loop() will disable buttons + activate stop
    digitalWrite(MORSE_TX_PIN, MORSE_TX_ON);   // first interval is always a mark
    Serial.printf("TX: \"%s\" (%d intervals @ %d WPM)\n", msg, count, g_wpm);
}

void morseUpdate()
{
    if (!g_morseTx) return;

    if ((uint32_t)(millis() - g_morseStartMs) < g_morseSeq[g_morseIndex]) return;

    g_morseIndex++;

    if (g_morseIndex >= g_morseCount) {
        // Sequence complete
        digitalWrite(MORSE_TX_PIN, MORSE_TX_OFF);
        g_morseTx = false;
        g_txUiPending = -1;   // loop() will re-enable buttons
        Serial.println("TX done.");
        return;
    }

    // Even index = MARK (LED on), odd index = SPACE (LED off)
    digitalWrite(MORSE_TX_PIN, (g_morseIndex % 2 == 0) ? MORSE_TX_ON : MORSE_TX_OFF);
    g_morseStartMs = millis();
}

static void startMorseText(const char *text)
{
    if (g_morseTx) return;
    if (!text || text[0] == '\0') return;

    int count = buildMorseSequence(text, g_wpm, g_morseSeq, MORSE_BUF_SIZE);
    if (count <= 0) return;

    g_morseCount   = count;
    g_morseIndex   = 0;
    g_morseStartMs = millis();
    g_morseTx      = true;
    g_txUiPending  = 1;
    digitalWrite(MORSE_TX_PIN, MORSE_TX_ON);
    Serial.printf("TX (kbd): \"%s\" (%d intervals @ %d WPM)\n", text, count, g_wpm);
}

// ---------------------------------------------------------------------------
// Phase 7/8: send button + stop button callbacks (called from ui.c via extern "C")
// ---------------------------------------------------------------------------
extern "C" void onSendButton(int slot)
{
    startMorse(slot);
}

extern "C" void onStopButton()
{
    stopMorse();
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(3000);   // wait for Serial Monitor to connect before printing
    Serial.println("\n=== CYD Morse Sender — Phase 1 (LVGL 9) ===");

    // --- Morse TX pin (Red LED / future relay) ---
    pinMode(MORSE_TX_PIN, OUTPUT);
    digitalWrite(MORSE_TX_PIN, MORSE_TX_OFF);

    // --- Backlight (must come before tft.init) ---
    pinMode(CYD_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(CYD_BACKLIGHT_PIN, HIGH);

    // --- Display ---
    tft.init();
    // Rotation 2: 180° from rotation 0, gives correct landscape orientation.
    // Rotation 0 filled the full screen but was upside-down and X-mirrored.
    // Rotation 2 corrects both axes with no GRAM addressing issues on a clean init.
    tft.setRotation(2);
    tft.fillScreen(TFT_BLACK);

    // --- Touchscreen on VSPI ---
    // Drive CS HIGH before starting SPI so the XPT2046 is deselected during init.
    // Pass -1 (no hardware SS) to avoid the ESP32 SPI peripheral fighting the
    // library's software CS management on GPIO 33.
    pinMode(XPT2046_CS, OUTPUT);
    digitalWrite(XPT2046_CS, HIGH);
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, -1);
    touchscreen.begin(touchscreenSPI);
    touchscreen.setRotation(2);

    // --- LVGL 9 init ---
    lv_init();

    g_disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(g_disp, my_disp_flush);
    lv_display_set_buffers(g_disp, buf1, NULL,
                           sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    g_indev = lv_indev_create();
    lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_indev, my_touchpad_read);
    // Note: lv_indev_set_display() is intentionally omitted.
    // LVGL falls back to lv_display_get_default() for hit-testing, which is g_disp.
    // Setting it explicitly in some 9.1 builds can break the active-screen lookup.

    // --- SquareLine UI ---
    ui_init();

    // --- Phase 2 & 3: load persisted messages + WPM, apply to UI ---
    loadAllMessages();
    syncSendButtonLabels();

    // Black text on send buttons and all three "Send" nav tab labels.
    lv_obj_t * const blackTextLabels[] = {
        ui_SendButton1Label, ui_SendButton2Label, ui_SendButton3Label,
        ui_SendButton4Label, ui_SendButton5Label, ui_SendButton6Label,
        ui_SendButton7Label,
        ui_SendButtonLabel,       // Send screen nav tab
        ui_SendScreenButtonLabel1, // Keyboard screen nav tab
        ui_SendScreenButtonLabel2  // Config screen nav tab
    };
    for (int i = 0; i < (int)(sizeof(blackTextLabels) / sizeof(blackTextLabels[0])); i++) {
        lv_obj_set_style_text_color(blackTextLabels[i], lv_color_hex(0x000000),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(blackTextLabels[i], LV_OPA_COVER,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    // Sync slider position and value label to the loaded WPM.
    lv_slider_set_value(ui_SpeedSlider, g_wpm, LV_ANIM_OFF);
    char wpmBuf[8];
    snprintf(wpmBuf, sizeof(wpmBuf), "%d", g_wpm);
    lv_label_set_text(ui_SpeedValueLabel, wpmBuf);

    Serial.println("Ready.");
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void loop()
{
    // Both timers must be forced on every iteration:
    // - indev timer:   event dispatch (hit-test, SHORT_CLICKED, screen navigation) only
    //                  runs through lv_indev_read_timer_cb, not through lv_indev_read().
    // - display timer: after a screen switch lv_screen_load_anim() does not automatically
    //                  schedule a repaint; forcing the refresh timer ensures every frame
    //                  is rendered without waiting for the 30 ms period to elapse.
    morseUpdate();

    // Apply deferred LVGL UI updates outside lv_timer_handler() to avoid
    // modifying object states from within the event dispatch call chain.
    if (g_txUiPending == 1) {
        g_txUiPending = 0;
        setSendButtonsEnabled(false);
        setStopButtonActive(true);
    } else if (g_txUiPending == -1) {
        g_txUiPending = 0;
        setSendButtonsEnabled(true);
        setStopButtonActive(false);
    }

    // Start any keyboard-initiated one-shot TX queued by onKeyboardConfirm.
    // Deferred here (outside lv_timer_handler) to avoid starting the TX state
    // machine from within LVGL's event dispatch chain.
    if (g_pendingTxText[0] != '\0' && !g_morseTx) {
        startMorseText(g_pendingTxText);
        g_pendingTxText[0] = '\0';
    }

    // Split into two lv_timer_handler() calls to decouple event dispatch from
    // rendering.  Forcing both timers in a single call causes LVGL 9.1 to crash
    // when a button SHORT_CLICKED event and a dirty-area render occur together.
    //
    // Call 1: force indev → dispatches touch events; display timer not forced so
    //         it only fires if its natural ~33ms period has already elapsed.
    lv_timer_ready(lv_indev_get_read_timer(g_indev));
    lv_timer_handler();
    //
    // Call 2: force display → renders dirty areas; indev timer was just serviced
    //         so its period hasn't elapsed again and it won't re-fire here.
    lv_timer_ready(lv_display_get_refr_timer(g_disp));
    lv_timer_handler();
    delay(5);
}
