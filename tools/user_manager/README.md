# VisionPass 用户管理工具

这是一个PC端上位机程序，用于管理VisionPass门禁系统的用户数据。

## 功能特点

- **用户管理**：添加、删除用户，管理用户密码
- **人脸注册**：导入照片，自动检测人脸并提取128维特征向量
- **RFID卡管理**：注册和删除RFID卡片
- **密码管理**：重置用户密码（SHA-256加密存储）

## 运行环境要求

- Linux系统（Ubuntu 18.04+）
- Qt 5.12+
- OpenCV 4.x
- NCNN（已静态链接）

## 使用方法

### 1. 启动程序

```bash
cd /home/viper/linux/visionpass/tools/user_manager/build
./user_manager
```

### 2. 数据库配置

程序首次启动时会自动在build目录创建`users.db`数据库文件。

**重要**：要让开发板使用这个数据库，需要将数据库复制到开发板：

```bash
# 复制数据库到开发板
scp /home/viper/linux/visionpass/tools/user_manager/build/users.db root@192.168.77.69:/opt/visionpass/data/

# 或者创建符号链接（如果在同一台机器）
ln -sf /home/viper/linux/visionpass/tools/user_manager/build/users.db /opt/visionpass/data/users.db
```

### 3. 人脸注册流程

1. 在"用户管理"标签页添加新用户（填写用户ID、姓名、密码）
2. 切换到"人脸注册"标签页
3. 输入用户ID，点击"选择照片"按钮
4. 选择一张清晰的正面人脸照片（JPG/PNG格式）
5. 程序会自动检测人脸并提取特征
6. 点击"保存特征"将特征存入数据库
7. 将数据库复制到开发板

**照片要求**：
- 正面照，光线均匀
- 人脸占照片的1/3以上
- 分辨率建议640x480以上
- 建议注册3-5张不同角度的照片以提高识别率

### 4. RFID卡注册

1. 在"RFID卡管理"标签页
2. 输入卡号（8位十六进制，如：AABBCCDD）
3. 输入关联的用户ID
4. 点击"注册卡片"

### 5. 密码管理

1. 在"密码管理"标签页
2. 输入用户ID
3. 输入新密码
4. 点击"重置密码"

## 模型文件

程序需要以下模型文件（已自动复制到build/model目录）：

- `MobileFaceNet.param` - 人脸识别模型参数
- `MobileFaceNet.bin` - 人脸识别模型权重
- `haarcascade_frontalface_alt2.xml` - 人脸检测模型

## 开发板同步

每次在PC端修改用户数据后，都需要将数据库同步到开发板：

```bash
scp /home/viper/linux/visionpass/tools/user_manager/build/users.db root@192.168.77.69:/opt/visionpass/data/
```

或者设置自动同步脚本：

```bash
#!/bin/bash
# sync_db.sh
DB_PATH="/home/viper/linux/visionpass/tools/user_manager/build/users.db"
BOARD_IP="192.168.77.69"
BOARD_PATH="/opt/visionpass/data/users.db"

echo "正在同步数据库到开发板..."
scp "$DB_PATH" "root@$BOARD_IP:$BOARD_PATH"
echo "同步完成！"
```

## 技术细节

### 人脸特征提取流程

1. **人脸检测**：使用OpenCV的Haar级联分类器检测人脸位置
2. **人脸裁剪**：根据检测框裁剪人脸区域
3. **尺寸归一化**：将人脸缩放到112x112像素
4. **颜色转换**：BGR转RGB
5. **特征提取**：使用NCNN MobileFaceNet模型提取128维特征向量
6. **归一化**：像素值归一化到[-1, 1]范围

### 数据库结构

```sql
-- 用户表
CREATE TABLE users (
    user_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 人脸特征表
CREATE TABLE face_features (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT NOT NULL,
    feature BLOB NOT NULL,  -- 128维float数组，512字节
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id)
);

-- RFID卡表
CREATE TABLE rfid_cards (
    card_id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(user_id)
);
```

## 故障排除

### 问题1：无法打开数据库
**原因**：数据库文件路径不正确或权限不足
**解决**：确保build目录有写入权限

### 问题2：人脸检测失败
**原因**：照片质量不佳或人脸不明显
**解决**：更换清晰的正面照片，确保光线均匀

### 问题3：特征提取失败
**原因**：模型文件缺失或损坏
**解决**：检查build/model目录下是否有完整的模型文件

### 问题4：开发板无法识别已注册的人脸
**原因**：数据库未同步或特征提取参数不一致
**解决**：
1. 确保数据库已复制到开发板
2. 确认PC端和开发板使用相同的模型文件
3. 重启开发板上的visionpass程序

## 更新日志

### v1.0.0 (2024-05-24)
- 初始版本
- 支持用户管理、人脸注册、RFID卡管理、密码管理
- 使用MobileFaceNet提取128维人脸特征
- 支持SHA-256密码加密
