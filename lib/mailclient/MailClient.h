#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>

// ─────────────────────────────────────────────────────────────────────────────
//  MailClient — minimal IMAPS client for polling unread mail from an ESP32.
//
//  Deliberately narrow: it only speaks the handful of IMAP commands needed to
//  get an unread count and a few From/Subject headers (STATUS, SELECT,
//  UID SEARCH, UID FETCH ... HEADER.FIELDS). It is NOT a general IMAP library.
//
//  Reconnects fresh on every poll() call rather than holding a persistent TLS
//  session — simpler and more reliable across a ~60s idle gap than keeping a
//  socket alive and hoping the server (or NAT) doesn't drop it.
//
//  Security note: uses setInsecure() (no certificate validation) to keep this
//  practical on an ESP32 without provisioning a CA bundle. Fine for a hobby
//  desk gadget on a trusted home network; pin the server cert if you want more.
// ─────────────────────────────────────────────────────────────────────────────
class MailClient {
public:
    struct Item {
        String from;
        String subject;
    };

    // Connects, logs in, checks INBOX, and (if unread > 0) fetches headers for
    // up to max_items of the most recent unread messages. Always disconnects
    // before returning. Returns false on any connection/login/protocol error.
    // timeout_ms: caller-supplied network timeout (pass MAIL_CONN_TIMEOUT_MS
    // from Config.h) — kept as a parameter rather than read here directly so
    // this library has zero dependency on the project's Config.h.
    bool poll(const char* host, uint16_t port, const char* user, const char* pass,
              uint16_t& unread_count, Item* items, uint8_t max_items, uint8_t& item_count,
              uint32_t timeout_ms = 8000);

private:
    WiFiClientSecure _client;
    uint16_t         _tagNum = 1;

    String nextTag();
    bool   readLine(String& out, uint32_t timeout_ms);
    // Sends "<tag> <cmd>\r\n" and reads lines until the tagged completion
    // line ("<tag> OK/NO/BAD ...") is seen. Full response (all lines,
    // including untagged "* ..." lines) is returned in `response`.
    bool   command(const String& cmd, String& response, uint32_t timeout_ms = 8000);
    static String extractHeaderField(const String& block, const char* field);
    static void   parseUnseenUids(const String& searchResponse, int* uids, int maxUids, int& count);
};
