# VisionPass AI门禁系统 — 交叉编译CMake配置
#
# 编译命令：
#   cd ~/linux/visionpass/app
#   mkdir build-arm && cd build-arm
#   cmake -DCMAKE_TOOLCHAIN_FILE=~/linux/visionpass/config/arm-linux-gnueabihf.toolchain.cmake ..
#   make -j$(nproc)
#
# 产物：visionpass（ARM ELF可执行文件）
# 部署到开发板：cp visionpass /opt/visionpass/bin/

cmake_minimum_required(VERSION 3.10)
project(VisionPass VERSION 1.0 LANGUAGES C CXX)

# C++11标准（GCC 4.9.4不完全支持C++14，所以用C++11）
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 路径配置 ====================

# Qt ARM路径
set(QT_ARM_DIR "/home/viper/linux/tool/sysroot-combined/opt/qt5.12-arm")
set(QT_QMAKE_EXECUTABLE "${QT_ARM_DIR}/bin/qmake")

# NCNN ARM路径
set(NCNN_ARM_DIR "/home/viper/ncnn/build-arm/install")

# sysroot（包含OpenCV运行库、C标准库头文件等）
set(SYSROOT_DIR "/home/viper/linux/tool/sysroot-combined")

# ==================== Qt配置 ====================

# 自动查找Qt5模块（通过sysroot中的Qt）
set(CMAKE_PREFIX_PATH "${QT_ARM_DIR}")
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network Sql Concurrent)

# Qt moc/rcc/uic工具（用主机版本的，因为它们是代码生成器，不是编译器）
set(Qt5Core_MOC_EXECUTABLE "${QT_ARM_DIR}/bin/moc")
set(Qt5Core_RCC_EXECUTABLE "${QT_ARM_DIR}/bin/rcc")
set(Qt5Core_UIC_EXECUTABLE "${QT_ARM_DIR}/bin/uic")

# ==================== OpenCV配置 ====================

# OpenCV在sysroot中（运行库已有，头文件已手动安装）
# 因为没有OpenCVConfig.cmake，手动指定路径
set(OpenCV_INCLUDE_DIRS "${SYSROOT_DIR}/usr/include/opencv2")
set(OpenCV_LIB_DIR "${SYSROOT_DIR}/usr/lib")

# OpenCV 3.1.0模块列表（只链接我们需要的模块）
set(OPENCV_LIBS
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_objdetect
    opencv_highgui
    opencv_videoio
)

# ==================== NCNN配置 ====================

# NCNN是静态库（libncnn.a），直接指定路径
set(NCNN_INCLUDE_DIRS "${NCNN_ARM_DIR}/include/ncnn")
set(NCNN_LIBRARIES "${NCNN_ARM_DIR}/lib/libncnn.a")

# ==================== 源文件 ====================

set(FACE_SOURCES
    src/face/FaceDetector.cpp
    src/face/FaceRecognizer.cpp
)

set(FACE_HEADERS
    src/face/FaceDetector.h
    src/face/FaceRecognizer.h
)

# ==================== 构建目标 ====================

# 暂时只编译人脸模块为共享库（后续整合到主程序时改为可执行文件）
add_library(visionpass_face STATIC ${FACE_SOURCES})

# 包含路径
target_include_directories(visionpass_face PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/face
    ${QT_ARM_DIR}/include
    ${QT_ARM_DIR}/include/QtCore
    ${QT_ARM_DIR}/include/QtGui
    ${QT_ARM_DIR}/include/QtWidgets
    ${NCNN_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/arm-linux-gnueabihf/include/c++/4.9.4
    /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/arm-linux-gnueabihf/include/c++/4.9.4/arm-linux-gnueabihf
    ${SYSROOT_DIR}/usr/include
)

# 链接库
target_link_libraries(visionpass_face PUBLIC
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    ${NCNN_LIBRARIES}
    ${OpenCV_LIB_DIR}/libopencv_core.so
    ${OpenCV_LIB_DIR}/libopencv_imgproc.so
    ${OpenCV_LIB_DIR}/libopencv_imgcodecs.so
    ${OpenCV_LIB_DIR}/libopencv_objdetect.so
    ${OpenCV_LIB_DIR}/libopencv_highgui.so
    ${OpenCV_LIB_DIR}/libopencv_videoio.so
    ${SYSROOT_DIR}/usr/lib/libz.so
    pthread
)

# Qt MOC处理（生成元对象代码，Qt信号槽机制需要）
qt5_generate_moc(FaceDetector.h ${CMAKE_CURRENT_BINARY_DIR}/moc_FaceDetector.cpp)
qt5_generate_moc(FaceRecognizer.h ${CMAKE_CURRENT_BINARY_DIR}/moc_FaceRecognizer.cpp)