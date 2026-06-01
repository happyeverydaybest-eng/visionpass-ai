#!/bin/sh
# 测试舵机PWM信号输出

echo "开始测试舵机PWM信号..."
echo "请观察舵机是否转动"
echo ""

# 加载驱动
/sbin/insmod /opt/visionpass/drivers/servo.ko 2>/dev/null
sleep 1

# 打开设备并保持一段时间
exec 3<>/dev/servo

echo "设置角度为0度，保持3秒..."
/opt/visionpass/bin/servo_test 0 &
PID1=$!
sleep 3

echo "设置角度为90度，保持3秒..."
/opt/visionpass/bin/servo_test 90 &
PID2=$!
sleep 3

echo "设置角度为180度，保持3秒..."
/opt/visionpass/bin/servo_test 180 &
PID3=$!
sleep 3

# 等待所有测试完成
wait $PID1 $PID2 $PID3

echo ""
echo "测试完成"
echo ""
echo "如果舵机没有转动，请检查："
echo "1. 舵机电源（红色线）是否连接到5V"
echo "2. 舵机地线（棕色线）是否连接到GND"
echo "3. 舵机信号线（橙色线）是否连接到JP6 Pin 5"
echo ""

# 卸载驱动
/sbin/rmmod servo 2>/dev/null
