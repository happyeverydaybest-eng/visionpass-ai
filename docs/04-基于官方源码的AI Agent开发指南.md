# VisionPass AI门禁系统 — 基于官方源码的AI Agent开发指南

> **硬件**：正点原子阿尔法开发板（I.MX6ULL）
> **内核**：Linux 4.1.15（`alientek_linux_2026_4_26`）
> **U-Boot**：U-Boot 2016.03（`alientek_uboot_2026_4_26`）
> **AI推理**：端侧部署（NCNN + MobileFaceNet）
> **语音解锁**：Sherpa-ONNX 离线关键词识别
> **开发模式**：AI Agent主导编码，你负责架构审查和硬件测试

---

## 目录

- [一、项目目标与新架构](#一项目目标与新架构)
- [二、AI Agent开发模式：你的角色与工作流](#二ai-agent开发模式你的角色与工作流)
- [三、环境准备（1-2天）](#三环境准备1-2天)
- [四、内核与设备树修改（第1阶段）](#四内核与设备树修改第1阶段)
- [五、驱动开发（第2阶段）](#五驱动开发第2阶段)
- [六、交叉编译AI推理引擎（第3阶段）](#六交叉编译ai推理引擎第3阶段)
- [七、用户空间应用开发（第4阶段）](#七用户空间应用开发第4阶段)
- [八、语音解锁模块（第5阶段）](#八语音解锁模块第5阶段)
- [九、部署与联调（第6阶段）](#九部署与联调第6阶段)
- [十、训练数据集与模型准备](#十训练数据集与模型准备)

---

## 一、项目目标与新架构

### 1.1 与原版的核心区别

| 对比项 | 原版VisionPass | 你的版本 |
|--------|---------------|---------|
| 人脸识别推理 | PC端（上位机） | **开发板端侧运行** |
| 通信方式 | C/S架构，TCP+UDP | **单机运行**，可选MQTT上报云端 |
| 视频显示 | 需要PC客户端 | 开发板接LCD屏直接显示 |
| 语音解锁 | 无 | **新增：离线关键词识别** |
| ADC模块 | 需要外部ADS1115 | 用GPIO+板载传感器替代 |
| EEPROM | 外部AT24C64 | 文件系统+RFID卡替代 |
| 陀螺仪 | MPU6050 | 板载ICM20608（已有） |
| 开发模式 | 人工编写 | **AI Agent主导，你审查** |

### 1.2 新系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                    I.MX6ULL 开发板                            │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              Qt GUI 应用（主程序）                      │   │
│  │                                                      │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │   │
│  │  │ 视频采集  │  │ 界面显示  │  │   开锁方式管理    │   │   │
│  │  │ V4L2     │  │ LCD/HDMI │  │                  │   │   │
│  │  └────┬─────┘  └──────────┘  └────┬───┬───┬──┬──┘   │   │
│  │       │                            │   │   │  │      │   │
│  │  ┌────▼────────────────────────────┘   │   │  │      │   │
│  │  │  AI推理引擎（NCNN）                  │   │  │      │   │
│  │  │  MobileFaceNet INT8                 │   │  │      │   │
│  │  │  → 人脸检测 → 特征提取 → 匹配        │   │  │      │   │
│  │  └─────────────────────────────────────┘   │  │      │   │
│  │                                             │  │      │   │
│  │  ┌─────────────────────────────────────────┘  │      │   │
│  │  │  语音识别引擎（Sherpa-ONNX）               │      │   │
│  │  │  KWS关键词识别："开门"、"关门"              │      │   │
│  │  │  WM8960音频采集 → MFCC → 匹配              │      │   │
│  │  └────────────────────────────────────────────┘      │   │
│  │                                                      │   │
│  │  ┌─────────────────────┐  ┌────────────────────┐    │   │
│  │  │ SQLite人脸数据库     │  │ JSON用户配置文件    │    │   │
│  │  │ id|name|descriptor   │  │ 密码|AES密钥|参数   │    │   │
│  │  └─────────────────────┘  └────────────────────┘    │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              硬件外设层                                 │   │
│  │                                                      │   │
│  │  CSI→OV2640   SPI3→ICM20608(板载)  I2C1→AP3216C(板载)│   │
│  │  SPI4→RC522   PWM3→SG90            GPIO→门磁         │   │
│  │  I2C1→AT24C64 SAI2→WM8960(板载)   GPIO→按键(板载)   │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 1.3 开锁方式（5种）

| 开锁方式 | 硬件 | 验证逻辑 | AI参与 |
|---------|------|---------|--------|
| **人脸识别** | OV2640摄像头 | NCNN MobileFaceNet特征匹配 | ✅ 端侧推理 |
| **RFID刷卡** | RC522模块 | 读取Mifare卡UID → 匹配数据库 | ❌ |
| **语音解锁** | WM8960麦克风（板载） | Sherpa-ONNX关键词识别 | ✅ 端侧推理 |
| **密码开锁** | Qt虚拟键盘 | SHA-256哈希比对 | ❌ |
| **物理按键** | 板载功能按钮 | GPIO中断 → 确认开锁 | ❌ |

### 1.4 需要修改的源码范围

| 源码树 | 修改文件 | 修改内容 |
|--------|---------|---------|
| **内核DTS** | `imx6ull-14x14-evk.dts` | 添加RC522(SPI4)、PWM3、AT24C64设备节点 |
| **内核驱动** | `drivers/char/` | 新增RC522字符设备驱动 |
| **内核配置** | `.config` | 确保V4L2/PWM/I2C/WM8960/SQLite已启用 |
| **U-Boot** | 不需要修改 | 出厂配置已足够 |
| **用户空间** | 新项目目录 | Qt应用 + NCNN + Sherpa-ONNX |

**U-Boot不需要修改**，因为不需要新增启动参数或boot命令。原有的U-Boot配置可以正常加载你的内核和设备树。

---

## 二、AI Agent开发模式：你的角色与工作流

### 2.1 你的角色：嵌入式AI架构师 + 硬件工程师

```
你的职责：
├── 架构决策    → 选择什么模型、什么框架、什么通信方式
├── 代码审查    → AI写的代码你要能看懂，能指出问题
├── 硬件测试    → 把代码部署到开发板上，看现象、调硬件
├── 问题反馈    → 把现象描述清楚反馈给AI，让它修复
└── 学习记录    → 每个阶段写笔记，形成知识积累
```

### 2.2 AI的角色：嵌入式软件工程师 + 驱动开发专家

```
AI的职责：
├── 写驱动代码   → 字符设备、设备树配置
├── 写应用代码   → Qt GUI、NCNN推理、网络通信
├── 编译脚本     → CMake/Makefile、交叉编译配置
├── 调试排错     → 根据日志分析问题根因
├── 写测试代码   → 单元测试、硬件测试脚本
└── 文档编写     → 注释、API文档、部署手册
```

### 2.3 开发工作流（Vibe Coding循环）

```
┌─────────────┐
│ 你下达指令   │ ← 用自然语言描述要做什么，比如：
│             │    "给RC522写一个SPI字符设备驱动，
│  (Prompt)   │     设备节点/dev/rc522，支持ioctl读写"
└──────┬──────┘
       ▼
┌─────────────┐
│ AI写代码     │ ← AI生成完整代码 + Makefile + 测试脚本
│             │
│  (Generate)  │
└──────┬──────┘
       ▼
┌─────────────┐
│ 你审查代码   │ ← 检查：逻辑对不对？有没有安全隐患？
│             │    有问题就告诉AI改，没问题就下一步
│  (Review)   │
└──────┬──────┘
       ▼
┌─────────────┐
│ 编译+部署   │ ← 交叉编译，拷贝到开发板，加载驱动
│             │
│  (Deploy)   │
└──────┬──────┘
       ▼
┌─────────────┐
│ 硬件测试    │ ← 实际操作硬件，观察现象
│             │    成功→进入下一个模块
│  (Test)     │    失败→记录现象，返回给AI修bug
└──────┬──────┘
       ▼
  循环下一个模块
```

### 2.4 推荐的AI工具和配置

| 工具 | 用途 | 安装方式 |
|------|------|---------|
| **Claude Code**（你在用的） | 主开发Agent，写代码+调试+文档 | 已安装 |
| **Claude Web** | 复杂问题的深度思考 | claude.ai |
| **浏览器** | 查阅芯片数据手册 | - |

**给Claude Code的配置建议**：

在项目的 `.claude/CLAUDE.md` 中写入以下内容，让AI每次都知道项目背景：

```markdown
# VisionPass AI门禁系统 - 项目上下文

- 硬件：正点原子阿尔法开发板（I.MX6ULL, ARM Cortex-A7, 528MHz）
- 内核：Linux 4.1.15，源码在 ~/linux/IMX6ULL/linux/alientek_linux_2026_4_26/
- 交叉编译器：arm-linux-gnueabihf-gcc
- Qt版本：5.12，arm平台
- AI推理：NCNN + MobileFaceNet INT8量化模型
- 语音：Sherpa-ONNX KWS（关键词识别）
- 用户是嵌入式Linux初学者，需要详细解释每一步
- 所有代码必须写注释，所有命令要说明作用
```

### 2.5 你下达指令的模板

每次让AI写代码时，用这个模板：

```
模块：[模块名]
目标：[要做什么]
参考：[相关文件路径]
要求：[具体技术要求]
约束：[硬件限制、性能要求等]
```

示例：
```
模块：RC522驱动
目标：写一个SPI字符设备驱动，实现RFID卡片读取
参考：内核源码 drivers/spi/，正点原子SPI驱动示例
要求：设备节点/dev/rc522，ioctl支持读/写/寻卡命令
约束：使用ECSPI4控制器，GPIO片选
```

### 2.6 每个阶段的AI参与度

| 阶段 | AI写代码 | AI调试 | 你的工作 |
|------|---------|--------|---------|
| 设备树修改 | 90% | 70% | 审查+烧录 |
| 驱动开发 | 80% | 80% | 测试+反馈现象 |
| NCNN交叉编译 | 60% | 70% | 解决编译错误 |
| 应用开发 | 70% | 60% | 架构审查+功能测试 |
| 语音模块 | 70% | 70% | 录音测试+调参 |
| 联调部署 | 40% | 80% | 全功能测试 |

---

## 三、环境准备（1-2天）

### 3.1 交叉编译工具链

```bash
# 1. 确认工具链位置（正点原子提供）
# 通常在以下位置之一：
ls /usr/local/arm/
# 或
ls ~/tools/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/

# 2. 添加到PATH（写入~/.bashrc持久化）
echo 'export PATH=/usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/bin:$PATH' >> ~/.bashrc
source ~/.bashrc

# 3. 验证
arm-linux-gnueabihf-gcc --version
# 应该输出 gcc version 4.9.4 或类似
```

### 3.2 Qt for ARM 编译

```bash
# 正点原子通常提供了编译好的Qt for ARM
# 如果没有，需要自己编译：

# 1. 下载Qt 5.12源码
wget https://download.qt.io/archive/qt/5.12/5.12.12/single/qt-everywhere-src-5.12.12.tar.xz
tar -xf qt-everywhere-src-5.12.12.tar.xz

# 2. 配置交叉编译
cd qt-everywhere-src-5.12.12
./configure -prefix /opt/qt5.12-arm \
    -release \
    -opensource \
    -no-pch \
    -xplatform linux-arm-gnueabi-g++ \
    -linuxfb \
    -qt-libjpeg \
    -qt-libpng \
    -qt-freetype \
    -no-opengl \
    -nomake examples \
    -nomake tests \
    -skip qtwebengine \
    -confirm-license

# 3. 编译（耗时约2-4小时）
make -j$(nproc)
sudo make install
```

### 3.3 NCNN 交叉编译

```bash
# 1. 下载NCNN源码
git clone https://github.com/Tencent/ncnn.git
cd ncnn
git checkout 20240421  # 稳定版本

# 2. 创建交叉编译工具链文件
cat > arm-linux-gnueabihf.toolchain.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_FIND_ROOT_PATH /usr/arm-linux-gnueabihf)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF

# 3. 配置并编译
mkdir build-arm && cd build-arm
cmake -DCMAKE_TOOLCHAIN_FILE=../arm-linux-gnueabihf.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$(pwd)/install \
      -DNCNN_VULKAN=OFF \
      -DNCNN_OPENMP=OFF \
      -DNCNN_BUILD_TOOLS=ON \
      -DNCNN_BUILD_EXAMPLES=OFF \
      ..
make -j$(nproc)
make install

# 编译成功后，install/目录下会有：
#   lib/libncnn.a          - 静态库
#   include/ncnn/          - 头文件
#   bin/                   - 转换工具
```

### 3.4 Sherpa-ONNX 交叉编译

```bash
# 1. 下载源码
git clone https://github.com/k2-fsa/sherpa-onnx.git
cd sherpa-onnx

# 2. 交叉编译（类似NCNN的方式）
mkdir build-arm && cd build-arm
cmake -DCMAKE_TOOLCHAIN_FILE=../arm-linux-gnueabihf.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$(pwd)/install \
      -DBUILD_SHARED_LIBS=ON \
      -DSHERPA_ONNX_ENABLE_TESTS=OFF \
      -DSHERPA_ONNX_ENABLE_PYTHON=OFF \
      -DSHERPA_ONNX_ENABLE_CHECK=OFF \
      ..
make -j$(nproc)
make install
```

### 3.5 开发板NFS挂载（可选但强烈推荐）

开发阶段频繁部署文件到开发板，用NFS挂载最快：

```bash
# PC端安装NFS服务器
sudo apt install nfs-kernel-server

# 配置共享目录
echo '/home/viper/linux/visionpass *(rw,sync,no_root_squash,no_subtree_check)' | sudo tee -a /etc/exports
sudo exportfs -ra

# 开发板挂载
# 串口登录开发板后：
mount -t nfs -o nolock <PC_IP>:/home/viper/linux/visionpass /mnt/nfs

# 这样开发板上直接访问/mnt/nfs就能运行你的程序
# 修改PC端代码后立即生效，无需scp
```

### 3.6 项目目录结构

在开始编码前，创建好项目目录：

```bash
mkdir -p ~/linux/visionpass/{kernel_patches,drivers,app,model,voice,config,deploy}
```

| 目录 | 用途 |
|------|------|
| `kernel_patches/` | 设备树修改对比文件 |
| `drivers/` | 自定义驱动源码（RC522等） |
| `app/` | Qt应用主程序 |
| `model/` | NCNN模型文件 |
| `voice/` | Sherpa-ONNX语音模型 |
| `config/` | 配置文件（JSON/INI） |
| `deploy/` | 部署脚本 |

---

## 四、内核与设备树修改（第1阶段）

### 4.1 你需要修改的文件

```
linux/alientek_linux_2026_4_26/arch/arm/boot/dts/imx6ull-14x14-evk.dts
```

这是正点原子阿尔法板的主设备树文件，所有硬件配置都在这里定义。

### 4.2 需要添加的设备树节点

| 外设 | 添加到哪个节点 | 说明 |
|------|--------------|------|
| RC522 RFID | `&ecspi4` 节点（新增） | 使用ECSPI4控制器，避免和ICM20608冲突 |
| SG90舵机 | `&pwm3` 节点（新增使能） | PWM3默认未启用，需要打开 |
| AT24C64 | `&i2c1` 子节点 | I2C EEPROM，内核自带at24驱动 |

### 4.3 具体修改内容（让AI Agent完成）

**下达给AI的指令模板：**

```
模块：设备树修改
目标：修改 imx6ull-14x14-evk.dts，添加以下外设节点：
  1. ECSPI4控制器，挂载RC522 RFID模块（compatible="alientek,rc522"）
  2. PWM3控制器，使能（用于SG90舵机）
  3. I2C1下添加AT24C64 EEPROM节点（compatible="atmel,24c64"）
参考：
  - 现有设备树文件：arch/arm/boot/dts/imx6ull-14x14-evk.dts
  - SoC定义文件：arch/arm/boot/dts/imx6ull.dtsi
  - 已有的ECSPI3配置作为参考
  - pinctrl_pwm1配置作为PWM引脚参考
要求：
  - 使用diff格式给出修改
  - 添加注释说明每个节点的作用
  - 引脚配置需要查imx6ull.dtsi确认复用关系
约束：
  - 不要修改已有节点的配置（除了添加新子节点）
  - SPI4片选用一个未使用的GPIO引脚
```

**预期修改效果（AI会帮你写出具体代码）：**

```dts
/* ===== 添加ECSPI4（RC522 RFID） ===== */
&ecspi4 {
    fsl,spi-num-chipselects = <1>;
    cs-gpio = <&gpio4 15 GPIO_ACTIVE_LOW>;  /* 示例引脚，需确认可用性 */
    pinctrl-names = "default";
    pinctrl-0 = <&pinctrl_ecspi4>;
    status = "okay";

    rc522@0 {
        compatible = "alientek,rc522";
        spi-max-frequency = <8000000>;
        reg = <0>;
    };
};

/* ===== 添加PWM3（SG90舵机） ===== */
&pwm3 {
    pinctrl-names = "default";
    pinctrl-0 = <&pinctrl_pwm3>;
    status = "okay";
};

/* ===== I2C1下添加AT24C64 ===== */
&i2c1 {
    /* ... 已有节点保持不变 ... */

    /* AT24C64 EEPROM */
    at24c64@50 {
        compatible = "atmel,24c64";
        reg = <0x50>;
        pagesize = <32>;
    };
};
```

### 4.4 编译设备树

```bash
cd ~/linux/IMX6ULL/linux/alientek_linux_2026_4_26

# 方法一：完整编译内核+DTB（第一次需要）
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- imx6ull-14x14-evk.dtb

# 方法二：只编译设备树（后续修改时更快）
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- dtbs

# 编译生成的文件位置
ls arch/arm/boot/dts/imx6ull-14x14-evk.dtb
```

### 4.5 烧录到开发板

```bash
# 方法一：通过TFTP烧录到SD卡/eMMC
# 将.dtb文件放到TFTP目录
cp arch/arm/boot/dts/imx6ull-14x14-evk.dtb /tftpboot/

# 在开发板U-Boot中：
tftp 80800000 imx6ull-14x14-evk.dtb
# 然后boot

# 方法二：直接替换SD卡/EMMC上的dtb文件
# 将.dtb拷贝到开发板启动分区
scp arch/arm/boot/dts/imx6ull-14x14-evk.dtb root@<开发板IP>:/boot/

# 方法三：NFS挂载直接读取（开发阶段推荐）
# U-Boot设置从NFS加载dtb
```

### 4.6 验证设备树

```bash
# 开发板启动后验证
# 1. 检查设备树是否正确加载
cat /proc/device-tree/model

# 2. 检查I2C设备
i2cdetect -y 0
# 应该看到：0x50(AT24C64), 0x1a(WM8960), 0x1e(AP3216C)

# 3. 检查SPI设备
ls /dev/spi*
# 应该有SPI4对应的设备

# 4. 检查PWM
ls /sys/class/pwm/
# 应该有pwmchipX（X取决于pwm3的编号）

# 5. 检查摄像头
ls /dev/video*
# 应该有video0或video1
```

---

## 五、驱动开发（第2阶段）

### 5.1 驱动开发优先级

```
优先级1（必须）：RC522 RFID驱动   ← 没有这个无法刷卡开锁
优先级2（必须）：SG90舵机控制      ← 没有这个无法执行开锁
优先级3（建议）：AT24C64 EEPROM   ← 可用文件替代，但学习价值高
优先级4（可选）：门磁检测          ← 可用GPIO替代
```

### 5.2 RC522字符设备驱动（重点）

这是最需要AI协助的部分，也是学习价值最高的。

**下达给AI的指令模板：**

```
模块：RC522 RFID驱动
目标：编写Linux字符设备驱动，通过SPI4控制RC522模块
参考：
  - 内核驱动框架：drivers/spi/spidev.c
  - 字符设备框架：Documentation/char-devices.txt
  - VisionPass原项目：VisionPass-fork/driver/rc522/rc522.c
  - RC522数据手册（网上搜索）
要求：
  1. 字符设备，主设备号动态分配，设备名"rc522"
  2. ioctl接口：
     - IOCTL_RC522_RESET    → 复位RC522
     - IOCTL_RC522_REQUEST  → 寻卡
     - IOCTL_RC522_ANTICOLL → 防冲突，获取UID
     - IOCTL_RC522_SELECT   → 选卡
     - IOCTL_RC522_AUTH     → 认证密钥
     - IOCTL_RC522_READ     → 读块
     - IOCTL_RC522_WRITE    → 写块
  3. 使用SPI子系统API（spi_write/spi_read），不是GPIO模拟
  4. 驱动加载后生成/dev/rc522设备节点
  5. 包含MODULE_LICENSE("GPL")
约束：
  - 内核版本4.1.15，API可能和新内核不同
  - SPI控制器是ECSPI4
  - 需要处理SPI片选
输出：
  - rc522.c（驱动源码）
  - Makefile（编译驱动）
  - rc522_test.c（测试程序）
```

### 5.3 SG90舵机（用sysfs PWM，不需要写驱动）

Linux 4.1内核自带PWM sysfs接口，**不需要写内核驱动**，用户空间直接操作即可：

```bash
# 1. 找到PWM芯片编号
ls /sys/class/pwm/
# 假设是pwmchip2

# 2. 导出PWM通道
echo 0 > /sys/class/pwm/pwmchip2/export

# 3. 配置PWM周期（20ms = 20000000ns）
echo 20000000 > /sys/class/pwm/pwmchip2/pwm0/period

# 4. 配置占空比
# 0度：0.5ms高电平 = 500000ns
echo 500000 > /sys/class/pwm/pwmchip2/pwm0/duty_cycle

# 90度：1.5ms高电平 = 1500000ns
echo 1500000 > /sys/class/pwm/pwmchip2/pwm0/duty_cycle

# 5. 启用PWM
echo 1 > /sys/class/pwm/pwmchip2/pwm0/enable
```

在Qt代码中，直接操作这些sysfs文件即可：

```cpp
// Qt代码示例
void ServoControl::setAngle(int angle) {
    // angle: 0-90
    // 映射：0°→500000ns, 90°→2500000ns
    int dutyCycle = 500000 + (angle * 2000000 / 90);

    QFile periodFile("/sys/class/pwm/pwmchip2/pwm0/period");
    if (periodFile.open(QIODevice::WriteOnly)) {
        periodFile.write("20000000");
        periodFile.close();
    }

    QFile dutyFile("/sys/class/pwm/pwmchip2/pwm0/duty_cycle");
    if (dutyFile.open(QIODevice::WriteOnly)) {
        dutyFile.write(QString::number(dutyCycle).toUtf8());
        dutyFile.close();
    }
}
```

### 5.4 AT24C64 EEPROM（内核自带驱动）

内核已经有 `at24` 驱动，设备树配置好后直接使用：

```bash
# 方法一：通过sysfs
cat /sys/bus/i2c/devices/0-0050/eeprom   # 读取
echo -ne "\x00\x01" > /dev/i2c-0          # 通过i2c-tools写入

# 方法二：在Qt代码中通过I2C设备文件操作
# open("/dev/i2c-0", O_RDWR) → ioctl设置地址 → read/write
```

### 5.5 驱动编译与加载

```bash
# 1. 编译驱动模块
cd ~/linux/visionpass/drivers/rc522
make
# 生成 rc522.ko

# 2. 拷贝到开发板
scp rc522.ko root@<开发板IP>:/lib/modules/4.1.15/

# 3. 在开发板上加载
ssh root@<开发板IP>
insmod /lib/modules/4.1.15/rc522.ko

# 4. 验证
lsmod           # 应该看到rc522模块
ls /dev/rc522   # 应该看到设备节点
dmesg | tail    # 应该看到驱动加载日志
```

---

## 六、交叉编译AI推理引擎（第3阶段）

### 6.1 NCNN 交叉编译详解

NCNN是腾讯开源的轻量级神经网络推理框架，**专为ARM移动端优化**，无第三方依赖，非常适合I.MX6ULL。

```bash
# 完整交叉编译步骤
cd ~/ncnn

# 1. 创建工具链文件
cat > arm-gnueabi.toolchain.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# I.MX6ULL优化
set(CMAKE_C_FLAGS "-mcpu=cortex-a7 -mfpu=neon -mfloat-abi=hard -O3")
set(CMAKE_CXX_FLAGS "-mcpu=cortex-a7 -mfpu=neon -mfloat-abi=hard -O3")
EOF

# 2. 配置
mkdir -p build-arm && cd build-arm
cmake -DCMAKE_TOOLCHAIN_FILE=../arm-gnueabi.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$(pwd)/install \
      -DNCNN_VULKAN=OFF \
      -DNCNN_OPENMP=OFF \
      -DNCNN_BUILD_TOOLS=ON \
      -DNCNN_BUILD_EXAMPLES=OFF \
      -DNCNN_BUILD_BENCHMARK=OFF \
      -DNCNN_RUNTIME_CPU=OFF \
      ..

# 3. 编译
make -j$(nproc)
make install

# 4. 验证编译结果
file install/lib/libncnn.a
# 应该输出：ARM, EABI5 version 1 (SYSV), not stripped
```

### 6.2 模型准备流程

```
PC端（训练/转换）                    开发板（推理）
┌─────────────────┐              ┌─────────────────┐
│ 1. PyTorch训练   │              │                 │
│    MobileFaceNet │              │  4. NCNN加载模型  │
│                 │              │     .param文件    │
│ 2. 导出ONNX     │              │     .bin文件      │
│    model.onnx   │              │                 │
│                 │              │  5. INT8推理      │
│ 3. NCNN转换     │── scp ──→    │     速度快30%     │
│    model.param  │              │                 │
│    model.bin    │              │  6. 人脸匹配      │
│                 │              │     欧氏距离<0.6  │
└─────────────────┘              └─────────────────┘
```

### 6.3 MobileFaceNet 说明

| 特性 | 数值 |
|------|------|
| 模型大小 | ~1.5MB（FP32）/ ~400KB（INT8） |
| 参数量 | ~1M |
| FLOPs | ~140M |
| 特征维度 | 512维 |
| 准确率（LFW） | ~99.4% |
| I.MX6ULL推理时间 | ~200-400ms（单帧） |

**为什么要用INT8量化？**
I.MX6ULL没有GPU/NPU，纯CPU推理。FP32推理一帧大约400-600ms，INT8量化后可以降到200-300ms，体验更流畅。

### 6.4 模型转换步骤（在PC上完成）

```bash
# 1. 下载MobileFaceNet预训练模型（PyTorch格式）
# 从 https://github.com/IrvingMeng/MobileFaceNet 下载

# 2. 导出为ONNX
python export_onnx.py  # 让AI帮你写这个脚本

# 3. 使用NCNN工具转换为NCNN格式
cd ~/ncnn/build-arm/install/bin
./onnx2ncnn model.onnx mobilefacenet.param mobilefacenet.bin

# 4. INT8量化
# 准备校准数据（一些人脸图片）
mkdir calibration
# 放入100张人脸图片

# 使用ncnn2table生成量化表
./ncnn2table --param=mobilefacenet.param \
             --bin=mobilefacenet.bin \
             --images=calibration/ \
             --mean=127.5,127.5,127.5 \
             --norm=0.0078125,0.0078125,0.0078125 \
             --shape=112,112,3 \
             --thread=4 \
             --output=mobilefacenet.table

# 使用ncnn2int8量化
./ncnn2int8 --param=mobilefacenet.param \
            --bin=mobilefacenet.bin \
            --table=mobilefacenet.table \
            --output=mobilefacenet_int8

# 5. 拷贝到开发板
scp mobilefacenet_int8.param mobilefacenet_int8.bin \
     root@<开发板IP>:/opt/visionpass/model/
```

---

## 七、用户空间应用开发（第4阶段）

### 7.1 项目架构

```
~/linux/visionpass/app/
├── CMakeLists.txt              # 主构建文件
├── src/
│   ├── main.cpp                # 程序入口
│   ├── mainwindow.cpp/h        # 主窗口（视频显示+控制按钮）
│   ├── video_capture.cpp/h     # V4L2视频采集
│   ├── face_recognizer.cpp/h   # NCNN人脸识别推理
│   ├── voice_unlock.cpp/h      # Sherpa-ONNX语音解锁
│   ├── rfid_reader.cpp/h       # RC522读卡（通过/dev/rc522 ioctl）
│   ├── servo_control.cpp/h     # SG90舵机（通过sysfs PWM）
│   ├── user_manager.cpp/h      # 用户管理（SQLite+JSON）
│   ├── password_auth.cpp/h     # 密码验证（SHA-256）
│   └── vibration_monitor.cpp/h # ICM20608振动监测
├── ui/
│   └── mainwindow.ui            # Qt Designer界面
├── config/
│   └── default.json             # 默认配置
└── resources/
    └── icons.qrc                # 图标资源
```

### 7.2 开发顺序（逐个模块推进）

每个模块完成后，**让AI写测试代码，你在开发板上验证**，验证通过再进入下一个。

| 顺序 | 模块 | 完成标准 | 预计时间 |
|------|------|---------|---------|
| 1 | 视频采集（V4L2） | 能采集帧并保存为JPEG文件 | 1天 |
| 2 | 舵机控制 | sysfs操作能让舵机转0°/90° | 半天 |
| 3 | RC522驱动+读卡 | 能读到Mifare卡UID | 1-2天 |
| 4 | 密码验证 | SHA-256哈希比对正确 | 半天 |
| 5 | NCNN人脸识别 | 能识别预注册的人脸 | 2-3天 |
| 6 | 语音解锁 | 说"开门"能触发开锁 | 2-3天 |
| 7 | Qt主界面 | LCD显示视频+按钮正常 | 1-2天 |
| 8 | 振动监测 | 板载ICM20608读取加速度 | 1天 |
| 9 | 用户管理 | 人脸注册+删除功能 | 1天 |
| 10 | 集成联调 | 5种开锁方式全部可用 | 2-3天 |

### 7.3 每个模块的AI指令模板

**示例1：视频采集模块**

```
模块：V4L2视频采集
目标：编写视频采集类，从/dev/video1采集OV2640画面
参考：
  - 内核文档：Documentation/media/v4l2-core/videobuf2.rst
  - VisionPass原项目：video_server/capture_thread.cpp
要求：
  1. 类名：VideoCapture
  2. 初始化：打开/dev/video1，设置格式为RGB565，640x480
  3. mmap映射3个缓冲区
  4. 采集函数返回cv::Mat（BGR格式）
  5. 支持开始/停止控制
  6. 使用独立线程采集，不阻塞主线程
约束：
  - 内核4.1.15，V4L2 API
  - 需要RGB565到BGR的转换（OpenCV cvtColor）
  - 帧率目标15-20 FPS
```

**示例2：人脸识别模块**

```
模块：NCNN人脸识别
目标：在开发板上运行MobileFaceNet进行人脸识别
参考：
  - NCNN文档：https://github.com/Tencent/ncnn/wiki
  - NCNN示例：examples/目录中的分类示例
  - 模型文件：model/mobilefacenet_int8.param/.bin
要求：
  1. 类名：FaceRecognizer
  2. 构造函数加载NCNN模型（INT8量化版）
  3. detectFace(cv::Mat) → 返回人脸区域（使用简单的Haar级联或HOG）
  4. extractFeature(cv::Mat face_roi) → 返回512维特征向量
  5. matchFeature(std::vector<float> feature, database) → 返回匹配结果
  6. 欧氏距离阈值0.6
约束：
  - I.MX6ULL纯CPU推理
  - 单帧推理时间目标<400ms
  - 模型是INT8量化版
```

**示例3：语音解锁模块**

```
模块：Sherpa-ONNX语音解锁
目标：使用WM8960麦克风采集音频，Sherpa-ONNX做关键词识别
参考：
  - Sherpa-ONNX文档：https://k2-fsa.github.io/sherpa/onnx/
  - WM8960 ALSA设备：/dev/snd/pcmC0D0c
要求：
  1. 类名：VoiceUnlock
  2. 使用ALSA采集16kHz单声道音频
  3. Sherpa-ONNX KWS模型流式推理
  4. 关键词："开门"（open door）
  5. 检测到关键词后触发开锁信号
  6. 连续识别，不是一次性
约束：
  - I.MX6ULL纯CPU推理
  - 采样率16kHz，16bit
  - 缓冲区大小512或1024样本
```

### 7.4 CMakeLists.txt 框架

```cmake
cmake_minimum_required(VERSION 3.10)
project(VisionPass LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 交叉编译设置（在工具链文件中定义）
# cmake -DCMAKE_TOOLCHAIN_FILE=arm-linux-gnueabihf.toolchain.cmake ..

# 依赖
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network Sql Multimedia)
find_package(OpenCV REQUIRED)

# NCNN
set(NCNN_DIR ~/ncnn/build-arm/install)
include_directories(${NCNN_DIR}/include)
link_directories(${NCNN_DIR}/lib)

# Sherpa-ONNX
set(SHERPA_DIR ~/sherpa-onnx/build-arm/install)
include_directories(${SHERPA_DIR}/include)
link_directories(${SHERPA_DIR}/lib)

# 源文件
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/video_capture.cpp
    src/face_recognizer.cpp
    src/voice_unlock.cpp
    src/rfid_reader.cpp
    src/servo_control.cpp
    src/user_manager.cpp
    src/password_auth.cpp
    src/vibration_monitor.cpp
)

# 可执行文件
add_executable(visionpass ${SOURCES})

target_link_libraries(visionpass
    Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::Sql Qt5::Multimedia
    ${OpenCV_LIBS}
    ncnn
    sherpa-onnx
    pthread
)

# I.MX6ULL优化
target_compile_options(visionpass PRIVATE
    -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=hard -O3
)
```

---

## 八、语音解锁模块（第5阶段）

### 8.1 技术方案

```
麦克风（WM8960板载）
    │
    ▼
ALSA采集：16kHz, 16bit, mono
    │
    ▼
Sherpa-ONNX KWS流式推理
    │
    ├── 提取MFCC/FBank特征
    ├── 运行轻量级KWS模型
    └── 输出关键词匹配概率
    │
    ▼
概率 > 阈值 → 触发"开门"信号 → 舵机开锁
```

### 8.2 为什么选 Sherpa-ONNX

| 对比项 | Sherpa-ONNX | Porcupine | 自建FFT方案 |
|--------|------------|-----------|-----------|
| 开源免费 | ✅ | ❌（商业许可） | ✅ |
| ARM支持 | ✅ | ✅ | 需自己写 |
| 中文支持 | ✅ | ❌（仅英文） | 需自己训练 |
| I.MX6ULL可用 | ✅（CPU推理） | ✅ | ✅ |
| 离线运行 | ✅ | ✅ | ✅ |
| 易于部署 | ✅ | ✅ | ❌（复杂） |
| 模型大小 | ~5MB | ~500KB | ~100KB |

### 8.3 关键词选择

| 关键词 | 中文 | 英文映射 | 用途 |
|--------|------|---------|------|
| "开门" | kāi mén | "open" | 开锁 |
| "关门" | guān mén | "close" | 关锁（手动复位） |

Sherpa-ONNX的KWS模型对中文支持有限，更实际的做法是：
1. 使用**英文关键词**（"open"、"close"），KWS模型对英文支持好
2. 或者使用**Sherpa-ONNX的流式ASR**（语音识别），识别整句话后判断

**更简化的替代方案**（推荐初学者）：

如果Sherpa-ONNX在I.MX6ULL上太慢，可以用**能量检测+录音+模板匹配**的简化方案：

```
1. 检测环境声音能量超过阈值 → 开始录音
2. 录音1秒 → 提取MFCC特征
3. 与预录的"开门"模板做DTW（动态时间规整）匹配
4. 相似度 > 阈值 → 触发开锁
```

这个方案代码量更少（约200行C代码），对CPU要求极低，适合I.MX6ULL。

### 8.4 硬件连接确认

你的开发板**板载WM8960**，已经焊接好了：
- 麦克风（咪头）：直接说即可
- 扬声器：用于语音反馈（"已开锁"、"识别失败"）

只需要在内核配置中确认WM8960驱动已启用：

```bash
# 在开发板上检查
cat /proc/asound/cards
# 应该看到wm8960声卡

arecord -l
# 应该看到录音设备

# 测试录音
arecord -D hw:0,0 -r 16000 -f S16_LE -c 1 -d 3 test.wav
# 播放测试
aplay test.wav
```

---

## 九、部署与联调（第6阶段）

### 9.1 内核编译

```bash
cd ~/linux/IMX6ULL/linux/alientek_linux_2026_4_26

# 1. 使用正点原子的defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- imx_v7_v7_ca_defconfig
# 或使用正点原子提供的config文件

# 2. 确保以下配置已启用（make menuconfig检查）
# Device Drivers > Character devices > 启用自定义驱动
# Device Drivers > Multimedia support > V4L2 support (启用)
# Device Drivers > I2C support > I2C device interface (启用)
# Device Drivers > PWM support > i.MX PWM support (启用)
# Device Drivers > Sound card support > ALSA > SoC Audio > WM8960 (启用)
# File systems > Network File Systems > NFS client support (可选)

# 3. 编译内核
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage -j$(nproc)
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- dtbs

# 输出文件：
#   arch/arm/boot/zImage          - 内核镜像
#   arch/arm/boot/dts/imx6ull-14x14-evk.dtb - 设备树
```

### 9.2 应用编译

```bash
cd ~/linux/visionpass/app
mkdir build && cd build

# 交叉编译
cmake -DCMAKE_TOOLCHAIN_FILE=~/arm-linux-gnueabihf.toolchain.cmake \
      -DQt5_DIR=/opt/qt5.12-arm/lib/cmake/Qt5 \
      -DCMAKE_INSTALL_PREFIX=/opt/visionpass \
      ..
make -j$(nproc)
```

### 9.3 部署到开发板

```bash
# 方法一：NFS挂载（开发阶段推荐）
# 在开发板上：
mount -t nfs -o nolock <PC_IP>:/home/viper/linux/visionpass /mnt/visionpass
cd /mnt/visionpass/app/build
./visionpass -platform linuxfb

# 方法二：拷贝到SD卡/eMMC（发布阶段）
scp app/build/visionpass root@<开发板IP>:/opt/visionpass/
scp model/* root@<开发板IP>:/opt/visionpass/model/
scp voice/* root@<开发板IP>:/opt/visionpass/voice/
scp config/* root@<开发板IP>:/opt/visionpass/config/
scp drivers/rc522/rc522.ko root@<开发板IP>:/lib/modules/4.1.15/

# 在开发板上运行
ssh root@<开发板IP>
cd /opt/visionpass
insmod /lib/modules/4.1.15/rc522.ko
./visionpass -platform linuxfb
```

### 9.4 开机自启

```bash
# 创建systemd服务
cat > /etc/systemd/system/visionpass.service << 'EOF'
[Unit]
Description=VisionPass AI Access Control
After=network.target

[Service]
Type=simple
ExecStart=/opt/visionpass/visionpass -platform linuxfb
WorkingDirectory=/opt/visionpass
Restart=always
RestartSec=3
Environment=QT_QPA_FB_TSLIB=1

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable visionpass
systemctl start visionpass
```

---

## 十、训练数据集与模型准备

### 10.1 人脸数据采集

在开发板上直接用摄像头采集自己的人脸数据：

```cpp
// 让AI写一个face_collector.cpp
// 功能：
// 1. 从摄像头采集帧
// 2. 使用OpenCV Haar级联检测人脸
// 3. 裁剪人脸ROI，保存为112x112图片
// 4. 按用户名称分类存储
//
// 使用方法：
// ./face_collector --name "viper" --count 100
// 会采集100张你的脸，保存到 dataset/viper/ 目录
```

### 10.2 模型选择建议

| 方案 | 模型 | 大小 | I.MX6ULL速度 | 准确率 | 推荐度 |
|------|------|------|-------------|--------|--------|
| 首选 | MobileFaceNet + NCNN INT8 | ~400KB | ~200ms | 99%+ | ⭐⭐⭐⭐⭐ |
| 备选 | FaceNet-Mobile + NCNN FP32 | ~4MB | ~500ms | 98%+ | ⭐⭐⭐ |
| 轻量 | SqueezeFace + NCNN INT8 | ~200KB | ~100ms | 95%+ | ⭐⭐⭐⭐ |

### 10.3 语音模板采集（简化方案）

```bash
# 如果选择DTW模板匹配方案（非Sherpa-ONNX）
# 让AI写一个voice_template_collector.cpp
# 功能：
# 1. 按提示说"开门"，录音5-10次
# 2. 提取每次的MFCC特征
# 3. 保存为模板文件 /opt/visionpass/voice/open.template
#
# 使用方法：
# ./voice_template_collector --keyword "open" --samples 10
```

---

## 十一、常见问题速查

| 问题 | 原因 | 解决方案 |
|------|------|---------|
| 编译NCNN报C++17错误 | 工具链版本太低 | 升级到gcc-linaro-6.x或使用-DCMAKE_CXX_STANDARD=11 |
| 开发板找不到/dev/video1 | 摄像头驱动未加载 | 检查设备树CSI配置，`modprobe ov2640` |
| 人脸识别太慢（>1s） | 未用INT8量化或未开NEON | 重新量化模型，检查编译优化选项 |
| RC522读不到卡 | SPI片选引脚不对或频率太高 | 检查设备树CS引脚，降低spi-max-frequency到2MHz |
| 舵机不转 | PWM引脚未正确复用 | 检查设备树pinctrl_pwm3配置 |
| 语音识别无反应 | 麦克风增益太低 | `amixer sset 'Capture' 80` 调整录音增益 |
| Qt界面黑屏 | linuxfb未正确初始化 | 检查framebuffer设备`ls /dev/fb*` |
| 开发板内存不足 | 模型加载占用太多 | MobileFaceNet仅400KB，应该不是问题；检查是否有内存泄漏 |

---

## 十二、学习路线与里程碑

```
Phase 1: 环境 + 设备树（第1周）
  ✅ 交叉编译环境
  ✅ 设备树修改 + 烧录
  ✅ 验证所有硬件节点

Phase 2: 驱动开发（第2-3周）
  ✅ RC522驱动（让AI写，你测试）
  ✅ 舵机控制（sysfs，简单）
  ✅ 振动监测（ICM20608，板载）

Phase 3: AI推理部署（第4-5周）
  ✅ NCNN交叉编译
  ✅ 模型训练/转换/量化
  ✅ 人脸识别模块

Phase 4: 语音解锁（第6周）
  ✅ WM8960 ALSA录音
  ✅ Sherpa-ONNX或DTW方案
  ✅ 关键词识别

Phase 5: 应用集成（第7-8周）
  ✅ Qt主界面
  ✅ 5种开锁方式集成
  ✅ 联调测试

Phase 6: 优化拓展（第9-10周）
  ✅ 性能优化
  ✅ 选择一个拓展方向做
```

---

## 附录A：给AI Agent的CLAUDE.md

在项目根目录创建 `.claude/CLAUDE.md`，内容如下：

```markdown
# VisionPass AI门禁系统

## 项目信息
- 硬件：正点原子阿尔法开发板（I.MX6ULL, ARM Cortex-A7 @528MHz, 512MB DDR3）
- 内核：Linux 4.1.15，源码 ~/linux/IMX6ULL/linux/alientek_linux_2026_4_26/
- 交叉编译器：arm-linux-gnueabihf-gcc (Linaro 4.9.4)
- Qt：5.12 ARM版本，安装在 /opt/qt5.12-arm/
- AI推理：NCNN + MobileFaceNet INT8
- 语音：Sherpa-ONNX KWS 或 DTW模板匹配
- 用户是嵌入式Linux初学者

## 编码规范
- 所有C/C++代码必须有中文注释，解释每段代码的作用
- 驱动代码遵循Linux内核编码规范
- 所有命令需要解释作用，不要假设用户知道
- 遇到内核4.1.15的旧API，需要说明和新的区别

## 开发流程
1. 先给出实现方案，再写代码
2. 代码完成后提供编译命令
3. 提供部署到开发板的步骤
4. 提供测试方法和预期结果
```

## 附录B：引脚分配备忘

| 外设 | 接口 | 引脚 | 设备树pinctrl |
|------|------|------|-------------|
| RC522 | ECSPI4 | MISO/MOSI/SCLK/CS(GPIO4_IO15) | pinctrl_ecspi4 |
| SG90 | PWM3 | GPIO1_IO04 | pinctrl_pwm3 |
| AT24C64 | I2C1 | UART4_TX/RX复用 | pinctrl_i2c1（已有） |
| OV2640 | CSI | CSI_DATA0-7/MCLK/VSYNC/HSYNC | pinctrl_csi1（已有） |
| ICM20608 | ECSPI3 | 已有，不需要改 | pinctrl_ecspi3（已有） |
| WM8960 | SAI2 | 已有，板载 | 已有 |
| 按键 | GPIO1_IO18 | 板载功能按钮 | pinctrl_gpio_keys（已有） |

---

*文档版本：v1.0 | 最后更新：2026-05-16*
