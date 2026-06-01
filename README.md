<p align="center">
  <img src="docs/images/logo.png" alt="VisionPass Logo" width="120" onerror="this.style.display='none'">
</p>

<h1 align="center">VisionPass</h1>
<h3 align="center">AI-Powered Smart Door Access Control System</h3>
<p align="center">
  基于 I.MX6ULL 嵌入式平台的多模态智能门禁系统
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-I.MX6ULL%20ARM--v7-blue?style=flat-square" alt="Platform">
  <img src="https://img.shields.io/badge/Framework-Qt%205.12-green?style=flat-square" alt="Framework">
  <img src="https://img.shields.io/badge/AI%20Engine-NCNN-orange?style=flat-square" alt="AI Engine">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-yellow?style=flat-square" alt="Language">
  <img src="https://img.shields.io/badge/License-MIT-blue?style=flat-square" alt="License">
  <img src="https://img.shields.io/badge/Status-Active%20Development-brightgreen?style=flat-square" alt="Status">
</p>

<p align="center">
  <a href="#-系统特性">特性</a> •
  <a href="#-系统架构">架构</a> •
  <a href="#-硬件平台">硬件</a> •
  <a href="#-快速开始">快速开始</a> •
  <a href="#-项目结构">项目结构</a> •
  <a href="#-文档">文档</a> •
  <a href="#-外部资源">外部资源</a>
</p>

---

## 📋 项目简介

VisionPass 是一套基于正点原子 I.MX6ULL Alpha 开发板的 **AI 智能门禁系统**，支持人脸识别、RFID 刷卡、语音指令、密码输入、物理按键五种开锁方式。所有 AI 推理均在嵌入式端侧完成（NCNN + MobileFaceNet），不依赖上位机或云端服务。

