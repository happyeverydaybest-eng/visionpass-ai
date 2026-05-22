# VisionPass 项目 TODO 清单

> 最后更新：2026-01-22

---

## ✅ 已完成的功能

### 环境搭建
- [x] 交叉编译工具链安装 (arm-linux-gnueabihf-gcc)
- [x] Qt 5.12.12 ARM 版本编译
- [x] NCNN 推理框架 ARM 版本编译
- [x] OpenCV 3.1.0 ARM 版本安装
- [x] CMake 交叉编译配置
- [x] sysroot-combined 整合（Qt 头文件 + 库文件）
- [x] MobileFaceNet 模型下载 (INT8 量化版)
- [x] Haar 级联人脸检测模型

### 设备树修改
- [x] RC522 RFID 模块 (ECSPI3 + GPIO CS)
- [x] SG90 舵机 (PWM3)
- [x] IR 传感器 (GPIO5_IO02)
- [x] 设备树编译和部署

### 内核驱动
- [x] RC522 RFID SPI 字符设备驱动
  - [x] SPI 通信协议实现
  - [x] ioctl 接口设计
  - [x] 寄存器读写封装
  - [x] 代码审查修复（CRC寄存器、BitFraming、线程安全）
- [x] SG90 舵机 PWM 驱动
  - [x] 软件 PWM 实现
  - [x] ioctl 角度控制
- [x] IR 传感器 GPIO 驱动
  - [x] 中断触发模式
  - [x] poll() 支持

### AI 人脸识别模块
- [x] FaceDetector - Haar 级联人脸检测
  - [x] OpenCV CascadeClassifier 封装
  - [x] QImage ↔ cv::Mat 转换
  - [x] 多尺度检测
- [x] FaceRecognizer - NCNN MobileFaceNet 推理
  - [x] 模型加载和初始化
  - [x] 特征提取 (128维向量)
  - [x] 余弦相似度计算
- [x] FaceProcessThread - 人脸处理线程
  - [x] 帧队列机制（最多5帧）
  - [x] 跳帧处理（每3帧处理1帧）
  - [x] 识别冷却机制（3秒）

### 视频采集模块
- [x] V4L2CaptureThread - 摄像头采集线程
  - [x] V4L2 mmap 模式
  - [x] RGB565 格式采集
  - [x] 3fps 帧率控制
  - [x] QImage 转换和信号发射

### RFID 刷卡模块
- [x] RC522User - RC522 用户空间封装
  - [x] SPI 寄存器读写
  - [x] 卡片检测流程（REQA → 防冲突 → SELECT）
  - [x] UID 读取
  - [x] 代码审查修复（CRC寄存器、BitFraming、线程安全）
- [x] RFIDThread - RFID 轮询线程
  - [x] 200ms 轮询间隔
  - [x] 去抖动逻辑
  - [x] 卡片移除等待

### 密码开锁模块
- [x] PasswordDialog - 数字键盘对话框
  - [x] 1024x600 布局适配
  - [x] 深色科技风 UI
  - [x] 8位密码限制
  - [x] 信号发射机制
- [x] 密码验证
  - [x] SHA-256 哈希
  - [x] 数据库比对
  - [x] 优化查询（避免加载 BLOB）

### 硬件控制模块
- [x] ServoControl - 舵机控制
  - [x] ioctl 接口
  - [x] 0°（锁定）/ 90°（解锁）
  - [x] 返回值检查（防止虚假信号）
- [x] BeeperControl - 蜂鸣器控制
  - [x] sysfs 接口
  - [x] QTimer 时序控制
  - [x] 保持文件打开（减少 VFS 开销）
- [x] IRSensorMonitor - IR 传感器监控
  - [x] QFile 轮询（500ms）
  - [x] 状态变化检测
  - [x] 内存泄漏修复

### 用户数据库
- [x] UserDatabase - SQLite 用户管理
  - [x] 数据库表设计
  - [x] CRUD 操作
  - [x] 人脸特征 BLOB 存储
  - [x] SHA-256 密码哈希
  - [x] 索引优化（card_uid, password_hash）
  - [x] bulk memcpy 优化

### 语音识别模块
- [x] VoiceThread - 语音识别线程（占位实现）
  - [x] QThread 框架
  - [x] 信号接口定义
  - [ ] DTW 算法实现（待完成）
  - [ ] MFCC 特征提取（待完成）
  - [ ] ALSA 录音接口（待完成）

### 系统集成
- [x] SystemController - 状态机控制器
  - [x] 状态定义（IDLE/SCANNING/UNLOCKED 等）
  - [x] 状态转换逻辑
  - [x] 模块初始化和清理
  - [x] stopAllActiveScanning()（防止资源泄漏）
- [x] MainWindow - 主窗口
  - [x] 1024x600 布局
  - [x] 视频显示区
  - [x] 按钮区（5个功能按钮）
  - [x] 状态 LED + 时间显示
  - [x] 性能优化（QImage::scaled）
