set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# 交叉编译sysroot路径（包含libc头文件+Qt头文件+Qt ARM库+rootfs运行库）
set(CMAKE_SYSROOT /home/viper/linux/tool/sysroot-combined)
set(CMAKE_FIND_ROOT_PATH /home/viper/linux/tool/sysroot-combined)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# I.MX6ULL Cortex-A7优化
set(CMAKE_C_FLAGS "-mcpu=cortex-a7 -mfpu=neon -mfloat-abi=hard -O3" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "-mcpu=cortex-a7 -mfpu=neon -mfloat-abi=hard -O3" CACHE STRING "" FORCE)