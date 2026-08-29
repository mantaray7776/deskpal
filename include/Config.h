#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Config.h  —  all pin assignments and compile-time feature flags
//  Mirror of platformio.ini -D flags, with fallback defaults.
// ─────────────────────────────────────────────────────────────────────────────

// ── I2C ──────────────────────────────────────────────────────────────────────
#ifndef PIN_SDA
#define PIN_SDA             4
#endif
#ifndef PIN_SCL
#define PIN_SCL             5
#endif
#define I2C_FREQ_HZ         400000   // 400 kHz fast mode

// ── INMP441 MEMS Microphone (I2S) ────────────────────────────────────────────
#define PIN_MIC_SCK         38
#define PIN_MIC_WS          39
#define PIN_MIC_SD          40
#define MIC_SAMPLE_RATE     16000
#define MIC_BUF_LEN         512
#define NOISE_QUIET_DB      65.0f
#define NOISE_LOUD_DB       90.0f

// ── SPI (shared bus) ─────────────────────────────────────────────────────────
#ifndef PIN_MOSI
#define PIN_MOSI            18
#endif
#ifndef PIN_MISO
#define PIN_MISO            19
#endif
#ifndef PIN_SCLK
#define PIN_SCLK            20
#endif

// ── TFT ILI9341 ──────────────────────────────────────────────────────────────
#ifndef PIN_TFT_CS
#define PIN_TFT_CS          21
#endif
#ifndef PIN_TFT_DC
#define PIN_TFT_DC          22
#endif
#ifndef PIN_TFT_RST
#define PIN_TFT_RST         23
#endif
#ifndef PIN_TFT_BL
#define PIN_TFT_BL          24
#endif
#define TFT_BL_FREQ         5000     // backlight PWM frequency
#define TFT_BL_DUTY         200      // 0–255

// ── I2C addresses ────────────────────────────────────────────────────────────
#define ADDR_OLED           0x3C
#define ADDR_MPU6050        0x68     // AD0 = GND
#define ADDR_HMC5883L       0x1E
#define ADDR_BMP390         0x76

// ── Interrupt / GPIO lines ───────────────────────────────────────────────────
#ifndef PIN_MPU_INT
#define PIN_MPU_INT         6
#endif
#ifndef PIN_HMC_DRDY
#define PIN_HMC_DRDY        7
#endif

// ── DNP: microSD ─────────────────────────────────────────────────────────────
#ifndef PIN_SD_CS
#define PIN_SD_CS           15
#endif

// ── DNP: LoRa RA-02 SX1278 ───────────────────────────────────────────────────
#ifndef PIN_LORA_CS
#define PIN_LORA_CS         16
#endif
#ifndef PIN_LORA_RST
#define PIN_LORA_RST        17
#endif
#ifndef PIN_LORA_DIO0
#define PIN_LORA_DIO0       11
#endif
#define LORA_FREQUENCY      433E6

// ── DNP: Rotary encoder ──────────────────────────────────────────────────────
#ifndef PIN_ENC_A
#define PIN_ENC_A           8
#endif
#ifndef PIN_ENC_B
#define PIN_ENC_B           9
#endif
#ifndef PIN_ENC_SW
#define PIN_ENC_SW          12
#endif

// ── DNP: Battery ADC ─────────────────────────────────────────────────────────
#ifndef PIN_BATT_ADC
#define PIN_BATT_ADC        1
#endif
#define BATT_DIVIDER_RATIO  2.0f    // 100k/100k divider
#define BATT_FULL_V         4.2f
#define BATT_EMPTY_V        3.2f

// ── Feature flags (set in platformio.ini -D, default off) ───────────────────
#ifndef FEATURE_SD
#define FEATURE_SD          0
#endif
#ifndef FEATURE_LORA
#define FEATURE_LORA        0
#endif
#ifndef FEATURE_ENCODER
#define FEATURE_ENCODER     0
#endif
#ifndef FEATURE_BATT
#define FEATURE_BATT        0
#endif
#ifndef FEATURE_BMP
#define FEATURE_BMP         0
#endif
#ifndef FEATURE_BUZZER
#define FEATURE_BUZZER      0
#endif

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID           "YOUR_SSID"
#define WIFI_PASSWORD       "YOUR_PASSWORD"
#define MDNS_HOSTNAME       "deskmon"    // → deskmon.local
#define WS_PORT             81

// ── Session defaults ─────────────────────────────────────────────────────────
#define SESSION_DEFAULT_SEC (25 * 60)   // 25-minute pomodoro
#define BREAK_SHORT_SEC     (5  * 60)
#define BREAK_LONG_SEC      (15 * 60)

