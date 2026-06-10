/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file audio_cls_srv.c
 * @brief Audio classification service using ESP-DL
 *
 * Implements mel-spectrogram preprocessing (16 kHz, 128 mel bands,
 * 25 ms window, 10 ms hop) followed by ESP-DL neural network inference
 * and debounce (5 consecutive frames at confidence >= 0.85).
 *
 * NOTE: This file uses the ESP-DL C++ API.  To compile correctly, add
 * the following to CMakeLists.txt:
 *
 *   set_source_files_properties("service/audio_cls_srv.c"
 *                               PROPERTIES LANGUAGE CXX)
 *
 * or rename to .cpp and update CMakeLists.txt accordingly.
 */

/* ================================================================== */
/*  C++-only headers (ESP-DL)                                         */
/* ================================================================== */
#ifdef __cplusplus
#include "dl_model_base.hpp"
#include "fbs_model.hpp"
#endif /* __cplusplus */

/* ================================================================== */
/*  C headers (shared)                                                 */
/* ================================================================== */
#include "service/audio_classify_service.h"
#include "service/app_config_service.h"
#include "runtime/task_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

/* ================================================================== */
/*  Constants                                                          */
/* ================================================================== */
#define SAMPLE_RATE         AUDIO_SAMPLE_RATE_HZ
#define N_FFT               512         /* FFT size (next pow2>400)   */
#define N_FFT_BINS          (N_FFT / 2 + 1)  /* 257 bins             */
#define WIN_LENGTH          512         /* librosa default win_length */
#define HOP_LENGTH          160         /* 10 ms hop  @ 16 kHz        */
#define N_MELS              128         /* Mel filterbank bands       */
#define N_FRAMES            100         /* Time frames in output      */
#define MEL_LOW_HZ          20.0f       /* Match training preprocess  */
#define MEL_HIGH_HZ         8000.0f     /* Highest frequency          */
#define AUDIO_CLASS_ALARM   0
#define AUDIO_CLASS_CAR_HORN 1
#define AUDIO_CLASS_KNOCK   2
#define AUDIO_CLASS_CLAPPING 3
#define AUDIO_CLASS_DOG_BARK 4
#define AUDIO_CLASS_FOOTSTEPS 5
#define AUDIO_CLASS_GLASS_BREAK 6
#define AUDIO_CLASS_DOORBELL 7
#define AUDIO_CLASS_CRYING_BABY 8
#define AUDIO_CLASS_ENGINE  9
#define AUDIO_CLASS_TRAFFIC 10
#define AUDIO_CLASS_BACKGROUND 11
#define KNOCK_CONF_THRESH   0.65f
#define KNOCK_DEBOUNCE_FRAMES 1
#define ACTIVE_RMS_THRESH   8.0f
#define ACTIVE_PEAK_THRESH  38
#define ACTIVE_HOLD_FRAMES  4

/* Training used 1 second clips with librosa center=True. */
#define TOTAL_NEEDED ((size_t)SAMPLE_RATE)

/* Ring-buffer size: enough for one full window + one new frame */
#define RING_SIZE   (TOTAL_NEEDED + AUDIO_FRAME_SAMPLES)  /* 17600 */

/* Small epsilon to avoid log(0) */
#define LOG_EPSILON 1e-10f

static const char *TAG = "AUDIO_CLS_SRV";

/* ================================================================== */
/*  Static state                                                       */
/* ================================================================== */

/* ESP-DL model handle (C++ object) */
#ifdef __cplusplus
static dl::Model *s_model = NULL;
#endif

/* Threshold (default 0.85 from header) */
static float s_threshold = CONFIDENCE_THRESH;

/* Audio ring buffer (linear, periodically shifted) */
static float s_ring[RING_SIZE];
static size_t s_ring_count = 0;         /* valid samples in buffer    */

/* Debounce state */
static int s_debounce_count  = 0;
static int s_debounce_class  = -1;
static int s_active_hold_frames = 0;
static uint32_t s_gate_skip_count = 0;
static int64_t s_last_trigger_ms[MAX_CLASSES] = {0};

static const char *s_class_names[MAX_CLASSES] = {
    [AUDIO_CLASS_ALARM] = "alarm",
    [AUDIO_CLASS_CAR_HORN] = "car_horn",
    [AUDIO_CLASS_KNOCK] = "knocking",
    [AUDIO_CLASS_CLAPPING] = "clapping",
    [AUDIO_CLASS_DOG_BARK] = "dog_bark",
    [AUDIO_CLASS_FOOTSTEPS] = "footsteps",
    [AUDIO_CLASS_GLASS_BREAK] = "glass_break",
    [AUDIO_CLASS_DOORBELL] = "doorbell",
    [AUDIO_CLASS_CRYING_BABY] = "crying_baby",
    [AUDIO_CLASS_ENGINE] = "engine",
    [AUDIO_CLASS_TRAFFIC] = "traffic",
    [AUDIO_CLASS_BACKGROUND] = "background",
};

