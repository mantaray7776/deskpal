#include "WakeWord.h"
#include "deskmon_mic_training_inferencing.h"
#include <cmath>

// signal_t.get_data is a plain function pointer, not a member function, and
// this project only ever has one WakeWord instance — same pattern as the
// static FFT buffers used elsewhere in this codebase.
static WakeWord* s_self = nullptr;

static int get_data_trampoline(size_t offset, size_t length, float* out_ptr) {
    return s_self->getData(offset, length, out_ptr);
}

void WakeWord::begin() {
    s_self = this;
    ring_ = new int16_t[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
    memset(ring_, 0, EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t));

    log_i("WakeWord: model window=%dms labels=%d arena=%dB",
          EI_CLASSIFIER_RAW_SAMPLE_COUNT / (EI_CLASSIFIER_FREQUENCY / 1000),
          EI_CLASSIFIER_LABEL_COUNT, EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE);

    if (EI_CLASSIFIER_FREQUENCY != 16000) {
        log_w("WakeWord: model trained at %dHz, mic feeds %dHz elsewhere — verify Config.h MIC_SAMPLE_RATE matches",
              EI_CLASSIFIER_FREQUENCY, 16000);
    }
}

int WakeWord::getData(size_t offset, size_t length, float* out_ptr) {
    size_t cap = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    for (size_t i = 0; i < length; i++) {
        size_t idx = (writePos_ + offset + i) % cap; // writePos_ = oldest sample once full
        out_ptr[i] = (float)ring_[idx];
    }
    return 0;
}

void WakeWord::feed(const int32_t* samples, size_t count, int shift_bits) {
    if (!ring_ || count == 0) return;
    size_t cap = EI_CLASSIFIER_RAW_SAMPLE_COUNT;

    // 1) push into the rolling 1s window, scaled to roughly match the
    //    amplitude range the model's training clips were captured at. If
    //    detection is unreliable, this *8 gain is the first thing to check —
    //    compare a live Serial-printed sample against what Edge Impulse
    //    Studio shows for one of your recorded "hey deskmon" clips.
    for (size_t i = 0; i < count; i++) {
        int32_t s = (samples[i] >> shift_bits) * 8;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        ring_[writePos_] = (int16_t)s;
        writePos_ = (writePos_ + 1) % cap;
        if (filled_ < cap) filled_++;
    }

    // 2) batch-level VAD — same dB formula MicTask's own noise_db calc uses.
    int64_t sumSq = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t s = samples[i] >> shift_bits;
        sumSq += (int64_t)s * s;
    }
    float rms = sqrtf((float)sumSq / (float)count);
    float db = 20.0f * log10f(rms + 1.0f);
    noiseDb_ = noiseDb_ * 0.8f + db * 0.2f;

    if (filled_ < cap) return; // wait for the first full window before ever inferring

    uint32_t now = millis();
    switch (state_) {
        case IDLE:
            if (db > vadOnDb_) {
                state_ = ARMED;
                armedAtMs_ = now;
            }
            break;
        case ARMED:
            if (now - armedAtMs_ >= ARM_FILL_MS) {
                runInference();
                state_ = COOLDOWN;
                cooldownUntilMs_ = now + COOLDOWN_MS;
            }
            break;
        case COOLDOWN:
            if (now >= cooldownUntilMs_) state_ = IDLE;
            break;
    }
}

void WakeWord::runInference() {
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &get_data_trampoline;

    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);
    if (r != EI_IMPULSE_OK) {
        log_e("WakeWord: run_classifier failed (%d)", (int)r);
        return;
    }

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT && ix < 2; ix++) {
        lastConf_[ix] = result.classification[ix].value;
    }

    log_i("WakeWord: hey_deskmon=%.2f hey_deskmon_stop=%.2f (dsp %dms cls %dms)",
          lastConf_[0], lastConf_[1], result.timing.dsp, result.timing.classification);

    Command best = (lastConf_[0] >= lastConf_[1]) ? CMD_START : CMD_STOP;
    float bestConf = (best == CMD_START) ? lastConf_[0] : lastConf_[1];

    if (bestConf >= confThreshold_ && callback_) {
        callback_(best);
    }
}
