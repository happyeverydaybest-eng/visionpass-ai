#!/bin/bash
# ============================================================
# VisionPass 一键部署脚本
#
# 功能：交叉编译 → 传输到开发板 → 配置环境
# 使用：在PC端执行 ./tools/deploy-to-board.sh
# ============================================================

set -e  # 任何命令失败则停止

# ---------- 配置区域 ----------

# 开发板IP地址（根据你的实际情况修改）
BOARD_IP="192.168.0.102"
BOARD_USER="root"

# 项目根目录
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$PROJECT_DIR/app"

# 交叉编译工具链文件
TOOLCHAIN_FILE="$PROJECT_DIR/config/arm-linux-gnueabihf.toolchain.cmake"

# 开发板上的安装路径
BOARD_INSTALL_DIR="/opt/visionpass"

# ---------- 配置结束 ----------

echo "=========================================="
echo "  VisionPass 部署工具"
echo "=========================================="
echo ""

# ===== 步骤1：交叉编译 =====
echo "[1/3] 交叉编译VisionPass..."

cd "$APP_DIR"

# 创建build目录（如果不存在）
if [ ! -d "build-arm" ]; then
    mkdir build-arm
fi

cd build-arm

# CMake配置
echo "  运行cmake..."
cmake -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" .. 2>&1 | tail -5

# 编译
echo "  编译中..."
make -j$(nproc) 2>&1 | tail -5

# 检查编译产物
if [ ! -f "visionpass" ]; then
    echo "  错误：编译失败，未找到 visionpass 可执行文件"
    exit 1
fi

echo "  编译完成: $(file visionpass | cut -d: -f2)"

# ===== 步骤2：传输到开发板 =====
echo ""
echo "[2/3] 传输到开发板 ($BOARD_IP)..."

# 创建远程目录
ssh "$BOARD_USER@$BOARD_IP" "mkdir -p $BOARD_INSTALL_DIR/bin $BOARD_INSTALL_DIR/drivers $BOARD_INSTALL_DIR/data $BOARD_INSTALL_DIR/logs $BOARD_INSTALL_DIR/config"

# 传输主程序
echo "  传输 visionpass..."
scp visionpass "$BOARD_USER@$BOARD_IP:$BOARD_INSTALL_DIR/bin/"
ssh "$BOARD_USER@$BOARD_IP" "chmod +x $BOARD_INSTALL_DIR/bin/visionpass"

# 传输启动脚本
echo "  传输启动脚本..."
scp "$PROJECT_DIR/tools/visionpass-launcher.sh" "$BOARD_USER@$BOARD_IP:$BOARD_INSTALL_DIR/bin/"
ssh "$BOARD_USER@$BOARD_IP" "chmod +x $BOARD_INSTALL_DIR/bin/visionpass-launcher.sh"

# 传输内核驱动（如果存在）
if [ -d "$PROJECT_DIR/drivers" ]; then
    echo "  传输内核驱动..."
    for ko in rc522 servo ir_sensor; do
        if [ -f "$PROJECT_DIR/drivers/$ko/$ko.ko" ]; then
            scp "$PROJECT_DIR/drivers/$ko/$ko.ko" "$BOARD_USER@$BOARD_IP:$BOARD_INSTALL_DIR/drivers/"
            echo "    $ko.ko ✓"
        fi
    done
fi

echo "  传输完成"

# ===== 步骤3：开发板端配置提示 =====
echo ""
echo "[3/3] 部署完成！"
echo ""
echo "=========================================="
echo "  后续步骤（SSH到开发板执行）："
echo "=========================================="
echo ""
echo "1. 测试运行："
echo "   ssh $BOARD_USER@$BOARD_IP"
echo "   export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0"
echo "   $BOARD_INSTALL_DIR/bin/visionpass"
echo ""
echo "2. 从出厂桌面启动（需要先确认出厂桌面进程名）："
echo "   # 查看出厂桌面进程"
echo "   ps aux | grep qt"
echo ""
echo "   # 编辑启动脚本，修改 ALIENTEK_DESKTOP_PROCESS 变量"
echo "   vi $BOARD_INSTALL_DIR/bin/visionpass-launcher.sh"
echo ""
echo "3. 在出厂桌面上添加VisionPass图标："
echo "   # 需要确认出厂桌面的应用发现机制"
echo "   # 常见方式：在特定目录放.desktop文件或脚本"
echo ""
echo "=========================================="
