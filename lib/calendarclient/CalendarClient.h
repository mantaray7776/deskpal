#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
//  CalendarClient — streams a plain HTTPS .ics feed and finds the soonest
//  upcoming VEVENT. Works with any calendar that publishes a secret iCal URL
//  (Google, Outlook, Fastmail, ...) — no OAuth needed.
//
//  Limitations (kept intentionally simple for a desk-gadget widget):
//   - Recurring events (RRULE) are matched at their literal DTSTART only,
//     not expanded — a daily standup will show its first-ever occurrence,
//     which is usually in the past and gets filtered out. Add non-recurring
//     "quick" events to your calendar for reliable countdowns, or extend
//     this with real RRULE expansion if you need recurrence.
//   - Folded (line-wrapped) SUMMARY/DTSTART lines aren't unfolded — fine for
//     the typical short values these fields have.
// ─────────────────────────────────────────────────────────────────────────────
class CalendarClient {
public:
    // now_epoch: current UTC unix time (see calendar_init()'s NTP setup).
    // Returns true and fills summary_out/start_epoch_out if a future event
    // was found in the feed.
    bool fetchNextEvent(const char* url, uint32_t now_epoch,
                         char* summary_out, size_t summary_len,
                         uint32_t& start_epoch_out);
};
