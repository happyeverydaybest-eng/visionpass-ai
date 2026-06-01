#!/bin/sh
# VisionPass 启动脚本
# 加载内核驱动模块并启动主程序

# 驱动模块路径
DRIVERS_DIR="/opt/visionpass/drivers"

# 日志文件
LOG_FILE="/opt/visionpass/logs/visionpass.log"

echo "VisionPass starting..."

# 加载RC522 RFID驱动
if [ ! -e /dev/rc522 ]; then
    echo "Loading RC522 driver..."
    /sbin/insmod $DRIVERS_DIR/rc522.ko
    sleep 1
    if [ -e /dev/rc522 ]; then
        echo "RC522 driver loaded: /dev/rc522"
    else
        echo "ERROR: Failed to load RC522 driver"
    fi
else
    echo "RC522 already loaded"
fi

# 加载舵机驱动
if [ ! -e /dev/servo ]; then
    echo "Loading servo driver..."
    /sbin/insmod $DRIVERS_DIR/servo.ko
    sleep 1
    if [ -e /dev/servo ]; then
        echo "Servo driver loaded: /dev/servo"
    else
        echo "ERROR: Failed to load servo driver"
    fi
else
    echo "Servo already loaded"
fi

# 加载红外传感器驱动
if [ ! -e /dev/ir_sensor ]; then
    echo "Loading IR sensor driver..."
    /sbin/insmod $DRIVERS_DIR/ir_sensor.ko
    sleep 1
    if [ -e /dev/ir_sensor ]; then
        echo "IR sensor driver loaded: /dev/ir_sensor"
    else
        echo "ERROR: Failed to load IR sensor driver"
    fi
else
    echo "IR sensor already loaded"
fi

# 杀掉旧的visionpass进程
pkill -9 visionpass 2>/dev/null
sleep 1

# 启动主程序
echo "Starting visionpass..."
export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0
nohup /opt/visionpass/bin/visionpass > $LOG_FILE 2>&1 &

sleep 2
echo "VisionPass started, PID: $!"
echo "Log file: $LOG_FILE"
