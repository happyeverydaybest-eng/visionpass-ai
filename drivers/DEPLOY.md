# VisionPass 驱动模块部署与测试指南

> 本文档面向嵌入式Linux初学者，详细讲解如何将编译好的驱动模块部署到开发板并进行测试。

---

## 一、编译产物总览

| 驱动 | .ko文件 | 测试程序 | 设备节点 |
|------|---------|---------|---------|
| RC522 RFID | `drivers/rc522/rc522.ko` | `drivers/rc522/rc522_test` | `/dev/rc522` |
| SG90舵机 | `drivers/servo/servo.ko` | `drivers/servo/servo_test` | `/dev/servo` |
| 红外传感器 | `drivers/ir_sensor/ir_sensor.ko` | `drivers/ir_sensor/ir_sensor_test` | `/dev/ir_sensor` |

---

## 二、部署到开发板（NFS方式）

### 2.1 确认NFS rootfs已挂载

在开发板上执行：

```bash
mount | grep nfs
```

如果看到类似 `/ on 192.168.1.100:/home/viper/linux/nfs/rootfs type nfs` 的输出，说明NFS已挂载。如果没有挂载，请按之前的教程先配置NFS启动。

### 2.2 创建目录并拷贝文件

**在开发板上执行（或NFS rootfs所在主机上执行）：**

```bash
# 创建驱动目录
mkdir -p /opt/visionpass/drivers
mkdir -p /opt/visionpass/bin

# 拷贝驱动模块（在PC上执行）
cp /home/viper/linux/visionpass/drivers/rc522/rc522.ko       /opt/visionpass/drivers/
cp /home/viper/linux/visionpass/drivers/servo/servo.ko       /opt/visionpass/drivers/
cp /home/viper/linux/visionpass/drivers/ir_sensor/ir_sensor.ko /opt/visionpass/drivers/

# 拷贝测试程序（在PC上执行）
cp /home/viper/linux/visionpass/drivers/rc522/rc522_test       /opt/visionpass/bin/
cp /home/viper/linux/visionpass/drivers/servo/servo_test       /opt/visionpass/bin/
cp /home/viper/linux/visionpass/drivers/ir_sensor/ir_sensor_test /opt/visionpass/bin/
```

---

## 三、测试步骤（按顺序进行）

### 3.1 RC522 RFID驱动测试

**接线检查：** 确保RC522已正确连接到ECSPI3（CS1）。

**在开发板上执行：**

```bash
# 加载驱动
cd /opt/visionpass/drivers
insmod rc522.ko

# 检查设备节点是否创建
ls -l /dev/rc522
```

**预期输出：**
```
crw-rw---- 1 root root 10, 56 May 17 16:00 /dev/rc522
```

**运行测试：**

```bash
# 方式1：寄存器测试（不寻卡，验证SPI通信）
/opt/visionpass/bin/rc522_test --test-only
```

**预期输出：**
```
Opened /dev/rc522 (fd=3)
VersionReg(0x37) = 0x91  → MFRC522 (NXP原版)
TxControlReg write 0x83 → readback 0x83 ✅ 写读一致
TxControlReg write 0x00 → readback 0x00 ✅ 写读一致
Register test completed (use without --test-only for card reading)
```

```bash
# 方式2：完整读卡测试（持续寻卡）
/opt/visionpass/bin/rc522_test
```

**预期输出（刷Mifare S50卡时）：**
```
Opened /dev/rc522 (fd=3)
VersionReg(0x37) = 0x91  → MFRC522 (NXP原版)
...
[1] Card detected! UID: 12 34 56 78  Type: 04 00
```

**卸载驱动：**
```bash
rmmod rc522
```

---

### 3.2 SG90舵机驱动测试

**接线检查：**
- VCC（红色） → 5V（JP6排针）
- GND（棕色） → GND（JP6排针）
- 信号（橙色） → GPIO5_IO10（JP6 Pin 28）

**在开发板上执行：**

```bash
# 加载驱动
cd /opt/visionpass/drivers
insmod servo.ko

# 检查设备节点
ls -l /dev/servo
```

**预期输出：**
```
crw-rw---- 1 root root 10, 57 May 17 16:00 /dev/servo
```

**运行测试：**

```bash
# 方式1：单次设置角度（推荐）
/opt/visionpass/bin/servo_test 90    # 设置90度
/opt/visionpass/bin/servo_test 0     # 设置0度
/opt/visionpass/bin/servo_test 180   # 设置180度

# 方式2：开锁/关锁预设角度
/opt/visionpass/bin/servo_test unlock  # 开锁（90度）
/opt/visionpass/bin/servo_test lock    # 关锁（0度）

# 方式3：循环扫描（0°→45°→90°→135°→180°→0°）
/opt/visionpass/bin/servo_test

# 方式4：停止PWM输出
/opt/visionpass/bin/servo_test stop
```

**预期现象：** 舵机根据指令转动到对应角度。

**卸载驱动：**
```bash
rmmod servo
```

---

### 3.3 红外传感器驱动测试

