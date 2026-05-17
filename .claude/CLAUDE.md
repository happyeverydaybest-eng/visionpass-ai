# VisionPass AI门禁系统 — 项目上下文

## 项目概述
基于正点原子阿尔法开发板（I.MX6ULL）的AI智能门禁系统，支持人脸识别、RFID刷卡、语音解锁、密码开锁、物理按键5种开锁方式。所有AI推理在开发板端侧运行（NCNN + Sherpa-ONNX），不依赖上位机。

## 硬件
- 开发板：正点原子阿尔法 I.MX6ULL（ARM Cortex-A7 @528MHz, 512MB DDR3）
- 板载传感器：ICM20608（六轴）、AP3216C（红外+距离+光照）、WM8960（音频）、蜂鸣器、功能按钮
- 外接模块：OV2640摄像头、RC522 RFID、SG90舵机、红外避障模块（门检测）

## 关键源码路径
- 内核源码：~/linux/IMX6ULL/linux/alientek_linux_2026_4_26/（Linux 4.1.15）
- U-Boot源码：~/linux/IMX6ULL/uboot/alientek_uboot_2026_4_26/
- 设备树文件：arch/arm/boot/dts/imx6ull-14x14-evk.dts（直接在内核源码中修改）
- 原版参考源码：~/linux/visionpass/VisionPass-fork/
- 原版编译成品：~/linux/visionpass/VisionPass/

## 交叉编译
- 工具链：arm-linux-gnueabihf-gcc（Linaro 4.9.4，路径 /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin/）
- cmake工具链文件：~/linux/visionpass/config/arm-linux-gnueabihf.toolchain.cmake
- Qt ARM：~/linux/tool/sysroot-combined/opt/qt5.12-arm/ ✅ Qt 5.12.12 ARM已编译完成
  - qmake路径：~/linux/tool/sysroot-combined/opt/qt5.12-arm/bin/qmake
  - 开发板rootfs已有Qt 5.12.9运行库，程序可直接运行（版本兼容）
  - sysroot-combined路径：~/linux/tool/sysroot-combined/（包含工具链头文件+Qt编译产物+rootfs运行库）
- NCNN ARM：~/ncnn/build-arm/install/ ✅ 已编译完成
- Sherpa-ONNX ARM：暂不编译（GCC 4.9.4不支持C++17，留作将来升级）

## AI方案
- 人脸识别：NCNN + MobileFaceNet INT8量化模型（~400KB，~200ms推理）
- 语音解锁：DTW模板匹配（MFCC特征+动态时间规整，无外部依赖，GCC 4.9.4兼容）
  Sherpa-ONNX留作将来升级（需要GCC 7+或更强开发板）
- 人脸检测：OpenCV Haar级联（开发板端运行）
- 人脸数据库：SQLite（/opt/visionpass/data/users.db）
- 系统配置：JSON文件（/opt/visionpass/config/system.json）

## 不使用的技术（与原版区别）
- 不用ADS1115 ADC → 用板载AP3216C + 红外模块（GPIO数字信号）
- 不用AT24C64 EEPROM → 用文件系统（JSON + SQLite）
- 不用MPU6050 → 用板载ICM20608（SPI3，寄存器地址不同）
- 不用PC端Dlib推理 → 用开发板端NCNN推理

## 设备树需要添加的节点（在内核源码中直接修改）
1. ECSPI4 + RC522 RFID子节点
2. PWM3使能（SG90舵机）
3. GPIO节点（红外模块，门检测）

## 编码规范
- 所有C/C++代码必须有中文注释，解释关键逻辑
- 驱动代码遵循Linux内核编码规范
- 用户空间代码遵循Qt/C++17规范
- 遇到内核4.1.15旧API需说明与新版本区别

## 开发流程
1. 先给出实现方案，再写代码
2. 代码完成后提供交叉编译命令
3. 提供部署到开发板的步骤
4. 提供测试方法和预期结果
5. 用户是嵌入式Linux初学者，需要详细解释每一步

## 项目文档
- docs/00-正点原子阿尔法开发板硬件资源.md
- docs/01-项目分析.md（原版技术架构分析）
- docs/02-复刻指南.md（原版复刻步骤）
- docs/03-硬件资源购买清单.md（最终版，不含ADS1115/EEPROM）
- docs/04-基于官方源码的AI Agent开发指南.md（完整开发计划）
- docs/TODO.md（任务清单）