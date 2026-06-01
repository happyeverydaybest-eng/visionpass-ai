/*
 * RC522 详细诊断工具
 *
 * 用于排查"SPI通信正常但读不到卡"的问题
 * 诊断项目：
 * 1. 所有关键寄存器值
 * 2. 天线驱动电流状态（TxControlReg）
 * 3. RF场强度（RFCfgReg + GsNReg）
 * 4. 接收电路状态
 * 5. 手动发送REQA并观察响应
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* RC522寄存器地址 */
#define CommandReg      0x01
#define ComIEnReg       0x02
#define DivIrqReg       0x05
#define ComIrqReg       0x04
#define ErrorReg        0x06
#define Status1Reg      0x07
#define Status2Reg      0x08
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define WaterLevelReg   0x0B
#define ControlReg      0x0C
#define BitFramingReg   0x0D
#define CollReg         0x0E
#define ModeReg         0x11
#define TxModeReg       0x12
#define RxModeReg       0x13
#define TxControlReg    0x14
#define TxAutoReg       0x15
#define TxSelReg        0x16
#define RxSelReg        0x17
#define RxThresholdReg  0x18
#define DemodReg        0x19
#define MfTxReg         0x1C
#define MfRxReg         0x1D
#define SerialSpeedReg  0x1F
#define CRCResultRegM   0x21
#define CRCResultRegL   0x22
#define ModWidthReg     0x24
#define RFCfgReg        0x26
#define GsNReg          0x27
#define CWGsPReg        0x28
#define ModGsPReg       0x29
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D
#define TCounterRegH    0x2E
#define TCounterRegL    0x2F
#define TestSel1Reg     0x31
#define TestSel2Reg     0x32
#define TestPinEnReg    0x33
#define TestPinValueReg 0x34
#define TestBusReg      0x35
#define AutoTestReg     0x36
#define VersionReg      0x37
#define AnalogTestReg   0x38
#define TestDAC1Reg     0x39
#define TestDAC2Reg     0x3A
#define TestADCReg      0x3B

/* RC522命令 */
#define PCD_IDLE        0x00
#define PCD_TRANSCEIVE  0x0C
#define PCD_RESETPHASE  0x0F
#define PCD_CALCCRC     0x03

/* PICC命令 */
#define PICC_REQIDL     0x26
#define PICC_REQALL     0x52
#define PICC_ANTICOLL1  0x93

#define MI_OK           0
#define MI_ERR          -1

static int fd;

static uint8_t ReadReg(uint8_t addr)
{
    uint8_t buf = addr;
    read(fd, &buf, 1);
    return buf;
}

static void WriteReg(uint8_t addr, uint8_t val)
{
    uint8_t buf[2] = {addr, val};
    write(fd, buf, 2);
}

static void SetBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t tmp = ReadReg(reg);
    WriteReg(reg, tmp | mask);
}

static void ClearBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t tmp = ReadReg(reg);
    WriteReg(reg, tmp & (~mask));
}

static void PcdReset(void)
{
    WriteReg(CommandReg, PCD_RESETPHASE);
    usleep(50000);
    WriteReg(ModeReg, 0x3D);
    WriteReg(RxModeReg, 0x00);    /* 重要：初始化接收模式 */
    WriteReg(TModeReg, 0x8D);
    WriteReg(TPrescalerReg, 0x3E);
    WriteReg(TReloadRegH, 0x00);
    WriteReg(TReloadRegL, 0x1F);
    WriteReg(TxControlReg, 0x03);  /* 直接开启天线 */
    WriteReg(TxAutoReg, 0x40);     /* 100% ASK调制 */
    WriteReg(RFCfgReg, 0x7F);      /* 最大接收增益 */
}

static char PcdComMF522(uint8_t command, uint8_t *inData, uint8_t inLen,
                        uint8_t *outData, uint32_t *outLenBit)
{
    uint8_t irq = 0x00;
    uint8_t irq_wait = 0x00;
    uint8_t last_bits = 0;
    uint8_t n = 0;
    uint32_t i = 0;

    if (command == PCD_TRANSCEIVE) {
        irq_wait = 0x30;
    }

    WriteReg(ComIEnReg, irq_wait | 0x80);
    ClearBitMask(ComIrqReg, 0x80);
    WriteReg(CommandReg, PCD_IDLE);
    SetBitMask(FIFOLevelReg, 0x80);

    for (i = 0; i < inLen; i++)
        WriteReg(FIFODataReg, inData[i]);

    /* 重要：每次通信都设置BitFramingReg */
    WriteReg(BitFramingReg, 0x07);
    WriteReg(CommandReg, command);

    if (command == PCD_TRANSCEIVE)
        SetBitMask(BitFramingReg, 0x80);

    i = 2000;
    do {
        irq = ReadReg(ComIrqReg);
        if (irq & 0x01) break;
        if (irq & irq_wait) break;
        usleep(100);
    } while (--i);

    ClearBitMask(BitFramingReg, 0x80);

    if (i == 0) {
        printf("    [TIMEOUT] 命令超时\n");
        return MI_ERR;
    }

    uint8_t err = ReadReg(ErrorReg);
    if (err & 0x1D) {
        printf("    [ERROR] ErrorReg = 0x%02X (", err);
        if (err & 0x01) printf("ProtocolErr ");
        if (err & 0x04) printf("BufferOvfl ");
        if (err & 0x08) printf("ParityErr ");
        if (err & 0x10) printf("CollErr ");
        printf(")\n");
        return MI_ERR;
    }

    n = ReadReg(FIFOLevelReg);
    last_bits = ReadReg(ControlReg) & 0x07;
    if (last_bits)
        *outLenBit = n * 8 + last_bits;
    else
        *outLenBit = n * 8;

    if (n == 0) n = 1;
    if (n > 18) n = 18;

    for (i = 0; i < n; i++)
        outData[i] = ReadReg(FIFODataReg);

    printf("    [OK] 接收 %d 字节, %d 位\n", n, *outLenBit);
    return MI_OK;
}

