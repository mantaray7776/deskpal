#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  WakeWord — thin wrapper around the trained Edge Impulse model
//  (deskmon_mic_training_inferencing) that decides WHEN to run it.
//
//  The model has exactly 2 classes ("hey_deskmon" / "hey_deskmon_stop") and
//  NO background/noise/unknown class — every 1s window fed to it gets forced
//  into one of those two labels, even silence. So we never run the model on
//  every window; a simple energy-based VAD gate only lets inference run
//  right after real speech-level energy is detected, then enforces a cooldown
//  so one utterance can't multi-trigger. This does NOT fix the model
//  confusing "hey deskmon" for other speech near the mic — only retraining
//  with a background/unknown class fixes that. See project notes.
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <functional>

class WakeWord {
public:
    enum Command : int8_t { CMD_NONE = -1, CMD_START = 0, CMD_STOP = 1 };
    using CommandCallback = std::function<void(Command)>;

    void begin();

    // Feed raw I2S samples straight from your existing i2s_read() buffer.
    // shift_bits should match what MicTask already uses (currently 14).
    void feed(const int32_t* samples, size_t count, int shift_bits = 14);

    // Smoothed ambient dB, same formula MicTask's own noise_db calc uses.
    float getNoiseDb() const { return noiseDb_; }

    void onCommand(CommandCallback cb) { callback_ = cb; }

    // Tuning knobs — see README for how to pick these on real hardware.
    void setVadThresholdDb(float onDb, float offDb) { vadOnDb_ = onDb; vadOffDb_ = offDb; }
    void setConfidenceThreshold(float t) { confThreshold_ = t; }

    // Last classifier output, exposed for the `status` serial command.
    float lastConfidenceStart() const { return lastConf_[0]; }
    float lastConfidenceStop()  const { return lastConf_[1]; }

    // Public so the C-style get_data callback (required by the EI signal_t
    // API) can reach into the ring buffer without a global/friend hack.
    int getData(size_t offset, size_t length, float* out_ptr);

private:
    void runInference();

    CommandCallback callback_;
    float noiseDb_        = 0.0f;
    float vadOnDb_         = 78.0f; // tune above your room's idle noise_db
    float vadOffDb_        = 72.0f; // unused directly (batch-level VAD, no hangover state) — kept for API symmetry
    float confThreshold_  = 0.80f;  // model must be at least this confident

    enum State { IDLE, ARMED, COOLDOWN } state_ = IDLE;
    uint32_t armedAtMs_       = 0;
    uint32_t cooldownUntilMs_ = 0;
    // Wait this long after speech onset before inferring, so the 1s window
    // (which ends "now") has a better chance of actually containing the
    // phrase instead of just its first syllable. Tune against real timing.
    static constexpr uint32_t ARM_FILL_MS = 500;
    // Ignore further triggers for this long after one inference runs, so a
    // single utterance can't fire the callback repeatedly.
    static constexpr uint32_t COOLDOWN_MS = 1500;

    float lastConf_[2] = {0.0f, 0.0f};

    // Sized directly off the model's own window length so this stays correct
    // if you ever retrain with a different window.
    int16_t* ring_    = nullptr; // allocated in begin() at EI_CLASSIFIER_RAW_SAMPLE_COUNT
    size_t   writePos_ = 0;
    size_t   filled_   = 0;
};
