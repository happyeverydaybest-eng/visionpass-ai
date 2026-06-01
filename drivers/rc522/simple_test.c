#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

int main() {
    printf("Starting simple test...\n");
    fflush(stdout);

    int fd = open("/dev/rc522", O_RDWR);
    if (fd < 0) {
        printf("Failed to open /dev/rc522\n");
        return 1;
    }
    printf("Opened /dev/rc522 (fd=%d)\n", fd);
    fflush(stdout);

    // Read VersionReg
    uint8_t buf = 0x37;
    int ret = read(fd, &buf, 1);
    printf("Read VersionReg: ret=%d, val=0x%02X\n", ret, buf);
    fflush(stdout);

    close(fd);
    printf("Done\n");
    return 0;
}
