/*
 * RC522 完整寄存器扫描程序
 * 读取所有关键寄存器的默认值，用于诊断SPI通信问题
 *
 * RC522复位后的默认值（来自MFRC522数据手册）：
 *   CommandReg(0x01)   = 0x20 (Idle)
 *   ComIEnReg(0x02)    = 0x80 (IRQInv=1)
 *   DivIEnReg(0x03)    = 0x00
 *   ComIrqReg(0x04)    = 0x00
 *   DivIrqReg(0x05)    = 0x04
 *   ErrorReg(0x06)     = 0x00
 *   Status1Reg(0x07)   = 0x00
 *   Status2Reg(0x08)   = 0x00
 *   FIFODataReg(0x09)  = 0x00
 *   FIFOLevelReg(0x0A) = 0x00
 *   WaterLevelReg(0x0B)= 0x08
 *   ControlReg(0x0C)   = 0x20
 *   BitFramingReg(0x0D)= 0x00
 *   CollReg(0x0E)      = 0x80
 *   ModeReg(0x11)      = 0x3D
 *   TxModeReg(0x12)    = 0x00
 *   RxModeReg(0x13)    = 0x00
 *   TxControlReg(0x14) = 0x80
 *   TxASKReg(0x15)     = 0x00
 *   TxSelReg(0x16)     = 0x10
 *   RxSelReg(0x17)     = 0x86
 *   RxThresholdReg(0x18)= 0x84
 *   DemodReg(0x19)     = 0x4D
 *   MfTxReg(0x1C)      = 0x00
 *   MfRxReg(0x1D)      = 0x00
 *   SerialSpeedReg(0x1F)= 0x00
 *   CRCResultRegM(0x21)= 0x00
 *   CRCResultRegL(0x22)= 0x00
 *   ModWidthReg(0x24)  = 0x26
 *   RFCfgReg(0x26)     = 0x70
 *   GsNReg(0x27)       = 0x88
 *   CWGsPReg(0x28)     = 0x20
 *   ModGsPReg(0x29)    = 0x20
 *   TModeReg(0x2A)     = 0x00
 *   TPrescalerReg(0x2B)= 0x00
 *   TReloadRegH(0x2C)  = 0x00
 *   TReloadRegL(0x2D)  = 0x00
 *   TCounterValRegH(0x2E)= 0x00
 *   TCounterValRegL(0x2F)= 0x00
 *   TestSel1Reg(0x31)  = 0x00
 *   TestSel2Reg(0x32)  = 0x00
 *   TestPinEnReg(0x33) = 0x00
 *   TestPinValueReg(0x34)= 0x00
 *   TestBusReg(0x35)   = 0x00
 *   AutoTestReg(0x36)  = 0x00
 *   VersionReg(0x37)   = 0x91 (MFRC522 v1) or 0x92 (MFRC522 v2) or 0x88 (FM17522)
 *   AnalogTestReg(0x38)= 0x00
 *   TestDAC1Reg(0x39)  = 0x00
 *   TestDAC2Reg(0x3A)  = 0x00
 *   TestADCReg(0x3B)   = 0x00
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* 读取一个寄存器 */
static uint8_t ReadRawRC(int fd, uint8_t addr)
{
	uint8_t buf = addr;
	read(fd, &buf, 1);
	return buf;
}

/* 写入一个寄存器 */
static void WriteRawRC(int fd, uint8_t addr, uint8_t val)
{
	uint8_t buf[2] = {addr, val};
	write(fd, buf, 2);
}

/* 寄存器默认值表 */
struct reg_default {
	uint8_t addr;
	uint8_t default_val;
	const char *name;
};