static float s_class_thresholds[MAX_CLASSES] = {
    [AUDIO_CLASS_ALARM] = 0.60f,
    [AUDIO_CLASS_CAR_HORN] = 0.60f,
    [AUDIO_CLASS_KNOCK] = KNOCK_CONF_THRESH,
    [AUDIO_CLASS_CLAPPING] = 0.72f,
    [AUDIO_CLASS_DOG_BARK] = 0.60f,
    [AUDIO_CLASS_FOOTSTEPS] = 0.60f,
    [AUDIO_CLASS_GLASS_BREAK] = 0.86f,
    [AUDIO_CLASS_DOORBELL] = 0.70f,
    [AUDIO_CLASS_CRYING_BABY] = 0.60f,
    [AUDIO_CLASS_ENGINE] = 0.82f,
    [AUDIO_CLASS_TRAFFIC] = 0.76f,
    [AUDIO_CLASS_BACKGROUND] = 1.01f,
};

static float s_class_margins[MAX_CLASSES] = {
    [AUDIO_CLASS_ALARM] = 0.06f,
    [AUDIO_CLASS_CAR_HORN] = 0.08f,
    [AUDIO_CLASS_KNOCK] = 0.05f,
    [AUDIO_CLASS_CLAPPING] = 0.14f,
    [AUDIO_CLASS_DOG_BARK] = 0.08f,
    [AUDIO_CLASS_FOOTSTEPS] = 0.08f,
    [AUDIO_CLASS_GLASS_BREAK] = 0.30f,
    [AUDIO_CLASS_DOORBELL] = 0.12f,
    [AUDIO_CLASS_CRYING_BABY] = 0.08f,
    [AUDIO_CLASS_ENGINE] = 0.25f,
    [AUDIO_CLASS_TRAFFIC] = 0.15f,
    [AUDIO_CLASS_BACKGROUND] = 1.00f,
};

static const char *_class_name(int class_id)
{
    if (class_id >= 0 && class_id < MAX_CLASSES && s_class_names[class_id]) {
        return s_class_names[class_id];
    }
    return "unknown";
}

static float _class_threshold(int class_id)
{
    if (class_id >= 0 && class_id < MAX_CLASSES) {
        return s_class_thresholds[class_id];
    }
    return s_threshold;
}

static float _class_margin(int class_id)
{
    if (class_id >= 0 && class_id < MAX_CLASSES) {
        return s_class_margins[class_id];
    }
    return 0.08f;
}

static int _class_debounce_frames(int class_id)
{
    switch (class_id) {
    case AUDIO_CLASS_KNOCK:
        return 1;
    case AUDIO_CLASS_CLAPPING:
    case AUDIO_CLASS_GLASS_BREAK:
    case AUDIO_CLASS_DOORBELL:
    case AUDIO_CLASS_CAR_HORN:
    case AUDIO_CLASS_DOG_BARK:
    case AUDIO_CLASS_FOOTSTEPS:
    case AUDIO_CLASS_CRYING_BABY:
        return 2;
    case AUDIO_CLASS_ALARM:
        return 3;
    case AUDIO_CLASS_ENGINE:
    case AUDIO_CLASS_TRAFFIC:
        return 4;
    default:
        return 3;
    }
}

static int64_t _class_cooldown_ms(int class_id)
{
    switch (class_id) {
    case AUDIO_CLASS_GLASS_BREAK:
        return 3600;
    case AUDIO_CLASS_DOORBELL:
        return 1800;
    case AUDIO_CLASS_CLAPPING:
        return 1800;
    case AUDIO_CLASS_KNOCK:
        return 2400;
    case AUDIO_CLASS_ALARM:
    case AUDIO_CLASS_ENGINE:
    case AUDIO_CLASS_TRAFFIC:
        return 2600;
    default:
        return 1700;
    }
}

/* Pre-computed tables */
static float  s_hann[WIN_LENGTH];                       /* Hann window          */
static float  s_mel_w[N_MELS][N_FFT_BINS];             /* mel filter weights   */
static float  s_twiddle_real[N_FFT];                   /* FFT twiddle (cos)    */
static float  s_twiddle_imag[N_FFT];                   /* FFT twiddle (sin)    */
static uint16_t s_bitrev[N_FFT];                       /* FFT bit-reversal     */
static bool   s_tables_ready = false;
static float  s_mel_spec[N_FRAMES][N_MELS];           /* reused inference input */
static bool   s_input_tensor_logged = false;
static uint32_t s_inference_count = 0;
static uint32_t s_process_count = 0;
static uint32_t s_process_errors = 0;
static uint32_t s_trigger_count = 0;
static uint32_t s_last_process_ms = 0;
static uint32_t s_min_process_ms = UINT32_MAX;
static uint32_t s_max_process_ms = 0;
static uint64_t s_process_time_sum_ms = 0;

/* ================================================================== */
/*  Pre-computation helpers                                            */
/* ================================================================== */

static void _init_hann(void)
{
    for (int i = 0; i < WIN_LENGTH; i++) {
        s_hann[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (WIN_LENGTH - 1)));
    }
}

static float _hz_to_mel_slaney(float hz)
{
    const float f_sp = 200.0f / 3.0f;
    float mel = hz / f_sp;

    if (hz >= 1000.0f) {
        const float min_log_hz = 1000.0f;
        const float min_log_mel = min_log_hz / f_sp;
        const float logstep = logf(6.4f) / 27.0f;
        mel = min_log_mel + logf(hz / min_log_hz) / logstep;
    }

    return mel;
}

static float _mel_to_hz_slaney(float mel)
{
    const float f_sp = 200.0f / 3.0f;
    float hz = mel * f_sp;

    if (mel >= 15.0f) {
        const float min_log_hz = 1000.0f;
        const float min_log_mel = 15.0f;
        const float logstep = logf(6.4f) / 27.0f;
        hz = min_log_hz * expf(logstep * (mel - min_log_mel));
    }

    return hz;
}

