// ─────────────────────────────────────────────────────────────────────────────
//  SuggestionTask.cpp — the assistant's suggestion engine.
//
//  Watches for two conditions and offers a suggestion (or auto-acts, if the
//  user has enabled auto-focus):
//    - "settled": calm + still + no session running for SUGGEST_SETTLE_SEC
//      -> suggest (or auto-start) a focus session.
//    - "sustained distraction": high distraction score for SUGGEST_BREAK_SEC
//      while a session is active -> suggest a break.
//  Other tasks (MailTask, CalendarTask, TodoTask, phone-notif handler) call
//  suggestion_offer() directly for their own triggers (new mail, meeting
//  soon, overdue task, new phone notification).
//
//  Input: the BOOT button (GPIO0 on most ESP32-S3 devkits) doubles as the
//  accept/dismiss control so suggestions are actionable without the web UI —
//  short press = accept, long press = dismiss.
// ─────────────────────────────────────────────────────────────────────────────
#include "Config.h"
#include "NavState.h"
#include "Suggestion.h"
#include <Arduino.h>

// Per-type cooldown bookkeeping (not part of NavState — purely internal).
static uint32_t s_last_offer_ms[6] = {0, 0, 0, 0, 0, 0}; // indexed by (SuggestionType - 1)

static inline uint8_t type_index(SuggestionType t) { return (uint8_t)t - 1; }

bool suggestion_init() {
    WITH_STATE([&]{ g_state.suggestion.auto_focus_enabled = (AUTO_FOCUS_DEFAULT != 0); });
    return true;
}

void suggestion_offer(SuggestionType type, const char* text, uint32_t ttl_sec) {
    if (type == SuggestionType::NONE) return;

    NavState st = state_snapshot();
    if (st.suggestion.active) return; // don't clobber one the user hasn't answered yet

    uint8_t  idx = type_index(type);
    uint32_t now = millis();
    if (s_last_offer_ms[idx] != 0 &&
        now - s_last_offer_ms[idx] < (uint32_t)SUGGEST_COOLDOWN_SEC * 1000) {
        return; // still cooling down for this suggestion type
    }
    s_last_offer_ms[idx] = now;

    WITH_STATE([&]{
        g_state.suggestion.type       = type;
        strlcpy(g_state.suggestion.text, text, sizeof(g_state.suggestion.text));
        g_state.suggestion.created_ms = now;
        g_state.suggestion.expires_ms = ttl_sec ? now + ttl_sec * 1000 : 0;
        g_state.suggestion.active     = true;
    });
    nav_log_event(EventType::SUGGESTION_SHOWN, (float)(uint8_t)type);
}

void suggestion_accept() {
    NavState st = state_snapshot();
    if (!st.suggestion.active) return;
    SuggestionType t = st.suggestion.type;

    WITH_STATE([&]{ g_state.suggestion.active = false; });
    nav_log_event(EventType::SUGGESTION_ACCEPTED, (float)(uint8_t)t);

    if (t == SuggestionType::FOCUS_TIME) {
        WITH_STATE([&]{
            g_state.session.active      = true;
            g_state.session.elapsed_sec = 0;
        });
        nav_log_event(EventType::SESSION_START);
    } else if (t == SuggestionType::TAKE_BREAK) {
        WITH_STATE([&]{ g_state.session.active = false; });
        nav_log_event(EventType::SESSION_END);
    }
    // MAIL_TRIAGE / CALENDAR / TODO suggestions are informational only —
    // nothing further to do on-device.
}

void suggestion_dismiss() {
    NavState st = state_snapshot();
    if (!st.suggestion.active) return;
    nav_log_event(EventType::SUGGESTION_DISMISSED, (float)(uint8_t)st.suggestion.type);
    WITH_STATE([&]{ g_state.suggestion.active = false; });
}

void SuggestionTask(void* pvParams) {
    pinMode(PIN_SUGGEST_BTN, INPUT_PULLUP); // active LOW

    uint32_t settle_sec = 0, break_sec = 0;
    uint32_t btn_down_since = 0;
    bool     btn_was_down = false;
    uint8_t  tick_div = 0; // 20 ticks @ 50ms = 1 Hz for the slower logic below

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        // ── button: poll every cycle (50 ms) so presses feel responsive ──
        bool down = (digitalRead(PIN_SUGGEST_BTN) == LOW);
        if (down && !btn_was_down) {
            btn_down_since = millis();
        } else if (!down && btn_was_down) {
            uint32_t held = millis() - btn_down_since;
            NavState st = state_snapshot();
            if (st.suggestion.active) {
                if (held >= SUGGEST_BTN_LONGPRESS_MS) suggestion_dismiss();
                else                                  suggestion_accept();
            }
        }
        btn_was_down = down;

        // ── 1 Hz: expiry + settle/break timers ──
        if (++tick_div >= 20) {
            tick_div = 0;
            NavState st = state_snapshot();

            if (st.suggestion.active && st.suggestion.expires_ms &&
                millis() > st.suggestion.expires_ms) {
                WITH_STATE([&]{ g_state.suggestion.active = false; });
            }

            bool calm = !st.motion.moving && st.session.distraction_score < 30;
            settle_sec = (calm && !st.session.active) ? settle_sec + 1 : 0;

            if (settle_sec >= SUGGEST_SETTLE_SEC) {
                settle_sec = 0;
                if (st.suggestion.auto_focus_enabled) {
                    WITH_STATE([&]{
                        g_state.session.active      = true;
                        g_state.session.elapsed_sec = 0;
                    });
                    nav_log_event(EventType::AUTO_FOCUS_STARTED);
                    nav_log_event(EventType::SESSION_START);
                    suggestion_offer(SuggestionType::FOCUS_TIME, "Auto-started a focus session", 6);
                } else {
                    suggestion_offer(SuggestionType::FOCUS_TIME, "Looks calm -- start a focus session?");
                }
            }

            break_sec = (st.session.active && st.session.distraction_score > 80)
                        ? break_sec + 1 : 0;
            if (break_sec >= SUGGEST_BREAK_SEC) {
                break_sec = 0;
                suggestion_offer(SuggestionType::TAKE_BREAK, "High interference -- take a break?");
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50));
    }
}
