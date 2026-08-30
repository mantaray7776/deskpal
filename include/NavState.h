#pragma once
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ─────────────────────────────────────────────────────────────────────────────
//  NavState  —  single shared struct for ALL sensor data and derived values.
//  SensorTask writes it under mutex. Every other task reads it.
//  Adding a new sensor = add a field here + populate in SensorTask.
//  Display / Web code never needs to change.
// ─────────────────────────────────────────────────────────────────────────────

struct SensorRaw {
    float ax, ay, az;       // accelerometer  (m/s²)
    float gx, gy, gz;       // gyroscope      (rad/s)
    float mx, my, mz;       // magnetometer   (µT)
    float temp_bmp;         // BMP390 temperature (°C)  — 0 if DNP
    float pressure;         // BMP390 pressure (hPa)    — 0 if DNP
};

struct Orientation {
    float roll, pitch, yaw; // Euler angles (degrees)
    float heading;          // tilt-compensated compass heading (0–360°)
    float q0, q1, q2, q3;  // Mahony quaternion (for WebSocket 3D view)
};

struct MotionState {
    bool     moving;            // true if RMS accel > threshold
    float    vibration_rms;     // RMS of accel magnitude delta (desk impact metric)
    float    em_variance;       // variance of mag vector magnitude (EM interference)
    float    noise_db;         
    uint32_t impact_count;      // cumulative desk impact events
    uint32_t em_spike_count;    // cumulative EM spike events
};

struct SessionState {
    bool     active;            // session running
    uint32_t elapsed_sec;       // seconds into current session
    uint32_t session_duration;  // target duration (default 25 min = 1500 s)
    uint8_t  pomodoro_count;    // completed pomodoros today
    uint8_t  distraction_score; // 0–100, computed by DistractTask
};

struct PowerState {
    float    batt_voltage;      // battery voltage (V) — 0 if DNP
    uint8_t  batt_percent;      // 0–100            — 0 if DNP
    bool     charging;          // true if VBUS present
};

struct WifiState {
    bool     connected;         // true once STA (home Wi-Fi) joins
    int8_t   rssi;
    char     ip[16];            // STA IP — "" if not connected yet
    char     ap_ip[16];         // always-on softAP IP (DeskMon-Setup), e.g. 192.168.4.1
    uint8_t  ws_clients;        // active WebSocket connections
};

// ── Mail (IMAP) ──────────────────────────────────────────────────────────────
#define MAIL_MAX_ITEMS 5

struct MailItem {
    char from[40];
    char subject[64];
};

struct MailState {
    bool      enabled;          // IMAP creds look configured (set in Config.h)
    bool      connected;        // last poll succeeded
    bool      error;            // last poll failed
    uint16_t  unread_count;
    uint8_t   item_count;       // valid entries in items[]
    MailItem  items[MAIL_MAX_ITEMS];
    uint32_t  last_check_ms;
};

// ── Assistant suggestions ────────────────────────────────────────────────────
enum class SuggestionType : uint8_t {
    NONE = 0,
    FOCUS_TIME,     // calm + still + idle -> good time to start a session
    TAKE_BREAK,     // sustained high distraction during an active session
    MAIL_TRIAGE,    // unread count just jumped
    CALENDAR_SOON,  // upcoming meeting within CALENDAR_SOON_MIN
    TODO_OVERDUE,   // overdue task(s) in Todoist
    PHONE_NOTIF,    // new phone notification via the Android companion app
};

struct SuggestionState {
    SuggestionType type;
    char           text[80];
    uint32_t       created_ms;
    uint32_t       expires_ms;         // 0 = no auto-expiry
    bool           active;
    bool           auto_focus_enabled; // if true, FOCUS_TIME auto-starts instead of asking
};

// ── Calendar (ICS polling) ───────────────────────────────────────────────────
struct CalendarState {
    bool      enabled;
    bool      connected;
    bool      has_next_event;
    char      next_summary[48];
    uint32_t  next_start_epoch;   // unix time, 0 if none
    uint32_t  last_check_ms;
};

// ── Todo (Todoist) ───────────────────────────────────────────────────────────
#define TODO_MAX_ITEMS 5

struct TodoItem {
    char   content[48];
    bool   overdue;
};

struct TodoState {
    bool      enabled;
    bool      connected;
    uint16_t  open_count;
    uint8_t   item_count;
    TodoItem  items[TODO_MAX_ITEMS];
    uint32_t  last_check_ms;
};

// ── Phone notifications (Android companion app -> HTTP POST /notify) ────────
#define PHONE_NOTIF_MAX 5

