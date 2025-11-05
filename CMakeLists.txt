cmake_minimum_required(VERSION 3.20)

project(use_ffmpeg LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON) 
set(CMAKE_CXX_EXTENSIONS OFF) 

# Qt6 配置 - 修正为实际安装的版本
set(CMAKE_PREFIX_PATH "D:/Qt/6.9.1/msvc2022_64")
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets Network)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 极简视频播放器
add_executable(player
    main.cpp
    VideoPlayer.cpp
    VideoPlayer.h
    VideoWidget.cpp
    VideoWidget.h
    AudioProcessor.cpp
    AudioProcessor.h
    OverlayWidget.cpp
    OverlayWidget.h
    NetworkStreamManager.cpp
    NetworkStreamManager.h
    NetworkConfig.cpp
    NetworkConfig.h
    StreamProtocolHandler.cpp
    StreamProtocolHandler.h
    NetworkStreamUI.cpp
    NetworkStreamUI.h
    NetworkStreamLoader.cpp
    NetworkStreamLoader.h
    LoadingWidget.cpp
    LoadingWidget.h
    resource.qrc
)

# FFmpeg 头文件路径
target_include_directories(player
    PRIVATE
    $ENV{VCPKG_ROOT}/installed/x64-windows-static/include
)

# 播放器配置
target_link_libraries(player
    Qt6::Core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Network
    $ENV{VCPKG_ROOT}/installed/x64-windows-static/lib/avcodec.lib
    $ENV{VCPKG_ROOT}/installed/x64-windows-static/lib/avformat.lib
    $ENV{VCPKG_ROOT}/installed/x64-windows-static/lib/avutil.lib
    $ENV{VCPKG_ROOT}/installed/x64-windows-static/lib/swscale.lib
    $ENV{VCPKG_ROOT}/installed/x64-windows-static/lib/swresample.lib
    # Windows系统库（静态链接FFmpeg所需）
    ws2_32
    secur32
    winmm
    mfplat
    mfuuid
    strmiids
    ole32
    user32
    bcrypt
)

# 修复链接器警告 LNK4098
if(WIN32 AND MSVC)
    target_link_options(player PRIVATE 
        /NODEFAULTLIB:LIBCMT
    )
endif()
