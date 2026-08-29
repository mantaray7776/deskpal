#pragma once
#include <Arduino.h>
#include "NavState.h"   // TodoItem, TODO_MAX_ITEMS

// ─────────────────────────────────────────────────────────────────────────────
//  TodoClient — polls the Todoist REST API v2 for open tasks. Token-based,
//  no OAuth flow needed (todoist.com/app/settings/integrations/developer).
// ─────────────────────────────────────────────────────────────────────────────
class TodoClient {
public:
    // today_ymd: current UTC date as "YYYY-MM-DD", used to flag overdue tasks.
    // Fills up to max_items items, overdue tasks first. open_count = total
    // active task count (not just the ones returned in items[]).
    bool fetch(const char* token, const char* today_ymd,
               uint16_t& open_count, TodoItem* items, uint8_t max_items, uint8_t& item_count);
};
