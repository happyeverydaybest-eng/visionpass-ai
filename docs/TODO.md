# VisionPass AI门禁系统 — TODO清单

> 最后更新：2026-06-01

## Phase 1: 环境准备 + 设备树 ✅

### 环境搭建
- [x] **安装系统包**（cmake, libsqlite3-dev, qtbase5-dev, libopencv-dev, nfs-kernel-server）
- [x] **验证交叉编译工具链**（Linaro 4.9.4，PATH已生效）
- [x] **创建cmake交叉编译工具链文件**（config/arm-linux-gnueabihf.toolchain.cmake）
- [x] **Qt ARM交叉编译**（Qt 5.12.12 ARM 已编译完成，路径 ~/linux/tool/sysroot-combined/opt/qt5.12-arm/）
- [x] **NCNN交叉编译**（~/ncnn/build-arm/install/，已验证ARM架构）
- [x] **Sherpa-ONNX暂不编译**（GCC 4.9.4不支持C++17，语音解锁改用DTW方案）
- [x] **CLAUDE.md创建**（.claude/CLAUDE.md，项目上下文已写入）
- [x] **原始设备树备份**（imx6ull-14x14-evk.dts.original）

### 设备树修改
- [ ] **修改 imx6ull-14x14-evk.dts**
  - 添加ECSPI3控制器节点 + RC522 RFID子节点
  - 使能PWM节点（SG90舵机，GPIO1_IO27）
  - 添加GPIO节点（IR传感器，GPIO5_IO02）
- [ ] **编译设备树**
- [ ] **烧录到开发板**
- [ ] **验证设备树**

## Phase 2: 驱动开发 ✅

- [x] **RC522 RFID驱动** — `drivers/rc522/rc522.ko`，SPI字符设备，ECSPI3 + GPIO1_IO26
- [x] **SG90舵机驱动** — `drivers/servo/servo.ko`，软件PWM 50Hz，GPIO1_IO27
- [x] **IR红外传感器驱动** — `drivers/ir_sensor/ir_sensor.ko`，GPIO5_IO02 字符设备
- [x] **ICM20608振动监测** — 板载SPI3驱动（内核自带）

## Phase 3: AI推理部署 ✅

- [x] **NCNN交叉编译验证**（~/ncnn/build-arm/install/）
- [x] **MobileFaceNet模型准备**（FP32 4MB + FP16 2MB，在 model/ 目录）
- [x] **Haar级联分类器**（haarcascade_frontalface_alt2.xml）
- [x] **FaceDetector** — OpenCV Haar 级联人脸检测
- [x] **FaceRecognizer** — NCNN MobileFaceNet 512维特征提取 + 余弦相似度匹配
- [x] **FaceProcessThread** — 生产者-消费者模式的人脸处理线程

## Phase 4: 语音解锁 ✅

- [x] **DTW模板匹配方案开发**（纯C++，无外部依赖，GCC 4.9.4兼容）
- [x] **WM8960 ALSA录音测试**（16kHz/16bit/单声道）
- [x] **MFCC特征提取实现**（13维MFCC + 一阶/二阶差分 = 39维）
- [x] **VoiceThread** — 语音监听线程，识别"开门"指令

## Phase 5: 应用集成 ✅

### 核心模块
- [x] **SystemController** — 中央状态机，管理所有模块生命周期
- [x] **V4L2CaptureThread** — OV2640 CSI 视频采集（640×480 RGB565）
- [x] **ServoControl** — 舵机开锁/关锁控制
- [x] **BeeperControl** — 蜂鸣器提示音
- [x] **IRSensorMonitor** — IR传感器人体检测，自动触发人脸识别
- [x] **RFIDThread** — RFID刷卡轮询 + UID识别（暂时禁用）
- [x] **ButtonMonitor** — 物理按键开锁（KEY0, GPIO1_IO18）

### 数据库
- [x] **UserDatabase** — SQLite 用户管理（users + face_features + rfid_cards 三表）
- [x] **数据库迁移** — 旧版单表自动迁移到新三表结构

### UI组件
- [x] **MainWindow** — 主界面（1024×600，视频显示 + 功能按钮）
- [x] **PasswordDialog** — 密码输入对话框
- [x] **NotificationOverlay** — 通知浮层（自适应高度）
- [x] **MessageDialog** — 全屏消息对话框（文字/语音消息）
- [x] **TouchKeyboard** — 自定义触屏键盘（QWERTY布局）
- [x] **VoiceRecordButton** — 按住录音按钮（ALSA + 软件增益降噪）

### 网络通信
- [x] **MessageClient** — TCP/UDP 消息客户端（设备端）
  - TCP 连接到 PC 端 message_manager（端口 9500）
  - UDP 设备发现监听（端口 9501/9503）
  - 支持文字消息 + Base64编码语音消息

### PC端工具
- [x] **user_manager** — 用户管理 GUI（tools/user_manager/）
  - 人脸照片导入、RFID卡管理、数据库同步
  - SSH 连接测试 + SCP 文件传输
- [x] **message_manager** — 消息管理 GUI（tools/message_manager/）
  - TCP 服务器（端口 9500）+ UDP 设备发现（端口 9501）
  - 文字/语音消息收发

### 构建系统
- [x] **CMakeLists.txt** — 完整的交叉编译配置
- [x] **deploy-to-board.sh** — 一键部署脚本

## Phase 6: 优化拓展（进行中）

- [x] **信号处理优化** — volatile sig_atomic_t + QTimer 模式（异步信号安全）
- [x] **TCP大消息支持** — 按客户端分包缓冲，换行分隔消息帧
- [x] **语音降噪** — 降低硬件ALSA增益 + 软件4倍增益
- [ ] **开机自启配置** — systemd service 文件
- [ ] **RFID功能恢复** — RF卡问题解决后取消 #if 0
- [ ] **安全加固** — AES密钥随机化、动态IV、密钥轮换
- [ ] **性能优化** — 识别帧率优化、模型量化（INT8）
- [ ] **拓展方向** — 人脸识别活体检测、多用户并发、云端管理

## 已知问题

| 问题 | 状态 | 说明 |
|:---|:---:|:---|
| RF卡读取不稳定 | 暂时禁用 | RFID 初始化已注释，待排查硬件 |
| GCC 4.9.4 不支持 C++17 | 已绕过 | 语音用 DTW 替代 Sherpa-ONNX |
| 触摸屏 tslib 未生效 | 已解决 | 改用 evdev 输入方式 |
