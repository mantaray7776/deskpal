#include "TodoClient.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

bool TodoClient::fetch(const char* token, const char* today_ymd,
                        uint16_t& open_count, Item* items, uint8_t max_items, uint8_t& item_count) {
    open_count = 0;
    item_count = 0;
    if (!token || strlen(token) == 0) return false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, "https://api.todoist.com/rest/v2/tasks")) return false;
    http.addHeader("Authorization", String("Bearer ") + token);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    // Filter to just the fields we use — keeps memory usage low regardless
    // of how many tasks or how much metadata Todoist sends per task.
    JsonDocument filter;
    JsonObject filterObj = filter.add<JsonObject>();
    filterObj["content"]     = true;
    filterObj["due"]["date"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) return false;

    JsonArray arr = doc.as<JsonArray>();
    open_count = arr.size();

    String today(today_ymd);
    uint8_t filled = 0;

    // Overdue tasks take priority in the widget — fill those first.
    for (JsonObject t : arr) {
        if (filled >= max_items) break;
        const char* content = t["content"] | "";
        const char* due     = t["due"]["date"] | "";
        bool overdue = today.length() && due[0] && String(due) < today;
        if (!overdue) continue;
        strlcpy(items[filled].content, content, sizeof(items[filled].content));
        items[filled].overdue = true;
        filled++;
    }
    for (JsonObject t : arr) {
        if (filled >= max_items) break;
        const char* content = t["content"] | "";
        const char* due     = t["due"]["date"] | "";
        bool overdue = today.length() && due[0] && String(due) < today;
        if (overdue) continue; // already added above
        strlcpy(items[filled].content, content, sizeof(items[filled].content));
        items[filled].overdue = false;
        filled++;
    }
    item_count = filled;
    return true;
}
