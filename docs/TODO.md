# VisionPass AI门禁系统 — TODO清单

> 最后更新：2026-05-16

## Phase 1: 环境准备 + 设备树（第1周）

### 环境搭建
- [x] **安装系统包**（cmake, libsqlite3-dev, qtbase5-dev, libopencv-dev, nfs-kernel-server）
- [x] **验证交叉编译工具链**（Linaro 4.9.4，PATH已生效）
- [x] **创建cmake交叉编译工具链文件**（config/arm-linux-gnueabihf.toolchain.cmake）
- [ ] **Qt ARM交叉编译**（待确认正点原子资料包是否有预编译版本）
- [x] **NCNN交叉编译**（~/ncnn/build-arm/install/，已验证ARM架构）
- [x] **Sherpa-ONNX暂不编译**（GCC 4.9.4不支持C++17，源码已克隆留作将来升级，语音解锁改用DTW方案）
- [ ] **NFS挂载配置**（需要sudo权限，用户手动执行）
  - 命令：`echo '/home/viper/linux/visionpass *(rw,sync,no_root_squash,no_subtree_check)' | sudo tee -a /etc/exports && sudo exportfs -ra`
- [x] **CLAUDE.md创建**（.claude/CLAUDE.md，项目上下文已写入）
- [x] **原始设备树备份**（imx6ull-14x14-evk.dts.original）

### 设备树修改
- [ ] **修改 imx6ull-14x14-evk.dts**
  - 添加ECSPI4控制器节点 + RC522 RFID子节点
  - 使能PWM3节点（SG90舵机）
  - 添加GPIO节点（红外模块，门检测）
  - 添加pinctrl_ecspi4引脚复用配置
  - 添加pinctrl_pwm3引脚复用配置
- [ ] **编译设备树**
  - `make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- imx6ull-14x14-evk.dtb`
- [ ] **烧录到开发板**
  - 通过TFTP/NFS/SCP部署dtb文件
- [ ] **验证设备树**
  - 开发板上检查 /proc/device-tree、I2C、SPI、PWM节点

## Phase 2: 驱动开发（第2-3周）

- [ ] **RC522 RFID驱动**（优先级1）
- [ ] **SG90舵机控制**（优先级2，sysfs PWM）
- [ ] **ICM20608振动监测**（优先级4，板载已有驱动）
- [ ] **红外模块GPIO读取**（优先级3，门检测）

## Phase 3: AI推理部署（第4-5周）

- [ ] **NCNN交叉编译验证**（已完成）
- [ ] **MobileFaceNet模型准备**
- [ ] **人脸识别模块开发**

## Phase 4: 语音解锁（第6周）

- [ ] **DTW模板匹配方案开发**（纯C++，无外部依赖，GCC 4.9.4兼容）
- [ ] **WM8960 ALSA录音测试**
- [ ] **MFCC特征提取实现**
- [ ] **模板采集+匹配测试**

## Phase 5: 应用集成（第7-8周）

- [ ] **Qt主界面开发**
- [ ] **视频采集模块**
- [ ] **RFID读卡封装**
- [ ] **密码验证模块**
- [ ] **用户管理模块**
- [ ] **CMakeLists.txt构建系统**
- [ ] **5种开锁方式集成联调**

## Phase 6: 优化拓展（第9-10周）

- [ ] **性能优化**
- [ ] **开机自启配置**
- [ ] **选择一个拓展方向**