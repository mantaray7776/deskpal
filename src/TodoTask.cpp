// ─────────────────────────────────────────────────────────────────────────────
//  TodoTask.cpp — polls Todoist for open tasks; suggests a nudge when tasks
//  are overdue. Requires NTP time (shared with CalendarTask) for "today".
// ─────────────────────────────────────────────────────────────────────────────
#include "Config.h"
#include "NavState.h"
#include "Suggestion.h"
#include "TodoClient.h"
#include <time.h>

static TodoClient s_todo;

bool todo_init() {
    bool configured = strlen(TODOIST_TOKEN) > 0;
    WITH_STATE([&]{ g_state.todo.enabled = configured; });
    if (configured) configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // harmless if already done
    else            log_w("TodoTask: TODOIST_TOKEN not set in Config.h — todo widget disabled");
    return configured;
}

void TodoTask(void* pvParams) {
    if (!g_state.todo.enabled) {
        vTaskDelete(nullptr);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(9000));

    for (;;) {
        time_t now;
        time(&now);
        char today[11] = "";
        if (now > 100000) {
            struct tm tmv;
            gmtime_r(&now, &tmv);
            strftime(today, sizeof(today), "%Y-%m-%d", &tmv);
        }

        uint16_t open_count = 0;
        TodoClient::Item items[TODO_MAX_ITEMS];
        uint8_t  count = 0;
        bool ok = false;
        WITH_NET_LOCK(30000, [&]{
            ok = s_todo.fetch(TODOIST_TOKEN, today, open_count, items, TODO_MAX_ITEMS, count);
        });

        WITH_STATE([&]{
            g_state.todo.connected     = ok;
            g_state.todo.last_check_ms = millis();
            if (ok) {
                g_state.todo.open_count = open_count;
                g_state.todo.item_count = count;
                for (uint8_t i = 0; i < count; i++) {
                    strlcpy(g_state.todo.items[i].content, items[i].content, sizeof(g_state.todo.items[i].content));
                    g_state.todo.items[i].overdue = items[i].overdue;
                }
            }
        });

        if (ok) {
            uint8_t overdue_count = 0;
            for (uint8_t i = 0; i < count; i++) if (items[i].overdue) overdue_count++;
            if (overdue_count > 0) {
                nav_log_event(EventType::TODO_OVERDUE, (float)overdue_count);
                char msg[80];
                if (overdue_count == 1) snprintf(msg, sizeof(msg), "1 overdue task -- worth a look?");
                else snprintf(msg, sizeof(msg), "%u overdue tasks -- worth a look?", overdue_count);
                suggestion_offer(SuggestionType::TODO_OVERDUE, msg, 30);
            }
        } else {
            log_w("TodoTask: fetch failed");
        }

        vTaskDelay(pdMS_TO_TICKS(TODO_CHECK_INTERVAL_SEC * 1000));
    }
}
