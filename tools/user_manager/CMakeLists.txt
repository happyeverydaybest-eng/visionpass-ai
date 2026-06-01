cmake_minimum_required(VERSION 3.10)
project(VisionPassUserManager VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 查找Qt5
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Sql Network)

# 查找OpenMP
find_package(OpenMP REQUIRED)

# 查找OpenCV
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs objdetect)

# NCNN配置 - 手动链接
set(NCNN_INCLUDE_DIR "/home/viper/ncnn/build-x86/src")
set(NCNN_LIBRARY "/home/viper/ncnn/build-x86/src/libncnn.a")

# 源文件
set(SOURCES
    main.cpp
    MainWindow.cpp
)

set(HEADERS
    MainWindow.h
)

# 创建可执行文件
add_executable(user_manager ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(user_manager PRIVATE
    /home/viper/ncnn/src
    /home/viper/ncnn/build-x86/src
)

# 链接库
target_link_libraries(user_manager
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Sql
    Qt5::Network
    ${OpenCV_LIBS}
    ${NCNN_LIBRARY}
    OpenMP::OpenMP_CXX
    pthread
    dl
)

# 安装规则
install(TARGETS user_manager DESTINATION bin)