- [x] CMake 构建系统
  - [x] 交叉编译配置
  - [x] MOC 文件生成
  - [x] 链接库配置

### 代码审查
- [x] 代码重用审查 - 发现 4 High + 7 Medium + 5 Low 问题
- [x] 效率审查 - 发现 3 Critical + 5 High + 7 Medium 问题
- [x] 代码质量审查 - 发现 3 Critical + 5 High + 11 Medium 问题
- [x] 修复所有 Critical 和 High 问题
  - [x] ServoControl 虚假信号修复
  - [x] unlockDoor() 状态机绕过修复
  - [x] IRSensorMonitor 内存泄漏修复
  - [x] 密码验证性能优化
  - [x] 数据库索引添加
  - [x] BeeperControl sysfs 优化
  - [x] featureToBlob bulk memcpy 优化

---

## 🔄 进行中的任务

### 文档完善
- [x] 项目学习指南 - 面向初学者的完整学习路径
- [ ] 硬件接线图 - 各模块引脚连接说明
- [ ] 部署手册 - 开发板部署和运行指南
- [ ] 故障排查手册 - 常见问题和解决方案

---

## 📋 待完成的任务

### 功能完善
- [ ] 语音识别 - DTW 算法实现
- [ ] 用户管理界面 - 添加/删除/修改用户
- [ ] 人脸注册界面 - 采集人脸并提取特征
- [ ] RFID 卡片注册 - 绑定用户和卡片
- [ ] 日志系统 - 记录开锁事件和时间
- [ ] 远程监控 - 网络传输视频流

### 性能优化
- [ ] 数据库连接池 - 减少连接开销
- [ ] 人脸特征缓存 - 避免重复查询
- [ ] 视频帧率自适应 - 根据场景调整
- [ ] 内存占用优化 - 减少峰值内存

### 测试和验证
- [ ] 单元测试 - 各模块功能测试
- [ ] 集成测试 - 完整流程测试
- [ ] 压力测试 - 长时间运行稳定性
- [ ] 边界测试 - 异常情况处理

### 部署优化
- [ ] 开机自启动 - systemd 服务配置
- [ ] 异常恢复 - 崩溃后自动重启
- [ ] OTA 升级 - 远程更新程序
- [ ] 日志轮转 - 防止日志文件过大

---

## 🎯 里程碑

### v1.0 - 基础功能完成 ✅
- 人脸识别开锁
- RFID 刷卡开锁
- 密码开锁
- 舵机控制
- 基础 UI

### v1.1 - 代码质量提升 ✅
- 完成代码审查
- 修复所有 Critical 和 High 问题
- 性能优化

### v1.2 - 文档完善 🔄
- 学习指南
- 部署手册
- 故障排查

### v2.0 - 功能完善（计划）
- 语音识别
- 用户管理界面
- 日志系统
- 远程监控

---

## 📊 代码统计

```bash
# 源代码文件数
find app/src -name "*.cpp" | wc -l  # 13 个实现文件
find app/src -name "*.h" | wc -l    # 13 个头文件

# 代码行数
find app/src -name "*.cpp" -o -name "*.h" | xargs wc -l
# 约 4000+ 行代码

# 驱动代码
find drivers -name "*.c" | xargs wc -l
# 约 800+ 行驱动代码
```

---

## 🔧 已知问题

### 低优先级
- BeeperControl::beepOnce() 未使用（可删除）
- BeeperControl::nextStep() 有冗余 if/else
- SystemController 析构函数冗余关闭调用
- VoiceThread 占位实现创建线程浪费资源

### 待优化
- SQL 查询未使用 prepared statement 缓存
- 无 WAL 模式（嵌入式写入性能）
- IR 传感器轮询使用 QFile（可用 POSIX API）

---

## 📝 更新日志

### 2026-01-22
- ✅ 完成代码审查（3 个审查代理并行）
- ✅ 修复所有 Critical 和 High 问题
- ✅ 创建学习指南文档
- ✅ 更新 TODO 清单

### 2026-01-21
- ✅ 集成所有模块到 SystemController
- ✅ 实现完整状态机
- ✅ 编译通过并生成可执行文件

### 2026-01-20
- ✅ 实现 UserDatabase（SQLite）
- ✅ 实现 VoiceThread（占位）
- ✅ 实现 PasswordDialog
- ✅ 实现 ServoControl、BeeperControl、IRSensorMonitor

### 2026-01-19
- ✅ 实现 RFIDThread
- ✅ 实现 RC522User
- ✅ 实现 V4L2CaptureThread
- ✅ 实现 FaceProcessThread

### 2026-01-18
- ✅ 实现 FaceRecognizer（NCNN）
- ✅ 实现 FaceDetector（Haar）
- ✅ 搭建交叉编译环境
- ✅ 编译 Qt 5.12 ARM 版本

---

**项目状态：** 核心功能已完成，正在进行文档完善和代码质量提升

**下一步计划：** 
1. 完善部署文档和故障排查手册
2. 实现用户管理界面
3. 完成语音识别功能
