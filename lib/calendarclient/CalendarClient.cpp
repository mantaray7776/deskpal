#include "CalendarClient.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Converts an ICS DTSTART value ("20260801T140000Z" / "...T140000" /
// "20260801") to a UTC unix epoch. Floating (no "Z") times are treated as
// UTC too — a known simplification; good enough for a countdown widget.
static uint32_t ics_datetime_to_epoch(const String& raw) {
    if (raw.length() < 8) return 0;
    int year  = raw.substring(0, 4).toInt();
    int month = raw.substring(4, 6).toInt();
    int day   = raw.substring(6, 8).toInt();
    int hour = 0, minute = 0, second = 0;
    int tpos = raw.indexOf('T');
    if (tpos >= 0 && raw.length() >= (size_t)(tpos + 7)) {
        hour   = raw.substring(tpos + 1, tpos + 3).toInt();
        minute = raw.substring(tpos + 3, tpos + 5).toInt();
        second = raw.substring(tpos + 5, tpos + 7).toInt();
    }
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) return 0;

    // Howard Hinnant's days_from_civil — standard, well-tested civil-date->
    // days-since-epoch conversion.
    int y = year;
    int m = month;
    y -= (m <= 2) ? 1 : 0;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097 + (long)doe - 719468;

    return (uint32_t)days * 86400UL + hour * 3600UL + minute * 60UL + second;
}

bool CalendarClient::fetchNextEvent(const char* url, uint32_t now_epoch,
                                     char* summary_out, size_t summary_len,
                                     uint32_t& start_epoch_out) {
    if (!url || strlen(url) == 0) return false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, url)) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();

    bool     inEvent = false;
    String   curSummary, curDtstart, line;
    uint32_t bestEpoch = 0;
    String   bestSummary;
    uint32_t lastByteMs = millis();

    // Stream line-by-line rather than buffering the whole feed — .ics files
    // can be large and ESP32 RAM is limited.
    while (http.connected() && (millis() - lastByteMs < 8000)) {
        if (!stream->available()) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        char c = (char)stream->read();
        lastByteMs = millis();

        if (c == '\n') {
            line.trim();
            if (line == "BEGIN:VEVENT") {
                inEvent    = true;
                curSummary = "";
                curDtstart = "";
            } else if (line == "END:VEVENT") {
                if (inEvent && curDtstart.length()) {
                    uint32_t ep = ics_datetime_to_epoch(curDtstart);
                    if (ep >= now_epoch && (bestEpoch == 0 || ep < bestEpoch)) {
                        bestEpoch   = ep;
                        bestSummary = curSummary;
                    }
                }
                inEvent = false;
            } else if (inEvent && line.startsWith("SUMMARY")) {
                int c2 = line.indexOf(':');
                if (c2 >= 0) curSummary = line.substring(c2 + 1);
            } else if (inEvent && line.startsWith("DTSTART")) {
                int c2 = line.indexOf(':');
                if (c2 >= 0) curDtstart = line.substring(c2 + 1);
            }
            line = "";
        } else if (c != '\r') {
            line += c;
        }
    }
    http.end();

    if (bestEpoch == 0) return false;
    start_epoch_out = bestEpoch;
    strlcpy(summary_out, bestSummary.length() ? bestSummary.c_str() : "(untitled)", summary_len);
    return true;
}
