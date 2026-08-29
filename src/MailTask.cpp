// ─────────────────────────────────────────────────────────────────────────────
//  MailTask.cpp — polls an IMAP mailbox for unread mail.
//  Core 0, low priority. Reconnects fresh each poll (see MailClient).
//
//  SETUP: set IMAP_HOST / IMAP_USER / IMAP_PASS in Config.h.
//  Gmail: enable IMAP in Gmail settings, use an App Password
//  (myaccount.google.com/apppasswords) — not your normal login password.
// ─────────────────────────────────────────────────────────────────────────────
#include "Config.h"
#include "NavState.h"
#include "Suggestion.h"
#include "MailClient.h"
#include <string.h>

static MailClient s_mail;

bool mail_init() {
    bool configured = strlen(IMAP_USER) > 0 && strcmp(IMAP_USER, "you@example.com") != 0;
    WITH_STATE([&]{ g_state.mail.enabled = configured; });
    if (!configured) log_w("MailTask: IMAP_USER not set in Config.h — mail widget disabled");
    return configured;
}

void MailTask(void* pvParams) {
    if (!g_state.mail.enabled) {
        vTaskDelete(nullptr);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(6000)); // let Wi-Fi come up first

    uint16_t prev_unread = 0;
    bool     first_poll  = true;

    for (;;) {
        uint16_t unread = 0;
        MailClient::Item items[MAIL_MAX_ITEMS];
        uint8_t count = 0;

        bool ok = s_mail.poll(IMAP_HOST, IMAP_PORT, IMAP_USER, IMAP_PASS,
                               unread, items, MAIL_MAX_ITEMS, count);

        WITH_STATE([&]{
            g_state.mail.connected     = ok;
            g_state.mail.error         = !ok;
            g_state.mail.last_check_ms = millis();
            if (ok) {
                g_state.mail.unread_count = unread;
                g_state.mail.item_count   = count;
                for (uint8_t i = 0; i < count; i++) {
                    strlcpy(g_state.mail.items[i].from,    items[i].from.c_str(),    sizeof(g_state.mail.items[i].from));
                    strlcpy(g_state.mail.items[i].subject, items[i].subject.c_str(), sizeof(g_state.mail.items[i].subject));
                }
            }
        });

        if (ok) {
            if (!first_poll && unread > prev_unread) {
                nav_log_event(EventType::MAIL_NEW, (float)(unread - prev_unread));
                suggestion_offer(SuggestionType::MAIL_TRIAGE,
                    (unread == prev_unread + 1) ? "New email -- quick triage break?"
                                                 : "Several new emails -- quick triage break?");
            }
            prev_unread = unread;
            first_poll  = false;
        } else {
            log_w("MailTask: poll failed");
        }

        vTaskDelay(pdMS_TO_TICKS(MAIL_CHECK_INTERVAL_SEC * 1000));
    }
}