static void dumpRegisters(void)
{
    printf("\n===== 寄存器状态 =====\n");
    printf("CommandReg    (0x01) = 0x%02X  (应为0x00=Idle)\n", ReadReg(CommandReg));
    printf("ComIEnReg     (0x02) = 0x%02X\n", ReadReg(ComIEnReg));
    printf("ErrorReg      (0x06) = 0x%02X  (应为0x00=无错误)\n", ReadReg(ErrorReg));
    printf("Status1Reg    (0x07) = 0x%02X\n", ReadReg(Status1Reg));
    printf("Status2Reg    (0x08) = 0x%02X  (bit3=MFCrypto1Active)\n", ReadReg(Status2Reg));
    printf("FIFOLevelReg  (0x0A) = 0x%02X\n", ReadReg(FIFOLevelReg));
    printf("BitFramingReg (0x0D) = 0x%02X\n", ReadReg(BitFramingReg));
    printf("CollReg       (0x0E) = 0x%02X\n", ReadReg(CollReg));
    printf("ModeReg       (0x11) = 0x%02X  (应为0x3D)\n", ReadReg(ModeReg));
    printf("TxModeReg     (0x12) = 0x%02X  (应为0x00)\n", ReadReg(TxModeReg));
    printf("RxModeReg     (0x13) = 0x%02X  (应为0x00)\n", ReadReg(RxModeReg));

    uint8_t txCtrl = ReadReg(TxControlReg);
    printf("TxControlReg  (0x14) = 0x%02X  ", txCtrl);
    if (txCtrl & 0x03)
        printf("✅ 天线已开启 (TX1=%d TX2=%d)\n", txCtrl & 0x01, (txCtrl >> 1) & 0x01);
    else
        printf("❌ 天线未开启！\n");

    printf("TxAutoReg     (0x15) = 0x%02X  (应为0x40=100%%ASK)\n", ReadReg(TxAutoReg));
    printf("TxSelReg      (0x16) = 0x%02X\n", ReadReg(TxSelReg));
    printf("RxSelReg      (0x17) = 0x%02X  (应为0x86)\n", ReadReg(RxSelReg));
    printf("RxThresholdReg(0x18) = 0x%02X\n", ReadReg(RxThresholdReg));
    printf("DemodReg      (0x19) = 0x%02X\n", ReadReg(DemodReg));
    printf("RFCfgReg      (0x26) = 0x%02X  (应为0x7F=最大增益)\n", ReadReg(RFCfgReg));
    printf("GsNReg        (0x27) = 0x%02X  (天线驱动导通强度)\n", ReadReg(GsNReg));
    printf("CWGsPReg      (0x28) = 0x%02X  (天线P驱动导通)\n", ReadReg(CWGsPReg));
    printf("ModGsPReg     (0x29) = 0x%02X  (调制P驱动导通)\n", ReadReg(ModGsPReg));
    printf("TModeReg      (0x2A) = 0x%02X  (应为0x8D)\n", ReadReg(TModeReg));
    printf("TPrescalerReg (0x2B) = 0x%02X  (应为0x3E)\n", ReadReg(TPrescalerReg));
    printf("TReloadRegH   (0x2C) = 0x%02X\n", ReadReg(TReloadRegH));
    printf("TReloadRegL   (0x2D) = 0x%02X  (应为0x1F)\n", ReadReg(TReloadRegL));
    printf("VersionReg    (0x37) = 0x%02X\n", ReadReg(VersionReg));
}

