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

# NCNN ARM路径
set(NCNN_ARM_DIR "/home/viper/ncnn/build-arm/install")

# sysroot（包含OpenCV运行库、C标准库头文件等）
set(SYSROOT_DIR "/home/viper/linux/tool/sysroot-combined")

# ==================== Qt配置 ====================

set(CMAKE_PREFIX_PATH "${QT_ARM_DIR}")
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network Sql Concurrent)

set(Qt5Core_MOC_EXECUTABLE "${QT_ARM_DIR}/bin/moc")
set(Qt5Core_RCC_EXECUTABLE "${QT_ARM_DIR}/bin/rcc")
set(Qt5Core_UIC_EXECUTABLE "${QT_ARM_DIR}/bin/uic")

# ==================== OpenCV配置 ====================

set(OpenCV_INCLUDE_DIRS "${SYSROOT_DIR}/usr/include/opencv2")
set(OpenCV_LIB_DIR "${SYSROOT_DIR}/usr/lib")

set(OPENCV_LIBS
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    opencv_objdetect
    opencv_highgui
    opencv_videoio
)

# ==================== NCNN配置 ====================

set(NCNN_INCLUDE_DIRS "${NCNN_ARM_DIR}/include/ncnn")
set(NCNN_LIBRARIES "${NCNN_ARM_DIR}/lib/libncnn.a")

# ==================== 源文件 ====================

# 人脸模块（已有）
set(FACE_SOURCES
    src/face/FaceDetector.cpp
    src/face/FaceRecognizer.cpp
    src/face/FaceProcessThread.cpp
)
set(FACE_HEADERS
    src/face/FaceDetector.h
    src/face/FaceRecognizer.h
    src/face/FaceProcessThread.h
)

# 系统控制器
set(CONTROLLER_SOURCES
    src/controller/SystemController.cpp
)
set(CONTROLLER_HEADERS
    src/controller/SystemController.h
    src/controller/SystemState.h
)

# 视频采集
set(CAPTURE_SOURCES
    src/capture/V4L2CaptureThread.cpp
)
set(CAPTURE_HEADERS
    src/capture/V4L2CaptureThread.h
)

# UI组件
set(UI_SOURCES
    ui/MainWindow.cpp
    ui/NotificationOverlay.cpp
)
set(UI_HEADERS
    ui/MainWindow.h
    ui/NotificationOverlay.h
)

# 入口点
set(MAIN_SOURCE
    main.cpp
)

# 所有源文件汇总
set(ALL_SOURCES
    ${MAIN_SOURCE}
    ${FACE_SOURCES}
    ${CONTROLLER_SOURCES}
    ${CAPTURE_SOURCES}
    ${UI_SOURCES}
)

set(ALL_HEADERS
    ${FACE_HEADERS}
    ${CONTROLLER_HEADERS}
    ${CAPTURE_HEADERS}
    ${UI_HEADERS}
)

# ==================== 构建目标：可执行文件 ====================

add_executable(visionpass ${ALL_SOURCES})

# 包含路径
target_include_directories(visionpass PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/face
    ${CMAKE_CURRENT_SOURCE_DIR}/src/controller
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${QT_ARM_DIR}/include
    ${QT_ARM_DIR}/include/QtCore
    ${QT_ARM_DIR}/include/QtGui
    ${QT_ARM_DIR}/include/QtWidgets
    ${QT_ARM_DIR}/include/QtNetwork
    ${QT_ARM_DIR}/include/QtSql
    ${NCNN_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/arm-linux-gnueabihf/include/c++/4.9.4
    /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/arm-linux-gnueabihf/include/c++/4.9.4/arm-linux-gnueabihf
    ${SYSROOT_DIR}/usr/include
)

# 链接库（只链接核心OpenCV模块，移除依赖GTK/GStreamer的highgui/videoio）
target_link_libraries(visionpass PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Network
    Qt5::Sql
    Qt5::Concurrent
    ${NCNN_LIBRARIES}
    ${OpenCV_LIB_DIR}/libopencv_core.so
    ${OpenCV_LIB_DIR}/libopencv_imgproc.so
    ${OpenCV_LIB_DIR}/libopencv_imgcodecs.so
    ${OpenCV_LIB_DIR}/libopencv_objdetect.so
    ${SYSROOT_DIR}/usr/lib/libz.so
    pthread
    -Wl,--allow-shlib-undefined  # 允许未定义的共享库符号（运行时由开发板提供）
)

# GCC 4.9.4兼容旧ABI（OpenCV库可能使用新ABI编译）
target_compile_definitions(visionpass PRIVATE _GLIBCXX_USE_CXX11_ABI=0)

# Qt MOC处理
qt5_wrap_cpp(MOC_FILES
    src/face/FaceDetector.h
    src/face/FaceRecognizer.h
    src/face/FaceProcessThread.h
    src/controller/SystemController.h
    src/capture/V4L2CaptureThread.h
    ui/MainWindow.h
    ui/NotificationOverlay.h
)
target_sources(visionpass PRIVATE ${MOC_FILES})