static void _init_mel_filterbank(void)
{
    /* Match librosa's default mel filterbank: htk=False, norm='slaney'. */
    const float mel_low  = _hz_to_mel_slaney(MEL_LOW_HZ);
    const float mel_high = _hz_to_mel_slaney(MEL_HIGH_HZ);
    const float mel_step = (mel_high - mel_low) / (N_MELS + 1);

    /* Hz per FFT bin */
    const float hz_per_bin = (float)SAMPLE_RATE / N_FFT;

    for (int m = 0; m < N_MELS; m++) {
        /* Mel-spaced centre, left, right */
        const float mc = mel_low + (m + 1) * mel_step;
        const float ml = mel_low +  m      * mel_step;
        const float mr = mel_low + (m + 2) * mel_step;

        /* Convert back to Hz */
        const float hz_c = _mel_to_hz_slaney(mc);
        const float hz_l = _mel_to_hz_slaney(ml);
        const float hz_r = _mel_to_hz_slaney(mr);

        /* Map to FFT bin indices (floating-point) */
        const float bin_c = hz_c / hz_per_bin;
        const float bin_l = hz_l / hz_per_bin;
        const float bin_r = hz_r / hz_per_bin;

        const int start_bin = (int)(bin_l + 0.5f);
        const int stop_bin  = (int)(bin_r + 0.5f);

        for (int k = 0; k < N_FFT_BINS; k++) {
            if (k < start_bin || k > stop_bin) {
                s_mel_w[m][k] = 0.0f;
            } else {
                float val;
                if (k <= bin_c) {
                    if (bin_c - bin_l > 0.0f) {
                        val = (k - bin_l) / (bin_c - bin_l);
                    } else {
                        val = 1.0f;
                    }
                } else {
                    if (bin_r - bin_c > 0.0f) {
                        val = (bin_r - k) / (bin_r - bin_c);
                    } else {
                        val = 1.0f;
                    }
                }
                const float enorm = 2.0f / (hz_r - hz_l);
                s_mel_w[m][k] = fmaxf(0.0f, val) * enorm;
            }
        }
    }
}

static void _init_fft_tables(void)
{
    /* Twiddle factors for N_FFT-point FFT */
    for (int i = 0; i < N_FFT; i++) {
        s_twiddle_real[i] = cosf(2.0f * (float)M_PI * i / N_FFT);
        s_twiddle_imag[i] = -sinf(2.0f * (float)M_PI * i / N_FFT);
    }

    /* Bit-reversal permutation table */
    int bits = 0;
    int tmp = N_FFT;
    while (tmp > 1) { tmp >>= 1; bits++; }

    for (int i = 0; i < N_FFT; i++) {
        int rev = 0;
        int x = i;
        for (int b = 0; b < bits; b++) {
            rev = (rev << 1) | (x & 1);
            x >>= 1;
        }
        s_bitrev[i] = (uint16_t)rev;
    }
}

static void _ensure_tables(void)
{
    if (s_tables_ready) return;
    _init_hann();
    _init_mel_filterbank();
    _init_fft_tables();
    s_tables_ready = true;
    ESP_LOGD(TAG, "Pre-computation tables ready (mel=%dx%d, fft=%d)",
             N_MELS, N_FFT_BINS, N_FFT);
}

/* ================================================================== */
/*  512-point Cooley-Tukey FFT (in-place, radix-2)                    */
/* ================================================================== */

static void _fft_512(float *real, float *imag)
{
    /* Bit-reversal permutation */
    for (int i = 0; i < N_FFT; i++) {
        int j = s_bitrev[i];
        if (j > i) {
            float tmp = real[i]; real[i] = real[j]; real[j] = tmp;
            tmp = imag[i]; imag[i] = imag[j]; imag[j] = tmp;
        }
    }

    /* Iterative Cooley-Tukey */
    for (int len = 2; len <= N_FFT; len <<= 1) {
        int half = len >> 1;
        float step_ang = 2.0f * (float)M_PI / len;
        float w_base_real = cosf(step_ang);
        float w_base_imag = -sinf(step_ang);

        for (int i = 0; i < N_FFT; i += len) {
            float w_real = 1.0f;
            float w_imag = 0.0f;
            for (int j = 0; j < half; j++) {
                int i0 = i + j;
                int i1 = i + j + half;
                float t_real = w_real * real[i1] - w_imag * imag[i1];
                float t_imag = w_real * imag[i1] + w_imag * real[i1];
                real[i1] = real[i0] - t_real;
                imag[i1] = imag[i0] - t_imag;
                real[i0] += t_real;
                imag[i0] += t_imag;
                /* Rotate twiddle */
                float wr = w_real * w_base_real - w_imag * w_base_imag;
                w_imag = w_real * w_base_imag + w_imag * w_base_real;
                w_real = wr;
            }
        }
    }
}

/* ================================================================== */
/*  Mel-spectrogram computation                                       */
/* ================================================================== */