int main(int argc, char *argv[])
{
    fd = open("/dev/rc522", O_RDWR);
    if (fd < 0) {
        printf("ERROR: Cannot open /dev/rc522: %s\n", strerror(errno));
        return -1;
    }
    printf("Opened /dev/rc522 (fd=%d)\n", fd);

    /* 完整复位 */
    printf("\n===== 复位RC522 =====\n");
    PcdReset();
    printf("复位完成\n");

    /* 寄存器dump */
    dumpRegisters();

    /* ===== 诊断1：手动发送REQA (0x26) ===== */
    printf("\n===== 诊断1：发送REQA命令 =====\n");
    {
        uint8_t tagType[2];
        uint32_t len = 0;
        uint8_t buf[1] = {PICC_REQIDL};

        WriteReg(BitFramingReg, 0x07);  /* 7位有效（REQ命令规范） */
        printf("  BitFramingReg set to 0x07\n");
        printf("  Sending REQA (0x26)...\n");

        char ret = PcdComMF522(PCD_TRANSCEIVE, buf, 1, tagType, &len);
        if (ret == MI_OK) {
            printf("  ✅ 收到响应! TagType: 0x%02X 0x%02X\n", tagType[0], tagType[1]);
        } else {
            printf("  ❌ 无响应\n");
        }
    }

    /* ===== 诊断2：发送REQALL (0x52) 尝试唤醒所有卡 ===== */
    printf("\n===== 诊断2：发送REQALL命令 =====\n");
    {
        uint8_t tagType[2];
        uint32_t len = 0;
        uint8_t buf[1] = {PICC_REQALL};

        WriteReg(BitFramingReg, 0x07);
        printf("  Sending REQALL (0x52)...\n");

        char ret = PcdComMF522(PCD_TRANSCEIVE, buf, 1, tagType, &len);
        if (ret == MI_OK) {
            printf("  ✅ 收到响应! TagType: 0x%02X 0x%02X\n", tagType[0], tagType[1]);
        } else {
            printf("  ❌ 无响应\n");
        }
    }

    /* ===== 诊断3：天线开关测试 ===== */
    printf("\n===== 诊断3：天线开关测试 =====\n");
    {
        /* 关闭天线 */
        ClearBitMask(TxControlReg, 0x03);
        usleep(200000);
        printf("  天线关闭, TxControlReg = 0x%02X\n", ReadReg(TxControlReg));

        /* 重新开启 */
        SetBitMask(TxControlReg, 0x03);
        usleep(50000);
        printf("  天线开启, TxControlReg = 0x%02X\n", ReadReg(TxControlReg));

        /* 再试一次寻卡 */
        uint8_t tagType[2];
        uint32_t len = 0;
        uint8_t buf[1] = {PICC_REQIDL};
        WriteReg(BitFramingReg, 0x07);
        printf("  重新发送REQA...\n");
        char ret = PcdComMF522(PCD_TRANSCEIVE, buf, 1, tagType, &len);
        if (ret == MI_OK) {
            printf("  ✅ 收到响应! TagType: 0x%02X 0x%02X\n", tagType[0], tagType[1]);
        } else {
            printf("  ❌ 无响应\n");
        }
    }

    /* ===== 诊断4：持续寻卡循环（10次） ===== */
    printf("\n===== 诊断4：持续寻卡（10次，每次间隔500ms）=====\n");
    printf("请将RFID卡靠近天线...\n");
    {
        int success = 0;
        int i;
        for (i = 0; i < 10; i++) {
            uint8_t tagType[2];
            uint32_t len = 0;
            uint8_t buf[1] = {PICC_REQIDL};
            WriteReg(BitFramingReg, 0x07);

            /* 确保天线开着 */
            uint8_t tx = ReadReg(TxControlReg);
            if (!(tx & 0x03)) {
                SetBitMask(TxControlReg, 0x03);
                usleep(50000);
            }

            char ret = PcdComMF522(PCD_TRANSCEIVE, buf, 1, tagType, &len);
            if (ret == MI_OK) {
                printf("[%d] ✅ 检测到卡! TagType: 0x%02X 0x%02X\n", i+1, tagType[0], tagType[1]);

                /* 尝试防冲撞获取UID */
                uint8_t uid[5];
                uint32_t uidLen = 0;
                uint8_t anticoll_buf[2] = {PICC_ANTICOLL1, 0x20};
                WriteReg(BitFramingReg, 0x00);  /* 防冲撞用8位 */
                ClearBitMask(CollReg, 0x80);
                char ret2 = PcdComMF522(PCD_TRANSCEIVE, anticoll_buf, 2, uid, &uidLen);
                if (ret2 == MI_OK) {
                    uint8_t check = uid[0] ^ uid[1] ^ uid[2] ^ uid[3];
                    if (check == uid[4]) {
                        printf("    UID: %02X %02X %02X %02X (BCC=0x%02X ✅)\n",
                               uid[0], uid[1], uid[2], uid[3], uid[4]);
                        success++;
                    } else {
                        printf("    UID校验失败: %02X %02X %02X %02X BCC=%02X\n",
                               uid[0], uid[1], uid[2], uid[3], uid[4]);
                    }
                } else {
                    printf("    防冲撞失败\n");
                }

                /* 天线关200ms再开，让卡片重新可检测 */
                ClearBitMask(TxControlReg, 0x03);
                usleep(200000);
                SetBitMask(TxControlReg, 0x03);
                usleep(50000);
            } else {
                printf("[%d] ❌ 未检测到卡\n", i+1);
            }
            usleep(500000);  /* 500ms间隔 */
        }
        printf("\n结果: %d/10 次成功\n", success);
    }

    close(fd);
    return 0;
}
