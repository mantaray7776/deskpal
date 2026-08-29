#pragma once
#include "NavState.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Suggestion.h — public API for the assistant's suggestion engine
//  (implemented in src/SuggestionTask.cpp). Any task can offer a suggestion;
//  the engine handles per-type cooldowns so callers don't need to.
// ─────────────────────────────────────────────────────────────────────────────

// Set up defaults (auto_focus_enabled etc). Call once from setup().
bool suggestion_init();

// Request that a suggestion be shown. Ignored if a suggestion is already
// active, or if this type was shown within the last SUGGEST_COOLDOWN_SEC.
// ttl_sec: how long it stays on screen before auto-dismissing (0 = never).
void suggestion_offer(SuggestionType type, const char* text, uint32_t ttl_sec = SUGGEST_EXPIRE_SEC);

// User accepted the active suggestion (web button or the BOOT-button tap).
// Triggers the associated action (e.g. start/stop a session).
void suggestion_accept();

// User dismissed the active suggestion without acting on it.
void suggestion_dismiss();

void SuggestionTask(void* pvParams);
