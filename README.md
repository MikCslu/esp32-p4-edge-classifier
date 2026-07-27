# ESP32-P4 边缘分类设备

[![Platform](https://img.shields.io/badge/platform-ESP32--P4-blue)](https://www.espressif.com/en/products/socs/esp32-p4)
[![Framework](https://img.shields.io/badge/framework-ESP--IDF%205.x-green)](https://docs.espressif.com/projects/esp-idf/)
[![License](https://img.shields.io/badge/license-MIT-lightgrey)](LICENSE)

**基于 ESP32-P4 的双模态边缘 AI 设备** — 实时音频场景分类 + 视觉人脸/表情检测，搭配 4 寸 DSI 触摸屏与语音播报。从裸机驱动到 LVGL 界面，采用 Driver → HAL → Service → App 四层解耦架构。

<p align="center">
  <i>实物演示截图 / GIF 待补充</i>
</p>

---

## 🧠 功能概览

| 模态 | 任务 | 模型 | 准确率 |
|------|------|------|--------|
| 音频 | 场景分类（12 类） | ONNX MobileNetV2 · keep14 | 90.53% |
| 视觉 | 人脸检测 + 表情识别（7 类） | PicoDet + 自训练表情模型 | — |

设备通过麦克风阵列持续收音，每秒进行一次音频场景推理；同时通过 CSI 摄像头实时检测人脸并识别表情。推理结果在 LVGL 9 动画界面上展示，并通过 MAX98357 功放播报语音反馈。

---

## 🧱 软件架构

```
┌──────────────────────────────────────────────┐
│                 应用层 (App)                  │
│     display_app  ·  audio_event_app          │
├──────────────────────────────────────────────┤
│                服务层 (Service)               │
│  audio_classify  ·  visual_classify          │
│  audio_playback  ·  audio_frame_bus          │
│  camera  ·  touch_input  ·  alert_feedback   │
│  event  ·  speech  ·  history  ·  telemetry  │
│  app_config  ·  app_state                    │
├──────────────────────────────────────────────┤
│              硬件抽象层 (HAL)                 │
│  display  ·  audio  ·  mic  ·  touch  ·  camera │
├──────────────────────────────────────────────┤
│               驱动层 (Driver)                 │
│  dsi_lcd(ST7701)  ·  es8311  ·  max98357     │
│  gt911  ·  sc2336_camera  ·  motor_driver    │
└──────────────────────────────────────────────┘
```

### 双核任务分配

| 核心 | 任务 | 优先级 | 栈大小 |
|------|------|--------|--------|
| Core 0（实时） | `audio_capture` | 10 | 4096 |
| Core 0 | `audio_inf` | 4 | 12288 |
| Core 1（UI+摄像头）| `ui_task` | 5 | 6144 |
| Core 1 | `camera_svc` | 3 | 5120 |
| Core 1 | `visual_inf` | 2 | 16384 |
| Core 1 | `telemetry` | 1 | 4096 |

音频采集以最高优先级固定在 Core 0，保证 16kHz 无丢帧；Audio/Visual 两个重推理任务分别隔离在不同核心，避免 UI 卡顿。

---

## 🔧 硬件配置

| 组件 | 型号 | 接口 |
|------|------|------|
| 主控 | ESP32-P4（双核 RISC-V，最高 400MHz） | — |
| 开发板 | Waveshare ESP32-P4 | — |
| 显示屏 | 4.0 寸 480×800 LCD | MIPI-DSI（ST7701） |
| 触摸 | 电容触摸面板 | I²C（GT911） |
| 摄像头 | SC2336 | CSI-2 MIPI，RAW8，1024×600@30fps |
| 音频 Codec | ES8311 + ES7210 | I²S0 |
| 功放 | MAX98357 | I²S0 |
| 振动马达 | 直流电机 | GPIO PWM |
| Flash | 16 MB | — |
| PSRAM | 8 MB | Octal |

显示屏旋转 90° 竖屏使用，PSRAM 双缓冲保证 30fps 刷新。

---

## 📁 项目结构

```
main/
├── app/                     # 应用编排
│   ├── audio_event_app      # 音频分类 → UI 刷新 / 语音播报
│   └── display_app          # 页面切换、LVGL 生命周期
├── driver/                  # 裸机外设驱动
│   ├── display/dsi_lcd      # MIPI-DSI 初始化 + LVGL 对接
│   ├── audio/es8311         # ES8311 DAC 编解码
│   ├── audio/max98357       # MAX98357 I²S 功放
│   ├── touch/gt911          # GT911 电容触摸
│   ├── sensor/sc2336        # SC2336 CSI 摄像头探测
│   └── actuator/motor       # PWM 振动马达
├── hal/                     # 硬件抽象封装
│   ├── display_hal
│   ├── audio_hal
│   ├── mic_hal
│   ├── touch_hal
│   └── camera_hal
├── service/                 # 业务逻辑服务
│   ├── audio_classify       # MobileNetV2 ONNX 推理
│   ├── visual_classify      # PicoDet 人脸 + 表情
│   ├── audio_frame_bus      # 采集与推理间环形缓冲
│   ├── audio_playback       # WAV 播放队列
│   ├── camera_service       # CSI 帧采集
│   ├── touch_input          # 触摸事件分发
│   ├── alert_feedback       # 马达振动 + 提示音
│   ├── event_service        # 跨任务事件总线
│   ├── speech_service       # 预录语音片段播报
│   ├── history_service      # NVS 持久化分类记录
│   ├── telemetry_service    # 栈/CPU 监控
│   ├── app_config           # NVS 设置持久化
│   └── app_state            # 共享运行状态
├── tasks/                   # FreeRTOS 任务入口
│   ├── audio_capture_task   # I²S 麦克风 → 环形缓冲
│   ├── audio_inference_task # 音频模型推理循环
│   └── ui_task              # LVGL 渲染循环
├── lvgl_port/               # LVGL 集成
│   ├── lvgl_port            # 显示 + 触摸初始化
│   └── ui/                  # 7 页面 LVGL 界面
│       ├── ui_main          # 主面板
│       ├── ui_emotion       # 表情识别可视化
│       ├── ui_voice         # 语音助手面板
│       ├── ui_quick_panel   # 快捷设置
│       ├── ui_settings      # 设备设置
│       ├── ui_notify        # 通知提醒
│       └── ui_log           # 分类历史记录
├── models/                  # AI 模型
│   ├── audio_model.h        # ONNX MobileNetV2（761KB）
│   ├── emotion_model.espdl  # 表情分类器（ESP-DL 格式）
│   └── espdet_pico_224_224_face.espdl  # PicoDet 人脸检测
├── assets/speech/           # 语音播报 WAV（12 句）
├── runtime/task_config.h    # 统一 FreeRTOS 参数配置
├── app_main.c               # 启动 + 初始化序列
├── CMakeLists.txt
└── idf_component.yml        # 组件管理：esp-dl、waveshare 平台驱动
```

---

## 🚀 快速开始

### 环境要求

- ESP-IDF v5.x（需包含 ESP32-P4 支持）
- ESP-DL（IDF Component Manager 自动拉取）
- Waveshare ESP32-P4 开发板

### 编译 & 烧录

```bash
# 克隆仓库
git clone git@github.com:MikCslu/esp32-p4-edge-classifier.git
cd esp32-p4-edge-classifier

# 设置目标芯片
idf.py set-target esp32p4

# 编译
idf.py build

# 烧录并打开串口监视
idf.py -p /dev/ttyUSB0 flash monitor
```

> `waveshare/esp32_p4_platform` 组件提供 Waveshare 开发板的 BSP 驱动（LCD、触摸、Codec），由 IDF Component Manager 自动拉取。

### 硬件连线

- SC2336 摄像头接入 CSI 接口
- MAX98357 功放接入 I²S 引脚
- USB-C 供电（建议 5V/2A）

---

## 🎯 关键设计决策

| 决策 | 理由 |
|------|------|
| **四层架构** | Driver/HAL/Service/App 逐层解耦，外设初始化、抽象、业务逻辑、UI 各自独立，便于单元测试和维护 |
| **双核任务固定** | Core 0 最高优先级采集音频，杜绝 I²S 丢帧；Core 1 中优先级渲染 UI，保证 30fps |
| **音频帧总线** | 采集与推理之间的环形缓冲，解耦实时 I/O 与 ML 推理延迟 |
| **ESP-DL 推理框架** | 乐鑫官方 NN 加速库，支持 ONNX 导入、C++ API、16-bit 量化 |
| **NVS 持久化** | 设置参数与分类历史记录均存入 NVS，断电不丢失 |
| **事件总线** | 发布-订阅模式解耦各服务——一条音频结果通过单次 event post 同时触发 UI 更新、语音播报、历史记录 |

---

## 📊 性能指标

| 指标 | 数值 |
|------|------|
| 音频模型大小 | 189 KB（量化后） |
| 音频推理耗时 | ~100–150 ms（MobileNetV2 on P4） |
| 人脸检测帧率 | ~15–20 fps（PicoDet-224） |
| UI 刷新率 | 30 fps（LVGL + PSRAM 双缓冲） |

---

## 🔮 待完成

- [ ] Wi-Fi OTA 固件升级
- [ ] MQTT 遥测数据上云
- [ ] 自定义唤醒词检测
- [ ] 摄像头抓拍存 SD 卡
- [ ] Web 远程监控面板

---

## 📄 开源协议

MIT

---

## 👤 作者

**MikCslu** — [GitHub](https://github.com/MikCslu)

嵌入式 AI / 边缘计算课程设计项目。欢迎提 Issue 或 PR 交流。
