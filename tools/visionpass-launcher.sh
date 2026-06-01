#!/bin/sh
# ============================================================
# VisionPass 启动脚本（桌面集成版）
#
# 功能：从正点原子出厂Qt桌面中启动VisionPass门禁系统
# 流程：暂停出厂桌面 → 启动VisionPass → VisionPass退出后恢复出厂桌面
#
# 使用方法：
#   1. 将此脚本部署到开发板 /opt/visionpass/bin/
#   2. 在出厂桌面中添加一个图标，执行此脚本
#   3. 点击图标 → 进入VisionPass → 点"退出系统" → 回到出厂桌面
#
# 注意：需要root权限运行（操作/dev设备节点）
# ============================================================

# ---------- 配置区域（根据你的开发板实际情况修改） ----------

# VisionPass主程序路径
VISIONPASS_BIN="/opt/visionpass/bin/visionpass"

# 内核驱动模块路径
DRIVERS_DIR="/opt/visionpass/drivers"

# 日志文件
LOG_DIR="/opt/visionpass/logs"
LOG_FILE="$LOG_DIR/visionpass.log"

# 出厂桌面进程名（需要在开发板上用 ps 确认）
# 常见名称：alientek_qt_app / gui_launcher / demo
# 如果不确定，先在开发板上执行: ps aux | grep qt
ALIENTEK_DESKTOP_PROCESS="ALIENTEK_DESKTOP"

# 出厂桌面启动命令（需要在开发板上确认）
# 查看方法: cat /etc/rc.local 或 cat /etc/init.d/S99*
ALIENTEK_DESKTOP_CMD=""

# ---------- 配置结束 ----------

# 创建日志目录
mkdir -p "$LOG_DIR"

echo "=========================================="
echo "  VisionPass AI门禁系统 启动器"
echo "=========================================="

# ===== 步骤1：暂停出厂桌面 =====
echo "[1/4] 正在暂停出厂桌面..."

# 查找出厂桌面进程PID
DESKTOP_PID=$(pgrep -f "$ALIENTEK_DESKTOP_PROCESS")

if [ -n "$DESKTOP_PID" ]; then
    echo "  找到出厂桌面进程: PID=$DESKTOP_PID"
    # 发送SIGTERM让其优雅退出
    kill -TERM "$DESKTOP_PID" 2>/dev/null
    # 等待进程退出（最多3秒）
    for i in 1 2 3; do
        if ! kill -0 "$DESKTOP_PID" 2>/dev/null; then
            echo "  出厂桌面已退出"
            break
        fi
        sleep 1
    done
    # 如果还没退出，强制kill
    if kill -0 "$DESKTOP_PID" 2>/dev/null; then
        echo "  强制停止出厂桌面..."
        kill -9 "$DESKTOP_PID" 2>/dev/null
        sleep 1
    fi
else
    echo "  未找到出厂桌面进程（可能已被关闭）"
fi

# ===== 步骤2：加载内核驱动 =====
echo "[2/4] 检查内核驱动..."

# 加载RC522 RFID驱动
if [ ! -e /dev/rc522 ]; then
    if [ -f "$DRIVERS_DIR/rc522.ko" ]; then
        echo "  加载RC522驱动..."
        /sbin/insmod "$DRIVERS_DIR/rc522.ko"
        sleep 0.5
    fi
fi

# 加载舵机驱动
if [ ! -e /dev/servo ]; then
    if [ -f "$DRIVERS_DIR/servo.ko" ]; then
        echo "  加载servo驱动..."
        /sbin/insmod "$DRIVERS_DIR/servo.ko"
        sleep 0.5
    fi
fi

# 加载红外传感器驱动
if [ ! -e /dev/ir_sensor ]; then
    if [ -f "$DRIVERS_DIR/ir_sensor.ko" ]; then
        echo "  加载IR sensor驱动..."
        /sbin/insmod "$DRIVERS_DIR/ir_sensor.ko"
        sleep 0.5
    fi
fi

echo "  驱动就绪"

# ===== 步骤3：启动VisionPass =====
echo "[3/4] 启动VisionPass门禁系统..."

# 设置Qt环境变量
export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0
export QT_PLUGIN_PATH=/usr/lib/qt5/plugins
export LD_LIBRARY_PATH=/opt/visionpass/lib:$LD_LIBRARY_PATH

# 前台运行VisionPass（阻塞直到用户退出）
"$VISIONPASS_BIN" >> "$LOG_FILE" 2>&1
VP_EXIT_CODE=$?

echo "[4/4] VisionPass已退出（返回码: $VP_EXIT_CODE）"

# ===== 步骤4：恢复出厂桌面 =====
echo "正在恢复出厂桌面..."

if [ -n "$ALIENTEK_DESKTOP_CMD" ]; then
    # 如果配置了出厂桌面启动命令，执行它
    $ALIENTEK_DESKTOP_CMD &
    echo "  出厂桌面已恢复"
else
    # 尝试常见的启动方式
    # 方式1：通过init.d脚本重启
    if [ -f /etc/init.d/S99myir ]; then
        /etc/init.d/S99myir start &
        echo "  通过S99myir恢复出厂桌面"
    elif [ -f /etc/init.d/qt-demo ]; then
        /etc/init.d/qt-demo start &
        echo "  通过qt-demo恢复出厂桌面"
    elif [ -f /etc/rc.local ]; then
        # 尝试从rc.local中提取启动命令
        echo "  请手动重启开发板以恢复出厂桌面"
        echo "  或配置此脚本中的 ALIENTEK_DESKTOP_CMD 变量"
    else
        echo "  无法自动恢复出厂桌面"
        echo "  请手动重启开发板，或配置 ALIENTEK_DESKTOP_CMD"
    fi
fi

echo "=========================================="
echo "  VisionPass会话结束"
echo "=========================================="