/**
 * @brief Compute mel-spectrogram from an audio buffer.
 *
 * @param[in]  audio      Input audio samples (float, length >= TOTAL_NEEDED)
 * @param[in]  offset     Starting index within `audio` for the window
 * @param[out] mel_out    Output buffer, shape [N_FRAMES][N_MELS]
 *
 * The function extracts N_FRAMES of WIN_LENGTH samples with HOP_LENGTH
 * stride, zero-pads to N_FFT, applies Hann window, computes power
 * spectrum, applies the mel filterbank, and takes log.
 */
static void _compute_mel_spectrogram(const float *audio, size_t offset,
                                     float mel_out[N_FRAMES][N_MELS])
{
    float fft_real[N_FFT];
    float fft_imag[N_FFT];
    float power[N_FFT_BINS];
    float max_energy = LOG_EPSILON;

    for (int t = 0; t < N_FRAMES; t++) {
        /* librosa.feature.melspectrogram defaults to center=True, so each
         * frame is centered at t * hop and padded with zeros at the edges. */
        memset(fft_real, 0, sizeof(fft_real));
        memset(fft_imag, 0, sizeof(fft_imag));
        for (int i = 0; i < WIN_LENGTH; i++) {
            int sample_idx = t * HOP_LENGTH + i - (N_FFT / 2);
            if (sample_idx >= 0 && sample_idx < (int)TOTAL_NEEDED) {
                fft_real[i] = audio[offset + (size_t)sample_idx] * s_hann[i];
            }
        }

        /* --- FFT --- */
        _fft_512(fft_real, fft_imag);

        /* --- Power spectrum (magnitude squared) --- */
        for (int k = 0; k < N_FFT_BINS; k++) {
            power[k] = fft_real[k] * fft_real[k] + fft_imag[k] * fft_imag[k];
        }

        /* --- Mel filterbank + log --- */
        for (int m = 0; m < N_MELS; m++) {
            float energy = 0.0f;
            for (int k = 0; k < N_FFT_BINS; k++) {
                energy += power[k] * s_mel_w[m][k];
            }
            energy = fmaxf(energy, LOG_EPSILON);
            mel_out[t][m] = energy;
            if (energy > max_energy) {
                max_energy = energy;
            }
        }
    }

    /* Match librosa.power_to_db(mel, ref=np.max), with the default
     * top_db=80. Values are in [-80, 0] dB. */
    for (int t = 0; t < N_FRAMES; t++) {
        for (int m = 0; m < N_MELS; m++) {
            float db = 10.0f * log10f(mel_out[t][m] / max_energy);
            if (db < -80.0f) {
                db = -80.0f;
            }
            mel_out[t][m] = db;
        }
    }
}

/* ================================================================== */
/*  Softmax                                                            */
/* ================================================================== */

static void _softmax(const float *input, float *output, int n)
{
    float max_val = input[0];
    for (int i = 1; i < n; i++) {
        if (input[i] > max_val) max_val = input[i];
    }
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        output[i] = expf(input[i] - max_val);
        sum += output[i];
    }
    if (sum > 0.0f) {
        for (int i = 0; i < n; i++) {
            output[i] /= sum;
        }
    }
}

/* ================================================================== */
/*  Audio accumulation                                                 */
/* ================================================================== */

/**
 * @brief Append new samples to the ring buffer.
 *
 * When the buffer has >= TOTAL_NEEDED samples, the oldest
 * AUDIO_FRAME_SAMPLES are discarded to maintain a sliding window
 * of TOTAL_NEEDED samples.
 */
static void _audio_accumulate(const int16_t *samples, size_t n)
{
    if (n == 0) return;

    if (n >= TOTAL_NEEDED) {
        const int16_t *window = samples + (n - TOTAL_NEEDED);
        for (size_t i = 0; i < TOTAL_NEEDED; i++) {
            s_ring[i] = (float)window[i] / 32768.0f;
        }
        s_ring_count = TOTAL_NEEDED;
        return;
    }

    /* Convert int16_t to float and append */
    size_t avail = RING_SIZE - s_ring_count;
    size_t to_copy = (n < avail) ? n : avail;

    for (size_t i = 0; i < to_copy; i++) {
        s_ring[s_ring_count + i] = (float)samples[i] / 32768.0f;
    }
    s_ring_count += to_copy;

    if (to_copy < n) {
        ESP_LOGW(TAG, "Ring buffer overflow, dropping %zu samples", n - to_copy);
    }

    /* Maintain sliding window: if we exceed TOTAL_NEEDED + margin,
     * discard the oldest chunk so the window stays dense. */
    if (s_ring_count > TOTAL_NEEDED + AUDIO_FRAME_SAMPLES) {
        size_t discard = s_ring_count - TOTAL_NEEDED;
        memmove(s_ring, s_ring + discard, (s_ring_count - discard) * sizeof(float));
        s_ring_count = TOTAL_NEEDED;
    }
}

static bool _audio_frame_is_active(const int16_t *samples, size_t n,
                                   float *rms_out, int *peak_out)
{
    uint64_t square_sum = 0;
    int peak = 0;

    for (size_t i = 0; i < n; i++) {
        int sample = samples[i];
        int abs_sample = sample < 0 ? -sample : sample;
        if (abs_sample > peak) {
            peak = abs_sample;
        }
        square_sum += (uint64_t)((int64_t)sample * (int64_t)sample);
    }

    float rms = sqrtf((float)((double)square_sum / (double)n));
    if (rms_out) {
        *rms_out = rms;
    }
    if (peak_out) {
        *peak_out = peak;
    }

    return rms >= ACTIVE_RMS_THRESH || peak >= ACTIVE_PEAK_THRESH;
}

