#include "MailClient.h"

String MailClient::nextTag() {
    char buf[8];
    snprintf(buf, sizeof(buf), "a%u", _tagNum++);
    return String(buf);
}

bool MailClient::readLine(String& out, uint32_t timeout_ms) {
    uint32_t start = millis();
    out = "";
    while (millis() - start < timeout_ms) {
        while (_client.available()) {
            char c = (char)_client.read();
            if (c == '\n') {
                out.trim();
                return true;
            }
            if (c != '\r') out += c;
        }
        if (!_client.connected()) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false; // timed out
}

bool MailClient::command(const String& cmd, String& response, uint32_t timeout_ms) {
    String tag = nextTag();
    _client.print(tag);
    _client.print(' ');
    _client.print(cmd);
    _client.print("\r\n");

    response = "";
    String line;
    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        if (!readLine(line, timeout_ms - (millis() - start))) return false;
        response += line;
        response += '\n';
        if (line.startsWith(tag + " ")) {
            // Tagged completion line — command finished, one way or another.
            return line.startsWith(tag + " OK");
        }
    }
    return false; // timed out waiting for completion
}

// Very small header-field extractor: finds "Field: value" (case-insensitive,
// single line only — good enough for typical From/Subject headers on a
// desk-gadget widget, not a MIME-correct parser).
String MailClient::extractHeaderField(const String& block, const char* field) {
    String needle = String(field) + ":";
    int idx = block.indexOf(needle);
    if (idx < 0) {
        // try lowercase variant too, servers are inconsistent about casing
        String lower = block;
        lower.toLowerCase();
        String needleLower = needle;
        needleLower.toLowerCase();
        idx = lower.indexOf(needleLower);
        if (idx < 0) return "";
    }
    int lineEnd = block.indexOf('\n', idx);
    if (lineEnd < 0) lineEnd = block.length();
    String val = block.substring(idx + needle.length(), lineEnd);
    val.trim();
    return val;
}

// Parses "* SEARCH 3 7 12 19 ..." → array of UIDs (ascending, oldest first).
void MailClient::parseUnseenUids(const String& searchResponse, int* uids, int maxUids, int& count) {
    count = 0;
    int idx = searchResponse.indexOf("* SEARCH");
    if (idx < 0) return;
    int lineEnd = searchResponse.indexOf('\n', idx);
    if (lineEnd < 0) lineEnd = searchResponse.length();
    String nums = searchResponse.substring(idx + 8, lineEnd);
    nums.trim();

    int pos = 0;
    while (pos < (int)nums.length() && count < maxUids) {
        while (pos < (int)nums.length() && nums[pos] == ' ') pos++;
        int start = pos;
        while (pos < (int)nums.length() && isDigit(nums[pos])) pos++;
        if (pos > start) uids[count++] = nums.substring(start, pos).toInt();
        else break;
    }
}

bool MailClient::poll(const char* host, uint16_t port, const char* user, const char* pass,
                       uint16_t& unread_count, Item* items, uint8_t max_items, uint8_t& item_count,
                       uint32_t timeout_ms) {
    unread_count = 0;
    item_count   = 0;

    _client.setInsecure();
    if (!_client.connect(host, port)) {
        log_w("MailClient: TCP/TLS connect to %s:%u failed", host, (unsigned)port);
        return false;
    }
    _client.setTimeout(timeout_ms / 1000); // socket exists now — safe to set

    // Server greeting ("* OK ...") — read and discard.
    String greeting;
    if (!readLine(greeting, timeout_ms) || !greeting.startsWith("*")) {
        log_w("MailClient: no/bad greeting: \"%s\"", greeting.c_str());
        _client.stop();
        return false;
    }

    // LOGIN
    String resp;
    String loginCmd = "LOGIN \"" + String(user) + "\" \"" + String(pass) + "\"";
    if (!command(loginCmd, resp, timeout_ms)) {
        log_w("MailClient: LOGIN failed, server said: \"%s\"", resp.c_str());
        _client.stop();
        return false;
    }

    // STATUS INBOX (UNSEEN) — works without SELECTing first.
    if (command("STATUS INBOX (UNSEEN)", resp, timeout_ms)) {
        int idx = resp.indexOf("UNSEEN");
        if (idx >= 0) {
            int p = idx + 6;
            while (p < (int)resp.length() && !isDigit(resp[p])) p++;
            int start = p;
            while (p < (int)resp.length() && isDigit(resp[p])) p++;
            if (p > start) unread_count = resp.substring(start, p).toInt();
        }
    }

    if (unread_count > 0 && max_items > 0) {
        // SELECT INBOX (needed for UID SEARCH / UID FETCH)
        if (command("SELECT INBOX", resp, timeout_ms)) {
            // UID SEARCH UNSEEN
            if (command("UID SEARCH UNSEEN", resp, timeout_ms)) {
                int uids[32];
                int uidCount = 0;
                parseUnseenUids(resp, uids, 32, uidCount);

                // Fetch the most recent N (UIDs come back ascending, so walk
                // from the end).
                int fetched = 0;
                for (int i = uidCount - 1; i >= 0 && fetched < max_items; i--) {
                    String fetchCmd = "UID FETCH " + String(uids[i]) +
                                       " (BODY.PEEK[HEADER.FIELDS (FROM SUBJECT)])";
                    String fetchResp;
                    if (!command(fetchCmd, fetchResp, timeout_ms)) continue;

                    String from    = extractHeaderField(fetchResp, "From");
                    String subject = extractHeaderField(fetchResp, "Subject");
                    if (from.length() == 0 && subject.length() == 0) continue;

                    items[fetched].from    = from.length()    ? from    : "(unknown)";
                    items[fetched].subject = subject.length() ? subject : "(no subject)";
                    fetched++;
                }
                item_count = fetched;
            }
        }
    }

    command("LOGOUT", resp, 3000); // best-effort, ignore result
    _client.stop();
    return true;
}