**接线检查：**
- VCC（+） → 3.3V（JP6排针）
- GND（-） → GND（JP6排针）
- OUT      → GPIO5_IO02（JP6 Pin 22）

**在开发板上执行：**

```bash
# 加载驱动
cd /opt/visionpass/drivers
insmod ir_sensor.ko

# 检查设备节点
ls -l /dev/ir_sensor
```

**预期输出：**
```
crw-rw---- 1 root root 10, 58 May 17 16:00 /dev/ir_sensor
```

**运行测试：**

```bash
# 方式1：只读取一次当前状态
/opt/visionpass/bin/ir_sensor_test --once
```

**预期输出：**
```
Opened /dev/ir_sensor (fd=3)
Door state: CLOSED (0)
```

```bash
# 方式2：轮询模式（每秒打印一次）
/opt/visionpass/bin/ir_sensor_test
```

**预期输出：**
```
=== Polling mode (Ctrl+C to exit) ===
Door state: CLOSED (0)
Door state: CLOSED (0)
Door state: OPEN (1)       <-- 用手遮挡红外传感器时
Door state: OPEN (1)
Door state: CLOSED (0)     <-- 移开手后
```

```bash
# 方式3：poll模式（等待变化才打印，推荐）
/opt/visionpass/bin/ir_sensor_test --poll
```

**预期输出：**
```
=== Poll mode (waiting for door state change) ===
Current state: CLOSED
Waiting... (Ctrl+C to exit)

[Change detected] Door state: OPEN (1)    <-- 遮挡传感器
[Change detected] Door state: CLOSED (0)    <-- 移开传感器
```

**卸载驱动：**
```bash
rmmod ir_sensor
```

---

## 四、常见问题排查

### 问题1：`insmod` 失败，提示 `insmod: can't insert 'xxx.ko': No such file or directory`

**原因：** 模块路径错误或文件未拷贝到开发板。

**解决：**
```bash
# 确认文件存在
ls /opt/visionpass/drivers/xxx.ko
# 如果不存在，从PC拷贝（在PC上执行）
cp /home/viper/linux/visionpass/drivers/xxx/xxx.ko /opt/visionpass/drivers/
```

### 问题2：`/dev/xxx` 不存在

**原因：** 驱动加载失败或设备节点未创建。

**解决：**
```bash
# 查看内核日志
dmesg | tail -20
```

常见原因：
- 设备树节点未正确添加 → 重新检查imx6ull-14x14-evk.dts
- GPIO申请失败 → 确认该GPIO未被其他驱动占用
- 权限不足 → 用root执行insmod

### 问题3：RC522 VersionReg读出来是0x00或0xFF

**原因：** SPI通信失败。

**排查步骤：**
1. 检查接线（MISO/MOSI/SCK/CS/RST）
2. 确认设备树中`spi-max-frequency`不超过5MHz
3. 用示波器或逻辑分析仪抓SPI波形
4. 检查`dmesg`中是否有SPI相关错误

### 问题4：舵机不转动

**原因：**
1. **供电不足**：SG90需要5V供电，电流约100mA，确保JP6排针的5V能提供足够电流
2. **接线错误**：确认信号线接到GPIO5_IO10（JP6 Pin 28）
3. **PWM周期不对**：检查`dmesg`中是否有`servo: PWM thread started`日志

### 问题5：红外传感器读数不变化

**原因：**
1. **接线错误**：确认OUT接到GPIO5_IO02（JP6 Pin 22）
2. **模块供电错误**：确认VCC接3.3V（不是5V）
3. **模块未对准**：红外发射管和接收管需要对准（或反射面在检测范围内）

---

## 五、批量加载/卸载脚本（可选）

创建 `/opt/visionpass/bin/load_drivers.sh`：

```bash
#!/bin/sh
# 加载所有VisionPass驱动

echo "Loading VisionPass drivers..."

cd /opt/visionpass/drivers

# 加载RC522
if [ -f rc522.ko ]; then
    insmod rc522.ko
    echo "RC522 loaded"
fi

# 加载IR传感器
if [f ir_sensor.ko ]; then
    insmod ir_sensor.ko
    echo "IR Sensor loaded"
fi

# 加载舵机（最后加载，因为会启动kthread）
if [ -f servo.ko ]; then
    insmod servo.ko
    echo "Servo loaded"
fi

echo "All drivers loaded!"
```

创建 `/opt/visionpass/bin/unload_drivers.sh`：

```bash
#!/bin/sh
# 卸载所有VisionPass驱动

echo "Unloading VisionPass drivers..."

rmmod servo      2>/dev/null
rmmod ir_sensor  2>/dev/null
rmmod rc522      2>/dev/null

echo "All drivers unloaded!"
```

---

## 六、下一步

驱动测试通过后，可以开始编写Qt主应用程序，将三个模块整合到一起：

1. **RC522 RFID** → 刷卡开锁
2. **红外传感器** → 门状态检测（自动关锁）
3. **SG90舵机** → 门锁执行

后续还将添加：
- NCNN人脸识别模块
- DTW语音解锁模块
- Qt主界面（摄像头预览、密码输入、用户管理等）