// ── Sensor thresholds (tunable via WebSocket config packet) ──────────────────
#define IMPACT_THRESHOLD    0.2f    // vib معمولاً 0.05–0.09 هست
#define EM_SPIKE_THRESHOLD  200.0f   // em معمولاً زیر 130 هست
#define MOVING_THRESHOLD    0.15f       // m/s² to consider device moving

// ── Task stack / priority ────────────────────────────────────────────────────
#define STACK_SENSOR        4096
#define STACK_DISPLAY       6144
#define STACK_WEB           8192
#define STACK_DISTRACT      3072
#define STACK_SESSION       2048

#define PRI_SENSOR          5           // highest — real-time sensor fusion
#define PRI_DISPLAY         3
#define PRI_WEB             4
#define PRI_DISTRACT        2
#define PRI_SESSION         1

#define CORE_SENSOR         1           // dedicated sensor core
#define CORE_DISPLAY        1
#define CORE_WEB            0           // Wi-Fi / TCP core
#define CORE_DISTRACT       0
#define CORE_SESSION        0

// ── IMAP (email polling) ──────────────────────────────────────────────────────
// Gmail: enable IMAP in Gmail settings, and use an *App Password*
// (myaccount.google.com/apppasswords) — NOT your normal login password,
// since Gmail rejects plain LOGIN on 2FA-protected accounts.
#ifndef IMAP_HOST
#define IMAP_HOST           "imap.gmail.com"
#endif
#ifndef IMAP_PORT
#define IMAP_PORT           993
#endif
#ifndef IMAP_USER
#define IMAP_USER           "you@example.com"
#endif
#ifndef IMAP_PASS
#define IMAP_PASS           "your-app-password"
#endif
#define MAIL_CHECK_INTERVAL_SEC   60
#define MAIL_CONN_TIMEOUT_MS      8000

// ── Calendar (ICS feed polling) ──────────────────────────────────────────────
// Google Calendar: Settings → your calendar → "Secret address in iCal format".
// Any plain-HTTPS .ics URL works (Outlook, Fastmail, etc. all publish one).
#ifndef CALENDAR_ICS_URL
#define CALENDAR_ICS_URL    ""     // leave blank to disable
#endif
#define CALENDAR_CHECK_INTERVAL_SEC  300
#define CALENDAR_SOON_MIN            10     // "meeting soon" suggestion threshold

// ── Todo (Todoist REST API) ──────────────────────────────────────────────────
// Get a token at todoist.com/app/settings/integrations/developer
#ifndef TODOIST_TOKEN
#define TODOIST_TOKEN       ""     // leave blank to disable
#endif
#define TODO_CHECK_INTERVAL_SEC   120

// ── Phone notifications (Android companion app → HTTP POST /notify) ─────────
// No cloud relay: the companion app runs a NotificationListenerService and
// POSTs JSON straight to http://deskmon.local/notify on your LAN whenever a
// notification appears. See lib/androidcompanion/ for the companion app.
#define PHONE_NOTIF_STALE_SEC     3600   // widget clears items older than this

// ── Suggestion / auto-focus engine ───────────────────────────────────────────
#define SUGGEST_SETTLE_SEC        45     // calm+still+idle seconds before suggesting focus
#define SUGGEST_BREAK_SEC         30     // sustained high-distraction seconds before suggesting a break
#define SUGGEST_EXPIRE_SEC        20     // auto-dismiss an unanswered suggestion
#define SUGGEST_COOLDOWN_SEC      300    // don't re-suggest same type for 5 min after dismiss
#define AUTO_FOCUS_DEFAULT        0      // 0 = suggest only, 1 = auto-start

#ifndef PIN_SUGGEST_BTN
#define PIN_SUGGEST_BTN     0            // BOOT button on most ESP32-S3 devkits
#endif
#define SUGGEST_BTN_LONGPRESS_MS  700

// ── Task stack / priority — new tasks ────────────────────────────────────────
#define STACK_MAIL          8192   // TLS needs a decent stack
#define STACK_CALENDAR      8192
#define STACK_TODO          6144
#define STACK_SUGGEST       3072

#define PRI_MAIL            1
#define PRI_CALENDAR        1
#define PRI_TODO            1
#define PRI_SUGGEST         2

#define CORE_MAIL           0
#define CORE_CALENDAR       0
#define CORE_TODO           0
#define CORE_SUGGEST        0