static void _slide_audio_window(void)
{
    if (s_ring_count > AUDIO_FRAME_SAMPLES) {
        size_t keep = s_ring_count - AUDIO_FRAME_SAMPLES;
        memmove(s_ring, s_ring + AUDIO_FRAME_SAMPLES, keep * sizeof(float));
        s_ring_count = keep;
    } else {
        s_ring_count = 0;
    }
}

/* ================================================================== */
/*  ESP-DL inference helper (C++ section)                              */
/* ================================================================== */

#ifdef __cplusplus

static esp_err_t _run_inference(float mel_spec[N_FRAMES][N_MELS],
                                float *output, size_t *output_size)
{
    if (!s_model) {
        ESP_LOGE(TAG, "Model not loaded");
        return ESP_ERR_INVALID_STATE;
    }

    dl::TensorBase *input = s_model->get_input();
    if (!input) {
        ESP_LOGE(TAG, "Failed to get input tensor");
        return ESP_FAIL;
    }

    if (!s_input_tensor_logged) {
        if (input->shape.size() == 4) {
            ESP_LOGI(TAG, "Input tensor dtype=%d shape=%dx%dx%dx%d exponent=%d",
                     (int)input->dtype,
                     (int)input->shape[0], (int)input->shape[1],
                     (int)input->shape[2], (int)input->shape[3],
                     (int)input->exponent);
        } else {
            ESP_LOGI(TAG, "Input tensor dtype=%d shape=%d dims exponent=%d",
                     (int)input->dtype, (int)input->shape.size(), (int)input->exponent);
        }
        s_input_tensor_logged = true;
    }

    s_inference_count++;
    bool log_debug = ((s_inference_count % 20) == 0);
    if (log_debug) {
        float mel_min = mel_spec[0][0];
        float mel_max = mel_spec[0][0];
        float mel_sum = 0.0f;
        for (int t = 0; t < N_FRAMES; t++) {
            for (int m = 0; m < N_MELS; m++) {
                float v = mel_spec[t][m];
                if (v < mel_min) mel_min = v;
                if (v > mel_max) mel_max = v;
                mel_sum += v;
            }
        }
        ESP_LOGI(TAG, "Mel stats: min=%.1f max=%.1f mean=%.1f",
                 (double)mel_min,
                 (double)mel_max,
                 (double)(mel_sum / (float)(N_FRAMES * N_MELS)));
    }

    /* Fill input tensor from mel spectrogram.
     * Handle both float and int8 quantized inputs */
    if (input->dtype == dl::DATA_TYPE_INT8) {
        int8_t *input_data = input->get_element_ptr<int8_t>();
        memset(input_data, 0, input->get_size() * sizeof(int8_t));
        /* Shape: [batch, channel, freq, time] for 4D, [batch, freq, time] for 3D */
        int ndim = (int)input->shape.size();
        bool nhwc = (ndim == 4 &&
                     input->shape[1] == N_MELS &&
                     input->shape[2] == N_FRAMES);
        int freq_dim  = nhwc ? input->shape[1] : ((ndim > 2) ? input->shape[ndim - 2] : N_MELS);
        int time_dim  = nhwc ? input->shape[2] : ((ndim > 1) ? input->shape[ndim - 1] : N_FRAMES);
        int channel_dim = nhwc ? input->shape[3] : 1;
        size_t max_frames = (size_t)N_FRAMES < (size_t)time_dim ? (size_t)N_FRAMES : (size_t)time_dim;
        size_t max_mels  = (size_t)N_MELS   < (size_t)freq_dim ? (size_t)N_MELS   : (size_t)freq_dim;
        int8_t q_min = 127;
        int8_t q_max = -128;

        float inv_scale = DL_RESCALE((int)input->exponent);
        for (size_t t = 0; t < max_frames; t++) {
            for (size_t m = 0; m < max_mels; m++) {
                size_t idx = nhwc
                             ? ((m * (size_t)time_dim + t) * (size_t)channel_dim)
                             : (m * (size_t)time_dim + t);
                if (idx < (size_t)(input->get_size())) {
                    float val = mel_spec[t][m] * inv_scale;
                    if (val > 127.0f) val = 127.0f;
                    if (val < -128.0f) val = -128.0f;
                    int8_t q = (int8_t)lroundf(val);
                    input_data[idx] = q;
                    if (q < q_min) q_min = q;
                    if (q > q_max) q_max = q;
                }
            }
        }
        if (log_debug) {
            ESP_LOGI(TAG, "Input fill: layout=%s freq=%d time=%d q_min=%d q_max=%d",
                     nhwc ? "NHWC" : "linear",
                     freq_dim, time_dim, (int)q_min, (int)q_max);
        }
    } else {
        /* Float input (original path) */
        float *input_data = input->get_element_ptr<float>();
        memset(input_data, 0, input->get_size() * sizeof(float));

        /* Determine dimensions from the model's input tensor */
        int ndim = (int)input->shape.size();
        bool nhwc = (ndim == 4 &&
                     input->shape[1] == N_MELS &&
                     input->shape[2] == N_FRAMES);
        int freq_dim  = nhwc ? input->shape[1] : ((ndim > 2) ? input->shape[ndim - 2] : N_MELS);
        int time_dim  = nhwc ? input->shape[2] : ((ndim > 1) ? input->shape[ndim - 1] : N_FRAMES);
        int channel_dim = nhwc ? input->shape[3] : 1;

        size_t max_frames = (size_t)N_FRAMES < (size_t)time_dim ?
                            (size_t)N_FRAMES : (size_t)time_dim;
        size_t max_mels  = (size_t)N_MELS   < (size_t)freq_dim ?
                            (size_t)N_MELS   : (size_t)freq_dim;

        /* Copy mel_spec[t][m] → input[m * time_dim + t] */
        for (size_t t = 0; t < max_frames; t++) {
            for (size_t m = 0; m < max_mels; m++) {
                size_t idx = nhwc
                             ? ((m * (size_t)time_dim + t) * (size_t)channel_dim)
                             : (m * (size_t)time_dim + t);
                if (idx < (size_t)(input->get_size())) {
                    input_data[idx] = mel_spec[t][m];
                }
            }
        }
        if (log_debug) {
            ESP_LOGI(TAG, "Input fill: layout=%s freq=%d time=%d",
                     nhwc ? "NHWC" : "linear", freq_dim, time_dim);
        }
    }

    /* Keep ESP-DL on the single-core path. RUNTIME_MODE_AUTO can enter the
     * multi-core scheduler on ESP32-P4 and has been observed to trip TLSF
     * heap assertions inside model->run(). */
    s_model->run(dl::RUNTIME_MODE_SINGLE_CORE);

    /* Read output — handle dequantization if needed */
    dl::TensorBase *output_tensor = s_model->get_output();
    if (!output_tensor) {
        ESP_LOGE(TAG, "Failed to get output tensor");
        return ESP_FAIL;
    }

    size_t out_sz = (size_t)output_tensor->get_size();
    if (output_tensor->dtype == dl::DATA_TYPE_INT8) {
        /* Dequantize int8 output to float */
        int8_t *out_int8 = output_tensor->get_element_ptr<int8_t>();
        float dequant_scale = DL_SCALE((int)output_tensor->exponent);
        for (size_t i = 0; i < out_sz && i < 16; i++) {
            output[i] = (float)out_int8[i] * dequant_scale;
        }
    } else {
        /* Float output */
        float *out_data = output_tensor->get_element_ptr<float>();
        memcpy(output, out_data, (out_sz > 16 ? 16 : out_sz) * sizeof(float));
    }
    *output_size = out_sz;

    return ESP_OK;
}