struct PhoneNotifItem {
    char     app[24];
    char     title[40];
    char     text[64];
    uint32_t received_ms;
};

struct PhoneNotifState {
    bool            enabled;         // set true on first received POST
    uint8_t         unread_count;    // cleared when viewed on web dashboard
    uint8_t         item_count;      // valid entries in items[] (most recent first)
    PhoneNotifItem  items[PHONE_NOTIF_MAX];
    uint32_t        last_received_ms;
};

// ── Event log ring buffer ───────────────────────────────────────────────────
#define EVENT_LOG_SIZE 32

enum class EventType : uint8_t {
    NONE = 0,
    IMPACT,        // desk impact detected
    EM_SPIKE,      // EM interference spike
    SESSION_START,
    SESSION_END,
    POMODORO_DONE,
    WIFI_CONNECTED,
    WIFI_LOST,
    OTA_START,
    OTA_DONE,
    MAIL_NEW,
    SUGGESTION_SHOWN,
    SUGGESTION_ACCEPTED,
    SUGGESTION_DISMISSED,
    AUTO_FOCUS_STARTED,
    CALENDAR_EVENT_SOON,
    TODO_OVERDUE,
    PHONE_NOTIF_RECEIVED,
};

struct Event {
    EventType   type;
    uint32_t    timestamp_ms;
    float       value;          // optional numeric payload
};

// ── Master NavState ─────────────────────────────────────────────────────────
struct NavState {
    SensorRaw   raw;
    Orientation orient;
    MotionState motion;
    SessionState session;
    PowerState   power;
    WifiState    wifi;

    MailState        mail;
    SuggestionState  suggestion;
    CalendarState    calendar;
    TodoState        todo;
    PhoneNotifState  phone;

    // Event ring buffer
    Event       events[EVENT_LOG_SIZE];
    uint8_t     event_head;     // next write index
    uint32_t    event_total;    // total events ever logged

    // Timing
    uint32_t    last_sensor_ms; // millis of last sensor update
    uint32_t    uptime_sec;
};

// ── Global singleton + mutex ─────────────────────────────────────────────────
extern NavState         g_state;
extern SemaphoreHandle_t g_state_mutex;

// Serializes network polling across MailTask/CalendarTask/TodoTask so at
// most one TLS connection (WiFiClientSecure) is ever live at a time. Each of
// those tasks independently opens a fairly heavy TLS session; letting two or
// three overlap was a plausible cause of RAM exhaustion / crash-reboots on
// this board (TFT framebuffer + ML model + Wi-Fi stack already use a lot of
// the available RAM). Long timeout (most of a poll cycle) since a legitimate
// IMAP/HTTP round trip can itself take several seconds.
extern SemaphoreHandle_t g_net_mutex;

#define WITH_NET_LOCK(timeout_ms, ...) do { \
    if (g_net_mutex != NULL && xSemaphoreTake(g_net_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) { \
        __VA_ARGS__(); \
        xSemaphoreGive(g_net_mutex); \
    } \
} while(0)

// Helper: acquire mutex, run lambda, release
// Usage:  WITH_STATE([&]{ g_state.session.active = true; });
// include/NavState.h

#define WITH_STATE(...) do { \
    if (g_state_mutex != NULL && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) { \
        __VA_ARGS__(); \
        xSemaphoreGive(g_state_mutex); \
    } \
} while(0)

// Safe snapshot for tasks that need a consistent read of the WHOLE struct.
// Use only when you actually touch fields across multiple sub-structs
// (DisplayTask, WebTask, SuggestionTask's main loop). For anything narrower,
// use the accessors below — they cost a few bytes of stack instead of
// sizeof(NavState) (2172 bytes), and are safe to nest/call from small-stack
// tasks without risking a stack-canary overflow.
inline void state_snapshot(NavState& out) {
    if (g_state_mutex != NULL && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        out = g_state;
        xSemaphoreGive(g_state_mutex);
    }
}

inline SuggestionState suggestion_snapshot() {
    SuggestionState s{};
    if (g_state_mutex != NULL && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s = g_state.suggestion;
        xSemaphoreGive(g_state_mutex);
    }
    return s;
}

inline SessionState session_snapshot() {
    SessionState s{};
    if (g_state_mutex != NULL && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s = g_state.session;
        xSemaphoreGive(g_state_mutex);
    }
    return s;
}

inline MotionState motion_snapshot() {
    MotionState s{};
    if (g_state_mutex != NULL && xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s = g_state.motion;
        xSemaphoreGive(g_state_mutex);
    }
    return s;
}

void nav_log_event(EventType type, float value = 0.0f);