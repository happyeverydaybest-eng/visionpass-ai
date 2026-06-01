#!/bin/sh
# 舵机全面诊断测试

echo "=========================================="
echo "舵机全面诊断测试"
echo "=========================================="
echo ""

# 检查驱动是否加载
echo "1. 检查驱动状态..."
if [ -c /dev/servo ]; then
    echo "   ✓ /dev/servo 设备存在"
else
    echo "   ✗ /dev/servo 设备不存在，正在加载驱动..."
    insmod /opt/visionpass/drivers/servo.ko
    sleep 1
    if [ -c /dev/servo ]; then
        echo "   ✓ 驱动加载成功"
    else
        echo "   ✗ 驱动加载失败"
        exit 1
    fi
fi
echo ""

# 检查PWM线程
echo "2. 检查PWM线程..."
if ps aux | grep -q "[s]ervo_pwm"; then
    echo "   ✓ PWM线程正在运行"
    ps aux | grep "[s]ervo_pwm" | awk '{print "   CPU使用率: " $3 "%"}'
else
    echo "   ⚠ PWM线程未运行（将在设置角度时启动）"
fi
echo ""

# 检查GPIO状态
echo "3. 检查GPIO状态..."
if [ -f /sys/kernel/debug/gpio ]; then
    GPIO_STATE=$(cat /sys/kernel/debug/gpio | grep gpio-27)
    if [ -n "$GPIO_STATE" ]; then
        echo "   ✓ GPIO-27 已配置"
        echo "   $GPIO_STATE"
    else
        echo "   ✗ GPIO-27 未找到"
    fi
else
    echo "   ⚠ 无法读取GPIO状态"
fi
echo ""

# 检查内核日志
echo "4. 最近的舵机相关内核日志:"
dmesg | grep -i "servo\|pwm" | tail -10 | sed 's/^/   /'
echo ""

# 硬件测试
echo "=========================================="
echo "硬件测试 - 请观察舵机是否转动"
echo "=========================================="
echo ""
echo "即将测试以下角度（每个保持3秒）："
echo "  0° → 90° → 180° → 90° → 0°"
echo ""
echo "请确保："
echo "  1. 舵机电源（红线）连接到 5V"
echo "  2. 舵机地线（棕线）连接到 GND"
echo "  3. 舵机信号线（橙线）连接到 JP6 Pin 5"
echo ""
read -p "按 Enter 开始测试..." dummy
echo ""

# 测试不同角度
for angle in 0 90 180 90 0; do
    echo "设置角度: ${angle}° (保持3秒)..."
    /opt/visionpass/bin/servo_test $angle > /dev/null 2>&1 &
    TEST_PID=$!
    sleep 3
    kill $TEST_PID 2>/dev/null
    wait $TEST_PID 2>/dev/null
    echo ""
done

echo "=========================================="
echo "测试完成"
echo "=========================================="
echo ""
echo "如果舵机没有转动，请检查："
echo "  1. 舵机电源是否连接到 5V（不是 3.3V）"
echo "  2. 舵机地线是否连接到 GND"
echo "  3. 舵机信号线是否连接到 JP6 Pin 5 (GPIO1_IO27)"
echo "  4. 尝试更换舵机或检查舵机是否损坏"
echo ""
echo "如果有万用表，可以测量："
echo "  - JP6 Pin 5 和 GND 之间的电压（应该在 0-3.3V 之间波动）"
echo "  - 舵机 VCC 和 GND 之间的电压（应该是 5V）"
echo ""
