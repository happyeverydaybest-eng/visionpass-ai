/*
 * VisionPass 系统状态枚举
 * 定义门禁系统的所有可能状态，用于状态机驱动UI变化
 */

#ifndef SYSTEMSTATE_H
#define SYSTEMSTATE_H

enum SystemState {
    IDLE,               /* 默认锁定状态，等待验证 */
    FACE_SCANNING,      /* 人脸扫描中，摄像头+NCNN运行 */
    FACE_MATCHED,       /* 人脸匹配成功（短暂，随即转UNLOCKED） */
    FACE_UNKNOWN,       /* 人脸未识别（陌生人） */
    RFID_WAITING,       /* 等待刷卡 */
    RFID_MATCHED,       /* 卡片匹配成功 */
    RFID_UNKNOWN,       /* 未授权卡片 */
    PASSWORD_INPUT,     /* 密码输入中（弹窗打开） */
    VOICE_LISTENING,    /* 语音监听中 */
    VOICE_MATCHED,      /* 语音匹配成功 */
    UNLOCKED,           /* 门已打开，舵机90度 */
    ALARM               /* 振动告警 */
};

#endif // SYSTEMSTATE_H