本项目是对 [VisionPass 原版](https://github.com/happyeverydaybest-eng/VisionPass) 的深度重构与功能增强，采用全新的系统架构、端侧 AI 推理方案，以及更加合理的硬件外设选型。

### 与原版的核心区别

| 维度 | 原版方案 | 本项目方案 | 改进说明 |
|:---:|:---:|:---:|:---:|
| **人脸识别** | PC 端 Dlib 推理 | 板载 NCNN + MobileFaceNet | 端侧推理，无需上位机 |
| **接近传感器** | ADS1115 ADC + 模拟红外 | 板载 AP3216C（I2C 数字） | 无需外接 ADC，简化硬件 |
| **惯性传感器** | MPU6050（I2C） | 板载 ICM20608（SPI） | 更高速率，板载集成 |
| **数据存储** | AT24C64 EEPROM（8KB） | SQLite + JSON（eMMC/SD） | 无限容量，便于管理 |
| **语音识别** | — | DTW 模板匹配（MFCC 特征） | 新增功能，无外部依赖 |
| **PC 端工具** | 视频监控 + 人脸识别 | 用户管理 + 消息通信 | 专注管理功能 |

---

## 🚀 系统特性

### 五种开锁方式

| 方式 | 技术实现 | 响应时间 |
|:---:|:---:|:---:|
| 🔍 **人脸识别** | NCNN + MobileFaceNet + OpenCV Haar | ~500ms |
| 📇 **RFID 刷卡** | RC522 + SPI 字符设备驱动 | ~100ms |
| 🗣️ **语音指令** | WM8960 录音 + DTW 模板匹配 | ~1s |
| 🔢 **密码输入** | SHA-256 哈希 + AES-128 加密传输 | 即时 |
| 🔘 **物理按键** | GPIO 中断检测 | 即时 |

### 系统能力

- **实时视频流** — V4L2 采集 + UDP 广播，支持远程监控
- **消息通信** — TCP/UDP 双协议，支持文字与语音消息
- **用户管理** — PC 端 GUI 工具，人脸照片导入、RFID 卡管理
- **触屏交互** — 自定义触屏键盘，适配 7 寸 LCD（1024×600）
- **自动部署** — SCP 一键同步数据库与模型到开发板
- **硬件驱动** — 自研 Linux 内核模块（RC522 / SG90 / IR Sensor）

---

## 🏗️ 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        PC 端 (Ubuntu)                           │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────┐  │
│  │  user_manager    │  │ message_manager  │  │  SCP/SSH     │  │
│  │  用户管理 GUI    │  │  消息管理 GUI    │  │  文件同步    │  │
│  │  - 人脸照片导入  │  │  - 文字消息收发  │  │              │  │
│  │  - RFID 卡管理   │  │  - 语音消息收发  │  │              │  │
│  │  - 数据库同步    │  │  - UDP 设备发现  │  │              │  │
│  └────────┬─────────┘  └────────┬─────────┘  └──────┬───────┘  │
│           │ TCP/UDP             │ TCP/UDP            │ SCP      │
└───────────┼─────────────────────┼────────────────────┼──────────┘
            │                     │                    │
            ▼                     ▼                    ▼
┌─────────────────────────────────────────────────────────────────┐
│                    I.MX6ULL 开发板 (ARM)                        │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                   VisionPass 主程序                      │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │   │
│  │  │ 视频采集  │ │ 人脸识别  │ │ 硬件控制  │ │ 语音处理  │   │   │
│  │  │ V4L2     │ │ NCNN     │ │ GPIO/SPI │ │ ALSA     │   │   │
│  │  │ OV2640   │ │ MobileF  │ │ RC522    │ │ WM8960   │   │   │
│  │  └──────────┘ │ FaceNet  │ │ SG90     │ │ DTW+MFCC │   │   │
│  │               │ Haar     │ │ IR/BEEP  │ │          │   │   │
│  │               └──────────┘ └──────────┘ └──────────┘   │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────────────┐   │   │
│  │  │ 数据库   │ │ 网络通信  │ │       Qt GUI         │   │   │
│  │  │ SQLite   │ │ TCP/UDP  │ │ 主界面 / 消息对话框   │   │   │
│  │  │ JSON配置 │ │ Message  │ │ 密码输入 / 触屏键盘   │   │   │
│  │  └──────────┘ │ Client   │ │ 通知浮层 / 语音按钮   │   │   │
│  │               └──────────┘ └──────────────────────┘   │   │
│  └──────────────────────────────────────────────────────────┘   │
│           │              │              │            │          │
│       ┌───┴───┐    ┌────┴────┐   ┌────┴────┐  ┌───┴───┐      │
│       │ OV2640│    │ RC522   │   │ SG90    │  │ WM8960│      │
│       │Camera │    │ RFID    │   │ Servo   │  │ Audio │      │
│       └───────┘    └─────────┘   └─────────┘  └───────┘      │
└─────────────────────────────────────────────────────────────────┘
```

### 软件分层

| 层级 | 组件 | 技术栈 |
|:---:|:---:|:---:|
| **应用层** | Qt GUI、业务逻辑 | Qt 5.12 Widgets、C++17 |
| **服务层** | 人脸识别、语音处理、网络通信 | NCNN、DTW/MFCC、QTcpSocket |
| **驱动层** | 硬件抽象、字符设备 | Linux Kernel Module、V4L2、ALSA |
| **硬件层** | 传感器、执行器、外设 | I2C、SPI、GPIO、PWM、I2S |

---

## 🔧 硬件平台

### 开发板

**正点原子 Alpha I.MX6ULL** — ARM Cortex-A7 @792MHz，512MB DDR3，8GB eMMC

板载资源：双网口、OV2640 CSI 摄像头接口、WM8960 音频、ICM20608 六轴、AP3216C 红外/光照、蜂鸣器、功能按键

### 外接模块（预算 ¥49 - 73）

| 模块 | 接口 | 用途 | 参考价格 |
|:---:|:---:|:---:|:---:|
| OV2640 摄像头 | CSI | 视频采集 + 人脸识别 | ¥25-35 |
| RC522 RFID 模组 | SPI（ECSPI3） | 刷卡开锁 | ¥10-15 |
| SG90 舵机 | PWM（GPIO1_IO27） | 门锁执行器 | ¥5-8 |
| 门磁开关 | GPIO | 门状态检测 | ¥3-5 |
| 杜邦线 | — | 接线 | ¥6-10 |

### 接线示意

```
┌─────────────────────────────────────────────────────────┐
│                  正点原子 Alpha 开发板                   │
│                                                         │
│  JP6 排针                                                │
│  ┌────┬────┬────┬────┬────┬────┬────┬────┐              │
│  │Pin5│Pin6│    │Pin9│Pin10│Pin11│    │Pin22│             │
│  │PWM │CS  │    │MOSI│MISO│SCK │    │IR  │              │
│  └──┬─┴──┬─┘    └──┬─┴──┬─┴──┬─┘    └──┬─┘              │
│     │    │         │    │    │         │                │
│  ┌──┴──┐│    ┌─────┴────┴────┴──────┐ │                │
│  │SG90 ││    │      RC522           │ │                │
│  │舵机 ││    │ VCC=3.3V  RST→3.3V  │ │                │
│  │橙线 ││    │ CS→Pin6   GND→GND   │ │                │
│  └─────┘│    └─────────────────────┘ │                │
│         │                    ┌────────┴──┐              │
│    ┌────┴────┐               │ IR 模块   │              │
│    │GPIO_IO26│               │ VCC=3.3V  │              │
│    └─────────┘               │ OUT→Pin22 │              │
│                              │ GND→GND   │              │
│                              └───────────┘              │
└─────────────────────────────────────────────────────────┘
```

> ⚠️ **关键注意**：RC522 VCC 必须接 **3.3V**（非 5V），否则会烧毁模块。详细接线图见 [docs/硬件接线图.md](docs/硬件接线图.md)。

---

## ⚡ 快速开始

### 环境要求

| 工具 | 版本 | 说明 |
|:---:|:---:|:---:|
| Ubuntu | 20.04+ | 开发主机操作系统 |
| arm-linux-gnueabihf-gcc | 4.9.4 | Linaro 交叉编译工具链 |
| CMake | 3.10+ | 构建系统 |
| Qt 5.12 | 5.12.12 ARM | 目标板 Qt 运行时 |
| NCNN | 20240410+ | AI 推理框架（ARM 版本） |

### 1. 克隆项目

```bash
git clone https://github.com/happyeverydaybest-eng/visionpass-ai.git
cd visionpass
```

### 2. 安装交叉编译工具链

```bash
# 下载 Linaro GCC 4.9.4
wget https://releases.linaro.org/components/toolchain/binaries/4.9-2017.01/arm-linux-gnueabihf/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf.tar.xz
sudo tar xf gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf.tar.xz -C /usr/local/arm/

# 添加到 PATH
echo 'export PATH=/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
```

### 3. 编译 NCNN（ARM 版本）

```bash
git clone https://github.com/Tencent/ncnn.git
cd ncnn && mkdir build-arm && cd build-arm
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/arm-linux-gnueabihf.toolchain.cmake \
      -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_BUILD_TOOLS=OFF ..
make -j$(nproc)
make install
```

### 4. 编译 VisionPass

```bash
cd app
mkdir -p build-arm && cd build-arm
cmake -DCMAKE_TOOLCHAIN_FILE=../../config/arm-linux-gnueabihf.toolchain.cmake ..
make -j$(nproc)
```

### 5. 编译内核驱动

```bash
cd drivers

# RC522 RFID 驱动
cd rc522 && make && cd ..

# SG90 舵机驱动
cd servo && make && cd ..

# IR 红外传感器驱动
cd ir_sensor && make && cd ..
```

### 6. 编译 PC 端工具

```bash
# 用户管理工具
cd tools/user_manager && mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 消息管理工具
cd tools/message_manager && mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

### 7. 部署到开发板

```bash
# 创建目录结构
ssh root@<board-ip> "mkdir -p /opt/visionpass/{bin,drivers,model,data,logs}"

# 传输文件
scp app/build-arm/visionpass root@<board-ip>:/opt/visionpass/bin/
scp drivers/*/*.ko root@<board-ip>:/opt/visionpass/drivers/
scp model/MobileFaceNet* model/haarcascade_frontalface_alt2.xml root@<board-ip>:/opt/visionpass/model/

# 加载驱动并启动
ssh root@<board-ip>
insmod /opt/visionpass/drivers/rc522.ko
insmod /opt/visionpass/drivers/servo.ko
insmod /opt/visionpass/drivers/ir_sensor.ko
export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0
/opt/visionpass/bin/visionpass
```

> 📖 详细部署步骤请参考 [docs/部署手册.md](docs/部署手册.md)。

---

## 📁 项目结构

```
visionpass/
├── app/                          # 主应用程序
│   ├── CMakeLists.txt            # CMake 构建配置
│   ├── main.cpp                  # 程序入口（信号处理 + Qt 初始化）
│   ├── src/
│   │   ├── capture/              # V4L2 视频采集模块
│   │   ├── controller/           # 系统控制器（核心状态机）
│   │   ├── database/             # SQLite 数据库管理
│   │   ├── face/                 # 人脸检测与识别（NCNN + Haar）
│   │   ├── hardware/             # 硬件抽象层（RC522 / Servo / IR / Beeper）
│   │   ├── network/              # TCP/UDP 网络通信
│   │   └── voice/                # 语音处理（DTW + MFCC）
│   └── ui/                       # Qt GUI 界面
│       ├── MainWindow.*          # 主界面
│       ├── MessageDialog.*       # 消息对话框
│       ├── TouchKeyboard.*       # 触屏键盘
│       ├── VoiceRecordButton.*   # 语音录制按钮
│       ├── PasswordDialog.*      # 密码输入对话框
│       └── NotificationOverlay.* # 通知浮层
│
├── drivers/                      # Linux 内核驱动模块
│   ├── rc522/                    # RC522 RFID SPI 驱动
│   ├── servo/                    # SG90 舵机 PWM 驱动
│   └── ir_sensor/                # IR 红外传感器 GPIO 驱动
│
├── model/                        # AI 模型文件
│   ├── MobileFaceNet.param       # NCNN 模型结构（FP32）
│   ├── MobileFaceNet.bin         # NCNN 模型权重（FP32, 4MB）
│   ├── MobileFaceNet-fp16.*      # FP16 量化版本（2MB）
│   └── haarcascade_frontalface_alt2.xml  # OpenCV Haar 级联
│
├── tools/                        # PC 端辅助工具
│   ├── user_manager/             # 用户管理 GUI（Qt）
│   ├── message_manager/          # 消息管理 GUI（Qt）
│   └── deploy-to-board.sh        # 一键部署脚本
│
├── config/                       # 构建配置
│   └── arm-linux-gnueabihf.toolchain.cmake  # ARM 交叉编译工具链
│
├── docs/                         # 项目文档
│   ├── 部署手册.md
│   ├── 硬件接线图.md
│   ├── 故障排查手册.md
│   ├── 用户管理操作手册.md
│   └── ...
│
└── .claude/                      # AI 辅助开发配置
```

---

## 🔬 技术细节

### 人脸识别流水线

```
摄像头采集 (V4L2, 640×480)
       │
       ▼
人脸检测 (OpenCV Haar Cascade)
       │
       ▼
ROI 裁剪 + 112×112 对齐
       │
       ▼
特征提取 (NCNN MobileFaceNet, 128D 向量)
       │
       ▼
特征匹配 (余弦相似度, 阈值 0.65)
       │
       ▼
开锁 / 拒绝
```

### 语音识别方案

由于 GCC 4.9.4 不支持 C++17，无法编译 Sherpa-ONNX，采用 DTW（Dynamic Time Warping）模板匹配方案：

1. **录音** — WM8960 I2S，16kHz / 16bit / 单声道
2. **预加重** — 高通滤波增强高频分量
3. **分帧加窗** — 25ms 帧长，10ms 帧移，Hamming 窗
4. **MFCC 特征** — 13 维 MFCC + 一阶差分 + 二阶差分 = 39 维
5. **DTW 匹配** — 动态时间规整，与注册模板比较距离

### 网络通信协议

```
PC message_manager ←── TCP 9500 ──→ 板载 MessageClient
                      UDP 9501 (设备发现)
                      UDP 9503 (发现应答)

消息格式（JSON + 换行分隔）:
{
  "type": "text" | "voice" | "discover" | "discover_reply",
  "text": "消息内容",
  "voice": "base64 编码的 PCM 音频",
  "duration": 3,
  "sender": "设备名称"
}
```

---

## 📖 文档

| 文档 | 说明 |
|:---:|:---:|
| [部署手册](docs/部署手册.md) | 完整的编译、部署、启动流程 |
| [硬件接线图](docs/硬件接线图.md) | 所有外设的详细接线说明（v2.0） |
| [故障排查手册](docs/故障排查手册.md) | 常见问题及解决方案 |
| [用户管理操作手册](docs/用户管理操作手册.md) | PC 端 user_manager 使用指南 |
| [项目分析](docs/01-项目分析.md) | 原版架构分析与技术选型 |
| [复刻指南](docs/02-复刻指南.md) | 从零复刻本项目的完整步骤 |
| [硬件购买清单](docs/03-硬件资源购买清单.md) | 外接模块采购参考 |
| [学习指南](docs/学习指南.md) | 嵌入式 Linux 学习路线 |

---

## 📦 外部资源

部分文件因体积较大，不纳入 Git 版本管理，存放于网盘：

| 文件 | 大小 | 说明 |
|:---:|:---:|:---:|
| `gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf.tar.xz` | 78 MB | ARM 交叉编译工具链（Linaro 4.9.4） |
| `rootfs.tar.bz2` | 224 MB | 开发板 rootfs（含 Qt 5.12.9 运行库） |
| `ncnn-build-arm-install.tar.xz` | 14 MB | NCNN ARM 预编译库（头文件 + 静态库） |

> 📥 **下载地址**：<https://pan.baidu.com/s/1PhoYNQhfvUq1aRRESzWR4w>（提取码：`tzeh`）
>
> **使用说明**：
> - 工具链解压到 `/usr/local/arm/`，并添加到 PATH
> - rootfs 用于开发板系统恢复或 NFS 挂载参考
> - NCNN 库解压后用于交叉编译 VisionPass 主程序
> - AI 模型文件（MobileFaceNet、Haar Cascade）已包含在仓库 `model/` 目录中
> - Qt 5.12 ARM 交叉编译产物需自行编译，详见 [docs/部署手册.md](docs/部署手册.md)

---

## 🛠️ 开发工具

### PC 端工具

| 工具 | 路径 | 功能 |
|:---:|:---:|:---:|
| **user_manager** | `tools/user_manager/` | 用户注册、人脸照片导入、RFID 卡管理、数据库同步 |
| **message_manager** | `tools/message_manager/` | 文字/语音消息收发、设备自动发现 |
| **deploy-to-board.sh** | `tools/deploy-to-board.sh` | 一键部署所有文件到开发板 |

### 调试命令

```bash
# 查看内核驱动日志
dmesg | grep -E "rc522|servo|ir_sensor"

# 测试 RFID 模块
cat /dev/rc522

# 测试舵机
echo "90" > /dev/servo

# 测试 IR 传感器
cat /dev/ir_sensor

# 查看摄像头设备
v4l2-ctl --list-devices

# 录音测试（WM8960）
arecord -D hw:0,0 -f S16_LE -r 16000 -c 1 -d 3 test.wav
aplay test.wav
```

---

## 🗺️ 开发路线

- [x] **Phase 1** — 开发环境搭建、交叉编译工具链配置
- [x] **Phase 2** — 内核驱动开发（RC522 / SG90 / IR Sensor）
- [x] **Phase 3** — AI 推理集成（NCNN + MobileFaceNet）
- [x] **Phase 4** — 语音识别（DTW + MFCC 特征提取）
- [x] **Phase 5** — 应用集成（Qt GUI + 五种开锁方式）
- [ ] **Phase 6** — 系统优化（开机自启、性能调优、安全加固）

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request。对于重大变更，请先开 Issue 讨论。

### 开发规范

- 所有 C/C++ 代码必须包含中文注释
- 驱动代码遵循 Linux 内核编码规范
- 用户空间代码遵循 Qt/C++17 规范
- 提交信息格式：`<type>: <description>`（如 `feat: 添加语音开锁功能`）

---

## 📄 许可证

本项目基于 [MIT License](LICENSE) 开源。

原版参考项目：[VisionPass](https://github.com/happyeverydaybest-eng/VisionPass)（仅供学习参考）

---

<p align="center">
  <sub>Built with ❤️ for Embedded Linux & AI</sub>
</p>
