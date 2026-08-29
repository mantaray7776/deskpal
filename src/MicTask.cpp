// ─────────────────────────────────────────────────────────────────────────────
//  MicTask.cpp  —  INMP441 MEMS microphone via I2S
//  Wiring: VDD→3.3V  GND→GND  SCK→GPIO38  WS→GPIO39  SD→GPIO40  L/R→GND
//
//  Also drives WakeWord (Edge Impulse model): "hey deskmon" starts the
//  session, "hey deskmon stop" ends it — same effect as pressing the
//  encoder button or hitting start/stop from the web UI.
//
//  Serial commands (type into the PlatformIO monitor, 115200 baud):
//    status              — print noiseDb, last model confidences, thresholds
//    threshold <0-1>     — set confidence threshold (default 0.80)
//    vad <onDb> <offDb>  — set VAD onset/offset dB (default 78 72)
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <driver/i2s.h>
#include <cmath>
#include "Config.h"
#include "NavState.h"
#include "WakeWord.h"

static const i2s_port_t MIC_I2S_PORT = I2S_NUM_1;
static WakeWord s_voice;

static void on_voice_command(WakeWord::Command cmd) {
    if (cmd == WakeWord::CMD_START) {
        WITH_STATE([&]{
            g_state.session.active      = true;
            g_state.session.elapsed_sec = 0;
        });
        nav_log_event(EventType::SESSION_START);
        log_i("WakeWord: 'hey deskmon' -> session started");
    } else if (cmd == WakeWord::CMD_STOP) {
        WITH_STATE([&]{ g_state.session.active = false; });
        nav_log_event(EventType::SESSION_END);
        log_i("WakeWord: 'hey deskmon stop' -> session stopped");
    }
}

// Small line-based command parser over Serial for tuning/status.
static void poll_serial_commands() {
    static String line;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            line.trim();
            if (line.length()) {
                if (line == "status") {
                    Serial.printf("WakeWord: noiseDb=%.1f  hey_deskmon=%.2f  hey_deskmon_stop=%.2f\n",
                                  s_voice.getNoiseDb(),
                                  s_voice.lastConfidenceStart(),
                                  s_voice.lastConfidenceStop());
                } else if (line.startsWith("threshold ")) {
                    float t = line.substring(10).toFloat();
                    s_voice.setConfidenceThreshold(t);
                    Serial.printf("WakeWord: confidence threshold set to %.2f\n", t);
                } else if (line.startsWith("vad ")) {
                    int sp = line.indexOf(' ', 4);
                    if (sp > 0) {
                        float onDb  = line.substring(4, sp).toFloat();
                        float offDb = line.substring(sp + 1).toFloat();
                        s_voice.setVadThresholdDb(onDb, offDb);
                        Serial.printf("WakeWord: VAD onDb=%.1f offDb=%.1f\n", onDb, offDb);
                    }
                }
            }
            line = "";
        } else {
            line += c;
        }
    }
}

bool mic_init() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = MIC_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = MIC_BUF_LEN,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };
    esp_err_t err = i2s_driver_install(MIC_I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) { log_e("MicTask: i2s_driver_install failed (%d)", err); return false; }

    i2s_pin_config_t pins = {
        .bck_io_num   = PIN_MIC_SCK,
        .ws_io_num    = PIN_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = PIN_MIC_SD,
    };
    err = i2s_set_pin(MIC_I2S_PORT, &pins);
    if (err != ESP_OK) { log_e("MicTask: i2s_set_pin failed (%d)", err); return false; }

    s_voice.begin();
    s_voice.onCommand(on_voice_command);

    log_i("MicTask: INMP441 OK on SCK=%d WS=%d SD=%d", PIN_MIC_SCK, PIN_MIC_WS, PIN_MIC_SD);
    return true;
}

void MicTask(void* pvParams) {
    // Reads I2S back-to-back (no outer task-period gap) so "hey deskmon"
    // isn't chopped up by silent gaps between reads. i2s_read() blocks on
    // the DMA queue, so this doesn't spin the CPU.
    static int32_t buf[MIC_BUF_LEN];
    uint32_t last_publish_ms = 0;

    while (true) {
        size_t bytes_read = 0;
        i2s_read(MIC_I2S_PORT, buf, sizeof(buf), &bytes_read, pdMS_TO_TICKS(200));

        int samples = (int)(bytes_read / sizeof(int32_t));
        if (samples > 0) {
            int64_t sum = 0;
            for (int i = 0; i < samples; i++) {
                int32_t s = buf[i] >> 14;
                sum += (int64_t)s * s;
            }
            float rms = sqrtf((float)sum / samples);
            float db = 20.0f * log10f(rms + 1.0f);

            // Feed the same shifted samples into the wake-word detector.
            s_voice.feed(buf, samples, 14);

            // Publish noise_db to shared state at ~5Hz instead of every
            // ~32ms read — DistractTask only samples it at 10Hz anyway.
            uint32_t now = millis();
            if (now - last_publish_ms >= 200) {
                last_publish_ms = now;
                WITH_STATE([&]{ g_state.motion.noise_db = db; });
            }
        }

        // Deliberately OUTSIDE the samples>0 check above: if the mic ever
        // stalls (I2S read timing out with 0 bytes — bad wiring, DMA hiccup,
        // etc.) serial commands must still work so you can debug it via
        // `status` instead of a dead terminal with no way to tell why.
        poll_serial_commands();
    }
}