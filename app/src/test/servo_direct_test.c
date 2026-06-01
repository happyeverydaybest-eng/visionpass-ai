#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define SERVO_SET_ANGLE 0x40045300

int main() {
    int fd = open("/dev/servo", O_RDWR);
    if (fd < 0) {
        printf("Failed to open /dev/servo\n");
        return 1;
    }

    printf("Testing servo at 0 degrees...\n");
    ioctl(fd, SERVO_SET_ANGLE, 0);
    sleep(2);

    printf("Testing servo at 90 degrees...\n");
    ioctl(fd, SERVO_SET_ANGLE, 90);
    sleep(2);

    printf("Testing servo at 180 degrees...\n");
    ioctl(fd, SERVO_SET_ANGLE, 180);
    sleep(2);

    printf("Back to 90 degrees...\n");
    ioctl(fd, SERVO_SET_ANGLE, 90);
    sleep(2);

    close(fd);
    printf("Test complete\n");
    return 0;
}