#else /* __cplusplus */

/* Stub when compiled as C — user must compile as C++ for ESP-DL */
static esp_err_t _run_inference(float mel_spec[N_FRAMES][N_MELS],
                                float *output, size_t *output_size)
{
    (void)mel_spec;
    (void)output;
    (void)output_size;
    ESP_LOGE(TAG, "ESP-DL requires C++ compilation");
    return ESP_ERR_NOT_SUPPORTED;
}

#endif /* __cplusplus */

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

esp_err_t audio_cls_srv_init(const char *model_data, size_t model_len)
{
    if (!model_data || model_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    _ensure_tables();

#ifdef __cplusplus
    /* Create ESP-DL model from flatbuffer data */
    if (s_model) {
        ESP_LOGW(TAG, "Model already loaded, re-initializing");
        delete s_model;
        s_model = NULL;
    }

    s_model = new (std::nothrow) dl::Model(
        reinterpret_cast<const char *>(model_data),
        fbs::MODEL_LOCATION_IN_FLASH_RODATA,
        0,              // let greedy manager auto-balance PSRAM/IRAM
        dl::MEMORY_MANAGER_GREEDY,
        nullptr,
        false           // param_copy: weights stay in Flash to avoid tlsf bug
    );

    if (!s_model) {
        ESP_LOGE(TAG, "Failed to allocate ESP-DL model");
        return ESP_FAIL;
    }

    // Check that model was loaded (get_inputs() should be non-empty)
    if (s_model->get_inputs().empty()) {
        ESP_LOGE(TAG, "Model loaded but has no inputs");
        delete s_model;
        s_model = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Audio classification model loaded (size=%zu)", model_len);
#else
    (void)model_data;
    (void)model_len;
    ESP_LOGE(TAG, "ESP-DL requires C++ compilation (set LANGUAGE CXX)");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    /* Reset state */
    esp_err_t cfg_ret = app_config_load_audio_tuning(s_class_thresholds,
                                                     s_class_margins,
                                                     MAX_CLASSES);
    if (cfg_ret != ESP_OK && cfg_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Audio tuning load failed: %s", esp_err_to_name(cfg_ret));
    }

    s_ring_count = 0;
    s_debounce_count = 0;
    s_debounce_class = -1;
    s_inference_count = 0;
    s_gate_skip_count = 0;
    s_active_hold_frames = 0;
    s_process_count = 0;
    s_process_errors = 0;
    s_trigger_count = 0;
    s_last_process_ms = 0;
    s_min_process_ms = UINT32_MAX;
    s_max_process_ms = 0;
    s_process_time_sum_ms = 0;
    memset(s_last_trigger_ms, 0, sizeof(s_last_trigger_ms));

    return ESP_OK;
}

esp_err_t audio_cls_srv_process(const int16_t *audio_frame, size_t frames,
                                cls_result_t *result)
{
    if (!audio_frame || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    if (frames == 0 || frames % AUDIO_FRAME_SAMPLES != 0) {
        /* Accept only full frames (1600 samples each) */
        ESP_LOGW(TAG, "Expected multiple of %d samples, got %zu",
                 AUDIO_FRAME_SAMPLES, frames);
        return ESP_ERR_INVALID_ARG;
    }

    /* Clear result */
    memset(result, 0, sizeof(cls_result_t));
    result->class_id = -1;

    float frame_rms = 0.0f;
    int frame_peak = 0;
    bool frame_active = _audio_frame_is_active(audio_frame, frames, &frame_rms, &frame_peak);
    if (frame_active) {
        s_active_hold_frames = ACTIVE_HOLD_FRAMES;
    } else if (s_active_hold_frames > 0) {
        s_active_hold_frames--;
    }

    /* Accumulate incoming samples */
    _audio_accumulate(audio_frame, frames);

    /* Wait until we have enough samples for a full mel spectrogram */
    if (s_ring_count < TOTAL_NEEDED) {
        ESP_LOGD(TAG, "Buffering audio: %zu/%zu samples",
                 s_ring_count, TOTAL_NEEDED);
        return ESP_OK;  /* not enough data yet, result stays zeroed */
    }

    if (s_active_hold_frames <= 0) {
        s_debounce_class = -1;
        s_debounce_count = 0;
        s_gate_skip_count++;
        if ((s_gate_skip_count % 50) == 1) {
            ESP_LOGI(TAG, "Audio gate: idle rms=%.1f peak=%d, inference skipped",
                     (double)frame_rms, frame_peak);
        }
        _slide_audio_window();
        return ESP_OK;
    }

    /* Compute mel-spectrogram on the latest TOTAL_NEEDED samples only when active. */
    size_t offset = s_ring_count - TOTAL_NEEDED;
    _compute_mel_spectrogram(s_ring, offset, s_mel_spec);

    /* Run ESP-DL inference */
    float output[16];   /* support up to 16 classes */
    size_t output_size = 0;
    esp_err_t err = _run_inference(s_mel_spec, output, &output_size);
    if (err != ESP_OK) {
        return err;
    }

    if (output_size > 16) {
        ESP_LOGW(TAG, "Output size %zu exceeds static buffer, truncating",
                 output_size);
        output_size = 16;
    }

    /* Softmax (model may already produce probabilities, but apply anyway) */
    float probs[16] = {0};
    _softmax(output, probs, (int)output_size);

    /* Find argmax */
    int best_class = 0;
    float best_conf = probs[0];
    int second_class = -1;
    float second_conf = -1.0f;
    for (size_t i = 1; i < output_size; i++) {
        if (probs[i] > best_conf) {
            second_conf = best_conf;
            second_class = best_class;
            best_conf = probs[i];
            best_class = (int)i;
        } else if (probs[i] > second_conf) {
            second_conf = probs[i];
            second_class = (int)i;
        }
    }
    if ((s_inference_count % 10) == 0 && output_size >= MAX_CLASSES) {
        ESP_LOGI(TAG,
                 "Class probs: alarm=%.3f horn=%.3f knock=%.3f clap=%.3f dog=%.3f steps=%.3f glass=%.3f bell=%.3f baby=%.3f engine=%.3f traffic=%.3f bg=%.3f best=%s(%d) second=%s(%d)",
                 (double)probs[0],
                 (double)probs[1],
                 (double)probs[2],
                 (double)probs[3],
                 (double)probs[4],
                 (double)probs[5],
                 (double)probs[6],
                 (double)probs[7],
                 (double)probs[8],
                 (double)probs[9],
                 (double)probs[10],
                 (double)probs[11],
                 _class_name(best_class),
                 best_class,
                 _class_name(second_class),
                 second_class);
    }

    result->class_id = best_class;
    result->confidence = best_conf;

    if (best_class == AUDIO_CLASS_BACKGROUND) {
        s_debounce_class = -1;
        s_debounce_count = 0;
        if (best_conf >= 0.90f && !frame_active) {
            s_active_hold_frames = 0;
        }
        _slide_audio_window();
        return ESP_OK;
    }

    /* Background is suppressed above. Once active, foreground classes can
     * emit UI events; knock uses a lower threshold and faster debounce because
     * it is transient. */
    float class_threshold = _class_threshold(best_class);
    int required_frames = _class_debounce_frames(best_class);
    float class_margin = _class_margin(best_class);
    float margin = best_conf - second_conf;
    bool candidate = best_conf >= class_threshold && margin >= class_margin;
    int64_t now_ms = esp_timer_get_time() / 1000;
    int64_t cooldown_ms = _class_cooldown_ms(best_class);
    bool cooldown_ready = (now_ms - s_last_trigger_ms[best_class]) >= cooldown_ms;

    if (candidate && best_class == s_debounce_class) {
        s_debounce_count++;
        if (s_debounce_count >= required_frames && cooldown_ready) {
            result->triggered = true;
            s_last_trigger_ms[best_class] = now_ms;
            ESP_LOGI(TAG, "TRIGGERED: class=%s(%d) confidence=%.3f threshold=%.2f margin=%.3f cooldown=%lldms rms=%.1f peak=%d",
                     _class_name(best_class), best_class, (double)best_conf,
                     (double)class_threshold, (double)margin, (long long)cooldown_ms,
                     (double)frame_rms, frame_peak);
        } else if ((s_inference_count % 12) == 0) {
            ESP_LOGI(TAG, "Candidate held: class=%s(%d) confidence=%.3f debounce=%d/%d cooldown_ready=%d rms=%.1f peak=%d",
                     _class_name(best_class), best_class, (double)best_conf,
                     s_debounce_count, required_frames, cooldown_ready ? 1 : 0,
                     (double)frame_rms, frame_peak);
        }
    } else {
        if (candidate) {
            s_debounce_class = best_class;
            s_debounce_count = 1;
            if (required_frames == 1 && cooldown_ready) {
                result->triggered = true;
                s_last_trigger_ms[best_class] = now_ms;
                ESP_LOGI(TAG, "TRIGGERED: class=%s(%d) confidence=%.3f threshold=%.2f margin=%.3f cooldown=%lldms rms=%.1f peak=%d",
                         _class_name(best_class), best_class, (double)best_conf,
                         (double)class_threshold, (double)margin, (long long)cooldown_ms,
                         (double)frame_rms, frame_peak);
            } else if ((s_inference_count % 12) == 0) {
                ESP_LOGI(TAG, "Candidate started: class=%s(%d) confidence=%.3f debounce=1/%d cooldown_ready=%d rms=%.1f peak=%d",
                         _class_name(best_class), best_class, (double)best_conf,
                         required_frames, cooldown_ready ? 1 : 0,
                         (double)frame_rms, frame_peak);
            }
        } else {
            if ((s_inference_count % 20) == 0) {
                ESP_LOGI(TAG, "Candidate rejected: class=%s(%d) confidence=%.3f threshold=%.2f margin=%.3f need_margin=%.2f rms=%.1f peak=%d",
                         _class_name(best_class), best_class, (double)best_conf,
                         (double)class_threshold, (double)margin, (double)class_margin,
                         (double)frame_rms, frame_peak);
            }
            s_debounce_class = -1;
            s_debounce_count = 0;
        }
    }

    /* Slide the audio window: discard the oldest AUDIO_FRAME_SAMPLES
     * so the next call processes the next overlapping window. */
    _slide_audio_window();

    return ESP_OK;
}

void audio_cls_srv_record_process_time(uint32_t elapsed_ms,
                                       esp_err_t status,
                                       bool triggered)
{
    s_process_count++;
    s_last_process_ms = elapsed_ms;
    s_process_time_sum_ms += elapsed_ms;
    if (elapsed_ms < s_min_process_ms) {
        s_min_process_ms = elapsed_ms;
    }
    if (elapsed_ms > s_max_process_ms) {
        s_max_process_ms = elapsed_ms;
    }
    if (status != ESP_OK) {
        s_process_errors++;
    }
    if (triggered) {
        s_trigger_count++;
    }
}

void audio_cls_srv_get_stats(audio_cls_srv_stats_t *stats)
{
    if (!stats) {
        return;
    }

    memset(stats, 0, sizeof(*stats));
    stats->processed = s_process_count;
    stats->active_inferences = s_inference_count;
    stats->idle_skipped = s_gate_skip_count;
    stats->triggered = s_trigger_count;
    stats->errors = s_process_errors;
    stats->last_time_ms = s_last_process_ms;
    stats->min_time_ms = (s_min_process_ms == UINT32_MAX) ? 0 : s_min_process_ms;
    stats->max_time_ms = s_max_process_ms;
    stats->avg_time_ms = s_process_count > 0
                             ? (uint32_t)(s_process_time_sum_ms / s_process_count)
                             : 0;
}

void audio_cls_srv_set_threshold(float confidence)
{
    s_threshold = confidence;
    ESP_LOGI(TAG, "Confidence threshold set to %.3f", confidence);
}

float audio_cls_srv_get_threshold(void)
{
    return s_threshold;
}

void audio_cls_srv_set_class_threshold(int class_id, float confidence)
{
    if (class_id < 0 || class_id >= MAX_CLASSES) {
        return;
    }
    if (confidence < 0.0f) {
        confidence = 0.0f;
    } else if (confidence > 1.0f) {
        confidence = 1.0f;
    }
    s_class_thresholds[class_id] = confidence;
    esp_err_t ret = app_config_save_audio_class_tuning(class_id,
                                                       s_class_thresholds[class_id],
                                                       s_class_margins[class_id]);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to save class threshold: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "Class %s(%d) threshold set to %.3f",
             _class_name(class_id), class_id, (double)confidence);
}

float audio_cls_srv_get_class_threshold(int class_id)
{
    if (class_id < 0 || class_id >= MAX_CLASSES) {
        return s_threshold;
    }
    return s_class_thresholds[class_id];
}

void audio_cls_srv_set_class_margin(int class_id, float margin)
{
    if (class_id < 0 || class_id >= MAX_CLASSES) {
        return;
    }
    if (margin < 0.0f) {
        margin = 0.0f;
    } else if (margin > 1.0f) {
        margin = 1.0f;
    }
    s_class_margins[class_id] = margin;
    esp_err_t ret = app_config_save_audio_class_tuning(class_id,
                                                       s_class_thresholds[class_id],
                                                       s_class_margins[class_id]);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to save class margin: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "Class %s(%d) margin set to %.3f",
             _class_name(class_id), class_id, (double)margin);
}

float audio_cls_srv_get_class_margin(int class_id)
{
    if (class_id < 0 || class_id >= MAX_CLASSES) {
        return 0.0f;
    }
    return s_class_margins[class_id];
}
