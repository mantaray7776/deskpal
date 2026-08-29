// ─────────────────────────────────────────────────────────────────────────────
//  CalendarTask.cpp — polls a plain .ics feed for the next upcoming event and
//  fires a "meeting soon" suggestion inside CALENDAR_SOON_MIN of it starting.
//  Requires NTP time (set up here) to know "now" and to compare event times.
// ─────────────────────────────────────────────────────────────────────────────
#include "Config.h"
#include "NavState.h"
#include "Suggestion.h"
#include "CalendarClient.h"
#include <time.h>

static CalendarClient s_cal;

bool calendar_init() {
    bool configured = strlen(CALENDAR_ICS_URL) > 0;
    WITH_STATE([&]{ g_state.calendar.enabled = configured; });
    if (configured) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // UTC, no DST offset
    } else {
        log_w("CalendarTask: CALENDAR_ICS_URL not set in Config.h — calendar widget disabled");
    }
    return configured;
}

void CalendarTask(void* pvParams) {
    if (!g_state.calendar.enabled) {
        vTaskDelete(nullptr);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(8000)); // let Wi-Fi + NTP settle first

    uint32_t last_notified_start = 0; // avoid re-suggesting the same event repeatedly

    for (;;) {
        time_t now;
        time(&now);

        if (now > 100000) { // sane epoch => NTP has synced
            char summary[48];
            uint32_t start_epoch = 0;
            bool ok = s_cal.fetchNextEvent(CALENDAR_ICS_URL, (uint32_t)now,
                                            summary, sizeof(summary), start_epoch);

            WITH_STATE([&]{
                g_state.calendar.connected      = ok;
                g_state.calendar.last_check_ms  = millis();
                g_state.calendar.has_next_event = ok;
                if (ok) {
                    strlcpy(g_state.calendar.next_summary, summary, sizeof(g_state.calendar.next_summary));
                    g_state.calendar.next_start_epoch = start_epoch;
                }
            });

            if (ok) {
                uint32_t mins_until = (start_epoch > (uint32_t)now) ? (start_epoch - (uint32_t)now) / 60 : 0;
                if (mins_until <= CALENDAR_SOON_MIN && start_epoch != last_notified_start) {
                    last_notified_start = start_epoch;
                    nav_log_event(EventType::CALENDAR_EVENT_SOON, (float)mins_until);
                    char msg[80];
                    snprintf(msg, sizeof(msg), "\"%.30s\" in %u min", summary, (unsigned)mins_until);
                    suggestion_offer(SuggestionType::CALENDAR_SOON, msg, 60);
                }
            } else {
                log_w("CalendarTask: fetch failed");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CALENDAR_CHECK_INTERVAL_SEC * 1000));
    }
}
