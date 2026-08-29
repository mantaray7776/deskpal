#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
//  TodoClient — polls the Todoist REST API v2 for open tasks. Token-based,
//  no OAuth flow needed (todoist.com/app/settings/integrations/developer).
//
//  Deliberately has zero dependency on the project's Config.h/NavState.h —
//  as a lib/ folder library it can't reliably reach the project's include/
//  directory in this environment, so it defines its own small Item struct;
//  TodoTask.cpp (in src/, which reliably sees both) converts into the
//  matching NavState TodoItem when copying results into shared state.
// ─────────────────────────────────────────────────────────────────────────────
class TodoClient {
public:
    struct Item {
        char content[48];
        bool overdue;
    };

    // today_ymd: current UTC date as "YYYY-MM-DD", used to flag overdue tasks.
    // Fills up to max_items items, overdue tasks first. open_count = total
    // active task count (not just the ones returned in items[]).
    bool fetch(const char* token, const char* today_ymd,
               uint16_t& open_count, Item* items, uint8_t max_items, uint8_t& item_count);
};