static const struct reg_default regs[] = {
	{0x01, 0x20, "CommandReg"},
	{0x02, 0x80, "ComIEnReg"},
	{0x04, 0x00, "ComIrqReg"},
	{0x05, 0x04, "DivIrqReg"},
	{0x06, 0x00, "ErrorReg"},
	{0x07, 0x00, "Status1Reg"},
	{0x08, 0x00, "Status2Reg"},
	{0x0A, 0x00, "FIFOLevelReg"},
	{0x0B, 0x08, "WaterLevelReg"},
	{0x0C, 0x20, "ControlReg"},
	{0x0D, 0x00, "BitFramingReg"},
	{0x0E, 0x80, "CollReg"},
	{0x11, 0x3D, "ModeReg"},
	{0x12, 0x00, "TxModeReg"},
	{0x13, 0x00, "RxModeReg"},
	{0x14, 0x80, "TxControlReg"},
	{0x15, 0x00, "TxASKReg"},
	{0x16, 0x10, "TxSelReg"},
	{0x17, 0x86, "RxSelReg"},
	{0x18, 0x84, "RxThresholdReg"},
	{0x19, 0x4D, "DemodReg"},
	{0x24, 0x26, "ModWidthReg"},
	{0x26, 0x70, "RFCfgReg"},
	{0x27, 0x88, "GsNReg"},
	{0x28, 0x20, "CWGsPReg"},
	{0x29, 0x20, "ModGsPReg"},
	{0x2A, 0x00, "TModeReg"},
	{0x2B, 0x00, "TPrescalerReg"},
	{0x37, 0x91, "VersionReg (0x91=v1, 0x92=v2, 0x88=FM17522)"},
	{0x3B, 0x00, "TestADCReg"},
};

#define NUM_REGS (sizeof(regs) / sizeof(regs[0]))

int main(int argc, char *argv[])
{
	int fd;
	int match_count = 0;
	int zero_count = 0;
	int i;

	fd = open("/dev/rc522", O_RDWR);
	if (fd < 0) {
		printf("ERROR: Cannot open /dev/rc522: %s\n", strerror(errno));
		return -1;
	}

	printf("RC522 Register Scan (fd=%d)\n", fd);
	printf("========================================\n");
	printf("%-6s %-30s %-8s %-8s %-6s\n", "Addr", "Name", "Default", "Actual", "Match");
	printf("----------------------------------------\n");

	for (i = 0; i < NUM_REGS; i++) {
		uint8_t actual = ReadRawRC(fd, regs[i].addr);
		int match = (actual == regs[i].default_val);
		if (match) match_count++;
		if (actual == 0x00) zero_count++;

		printf("0x%02X   %-30s 0x%02X     0x%02X     %s\n",
		       regs[i].addr, regs[i].name,
		       regs[i].default_val, actual,
		       match ? "OK" : "FAIL");
	}

	printf("========================================\n");
	printf("Total: %d registers, %d match default, %d are 0x00\n",
	       (int)NUM_REGS, match_count, zero_count);

	if (zero_count == NUM_REGS) {
		printf("\nDIAGNOSIS: ALL registers return 0x00\n");
		printf("  -> SPI communication is completely broken\n");
		printf("  -> Possible causes:\n");
		printf("     1. RC522 RST pin not connected to 3.3V\n");
		printf("     2. RC522 module not powered (check VCC=3.3V)\n");
		printf("     3. CS line not toggling (check JP6 Pin 6 wiring)\n");
		printf("     4. MISO line broken (RC522 can't send data back)\n");
	} else if (match_count > NUM_REGS / 2) {
		printf("\nDIAGNOSIS: Most registers match defaults\n");
		printf("  -> SPI communication is working!\n");
	} else {
		printf("\nDIAGNOSIS: Some registers match, some don't\n");
		printf("  -> Partial SPI communication (check signal integrity)\n");
	}

	/* 尝试写读测试 */
	printf("\n--- Write/Read Test ---\n");
	WriteRawRC(fd, 0x24, 0x55);  /* ModWidthReg */
	usleep(1000);
	uint8_t readback = ReadRawRC(fd, 0x24);
	printf("Write 0x55 to ModWidthReg(0x24), readback=0x%02X %s\n",
	       readback, readback == 0x55 ? "OK" : "FAIL");

	/* 恢复默认值 */
	WriteRawRC(fd, 0x24, 0x26);

	close(fd);
	return 0;
}
