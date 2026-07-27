# ESP32-P4 Edge Classifier

[![Platform](https://img.shields.io/badge/platform-ESP32--P4-blue)](https://www.espressif.com/en/products/socs/esp32-p4)
[![Framework](https://img.shields.io/badge/framework-ESP--IDF%205.x-green)](https://docs.espressif.com/projects/esp-idf/)
[![License](https://img.shields.io/badge/license-MIT-lightgrey)](LICENSE)

**A dual-modal edge AI device running on ESP32-P4** — real-time audio scene classification + visual face/emotion detection, with a 4-inch DSI touch display and voice feedback. Built from bare-metal drivers up to LVGL UI, all in a clean 4-layer architecture.

<p align="center">
  <i>Screenshots / demo GIF coming soon</i>
</p>

---

## 🧠 What It Does

| Modality | Task | Model | Accuracy |
|----------|------|-------|----------|
| Audio | Scene classification (12 classes) | ONNX MobileNetV2 · keep14 | 90.53% |
| Visual | Face detection + emotion (7 classes) | PicoDet + custom emotion model | — |

The device continuously listens through a microphone array, classifies ambient audio scenes every second, and simultaneously performs face detection and emotion recognition from the CSI camera feed. Results are displayed on an animated LVGL 9 UI with voice prompts via MAX98357 speaker.

---

## 🧱 Architecture

```
┌──────────────────────────────────────────────┐
│                  App Layer                   │
│     display_app  ·  audio_event_app          │
├──────────────────────────────────────────────┤
│               Service Layer                  │
│  audio_classify  ·  visual_classify         │
│  audio_playback  ·  audio_frame_bus         │
│  camera  ·  touch_input  ·  alert_feedback  │
│  event  ·  speech  ·  history  ·  telemetry │
│  app_config  ·  app_state                    │
├──────────────────────────────────────────────┤
│                 HAL Layer                    │
│  display  ·  audio  ·  mic  ·  touch  ·  camera │
├──────────────────────────────────────────────┤
│               Driver Layer                   │
│  dsi_lcd(ST7701)  ·  es8311  ·  max98357    │
│  gt911  ·  sc2336_camera  ·  motor_driver   │
└──────────────────────────────────────────────┘
```

### Task Map (Dual-Core Pinned)

| Core | Task | Priority | Stack |
|------|------|----------|-------|
| Core 0 (Realtime) | `audio_capture` | 10 | 4096 |
| Core 0 | `audio_inf` | 4 | 12288 |
| Core 1 (UI + Camera) | `ui_task` | 5 | 6144 |
| Core 1 | `camera_svc` | 3 | 5120 |
| Core 1 | `visual_inf` | 2 | 16384 |
| Core 1 | `telemetry` | 1 | 4096 |

Audio capture runs at highest priority on Core 0 for glitch-free 16kHz streaming. Heavy inference workloads (audio MobileNetV2, PicoDet) are isolated on separate cores to avoid UI jank.

---

## 🔧 Hardware

| Component | Model | Interface |
|-----------|-------|-----------|
| SoC | ESP32-P4 (dual RISC-V, 400 MHz) | — |
| Board | Waveshare ESP32-P4 | — |
| Display | 4-inch 480×800 LCD | MIPI-DSI (ST7701) |
| Touch | Capacitive touch panel | I²C (GT911) |
| Camera | SC2336 | CSI-2 MIPI, RAW8, 1024×600 @ 30fps |
| Audio Codec | ES8311 + ES7210 | I²S0 |
| Speaker Amp | MAX98357 | I²S0 |
| Vibration Motor | DC motor | GPIO PWM |
| Flash | 16 MB | — |
| PSRAM | 8 MB | Octal |

The display is rotated 90° for portrait orientation, using PSRAM-backed double framebuffer.

---

## 📁 Project Structure

```
main/
├── app/                    # Application orchestration
│   ├── audio_event_app     # Audio classification → UI/voice dispatch
│   └── display_app         # Page switching, LVGL lifecycle
├── driver/                 # Bare-metal peripheral drivers
│   ├── display/dsi_lcd     # MIPI-DSI init + LVGL port
│   ├── audio/es8311        # ES8311 DAC codec
│   ├── audio/max98357      # MAX98357 I²S speaker amp
│   ├── touch/gt911         # GT911 capacitive touch
│   ├── sensor/sc2336       # SC2336 CSI camera probe
│   └── actuator/motor      # PWM vibration motor
├── hal/                    # Hardware abstraction wrappers
│   ├── display_hal
│   ├── audio_hal
│   ├── mic_hal
│   ├── touch_hal
│   └── camera_hal
├── service/                # Business logic services
│   ├── audio_classify      # MobileNetV2 ONNX inference
│   ├── visual_classify     # PicoDet face + emotion
│   ├── audio_frame_bus     # Ring buffer between capture & inference
│   ├── audio_playback      # WAV playback queue
│   ├── camera_service      # CSI frame capture
│   ├── touch_input         # Touch event dispatch
│   ├── alert_feedback      # Motor vibration + voice alert
│   ├── event_service       # Inter-task event bus
│   ├── speech_service      # Pre-recorded voice prompts
│   ├── history_service     # NVS-backed result log
│   ├── telemetry_service   # Stack/CPU monitoring
│   ├── app_config          # NVS settings persistence
│   └── app_state           # Shared runtime state
├── tasks/                  # FreeRTOS task entry points
│   ├── audio_capture_task  # I²S mic → ring buffer
│   ├── audio_inference_task# Audio model inference loop
│   └── ui_task             # LVGL render loop
├── lvgl_port/              # LVGL integration
│   ├── lvgl_port           # Display + touch init
│   └── ui/                 # 7-page LVGL UI
│       ├── ui_main         # Dashboard
│       ├── ui_emotion      # Face emotion visualization
│       ├── ui_voice        # Voice assistant panel
│       ├── ui_quick_panel  # Quick settings overlay
│       ├── ui_settings     # Device settings
│       ├── ui_notify       # Alert notifications
│       └── ui_log          # Classification history
├── models/                 # AI model binaries
│   ├── audio_model.h       # ONNX MobileNetV2 (761 KB)
│   ├── emotion_model.espdl # Emotion classifier (ESP-DL format)
│   └── espdet_pico_224_224_face.espdl  # PicoDet face detector
├── assets/speech/          # Voice prompt WAV files (12 phrases)
├── runtime/task_config.h   # Centralized RTOS sizing
├── app_main.c              # Boot + init sequence
├── CMakeLists.txt
└── idf_component.yml       # Managed: esp-dl, waveshare/esp32_p4_platform
```

---

## 🚀 Getting Started

### Prerequisites

- ESP-IDF v5.x (with ESP32-P4 support)
- ESP-DL (auto-fetched via IDF Component Manager)
- Waveshare ESP32-P4 board

### Build & Flash

```bash
# Clone
git clone git@github.com:MikCslu/esp32-p4-edge-classifier.git
cd esp32-p4-edge-classifier

# Set target
idf.py set-target esp32p4

# Build
idf.py build

# Flash + monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

> **Note:** The `waveshare/esp32_p4_platform` component provides BSP drivers for the Waveshare board (LCD, touch, codec). These are fetched automatically by the IDF Component Manager.

### Hardware Setup

- Connect SC2336 camera to CSI interface
- Connect speaker to I²S breakout (MAX98357)
- Power via USB-C (5V/2A recommended)

---

## 🎯 Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| **4-layer architecture** | Driver/HAL/Service/App separation keeps peripheral init, abstraction, logic, and UI decoupled — each layer testable in isolation |
| **Core-pinned tasks** | Audio capture on Core 0 at highest priority eliminates I²S underruns; UI on Core 1 at mid priority guarantees 30fps rendering |
| **Audio frame bus** | Ring buffer between capture and inference tasks decouples real-time I/O from ML processing latency |
| **ESP-DL for inference** | Espressif's optimized NN library — ONNX import, C++ inference API, 16-bit quantization support |
| **NVS-backed config + history** | Settings and classification logs survive power cycles without external storage |
| **event_service** | Centralized publish/subscribe decouples services — audio result → UI update, voice trigger, history log via single event post |

---

## 📊 Performance

| Metric | Value |
|--------|-------|
| Audio model size | 189 KB (quantized) |
| Audio inference time | ~100–150 ms (MobileNetV2 on P4) |
| Face detection | PicoDet-224 @ ~15–20 fps |
| UI frame rate | 30 fps (LVGL with PSRAM double buffer) |

---

## 🔮 Planned / In Progress

- [ ] Wi-Fi OTA firmware updates
- [ ] MQTT telemetry streaming
- [ ] Custom wake-word detection
- [ ] Camera snapshot capture to SD card
- [ ] Web dashboard for remote monitoring

---

## 📄 License

MIT

---

## 👤 Author

**MikCslu** — [GitHub](https://github.com/MikCslu)

Built as a capstone project for embedded AI / edge computing. Questions or collaboration: open an issue or PR.
