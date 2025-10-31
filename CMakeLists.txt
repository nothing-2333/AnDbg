# 指定 NDK 路径
set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")

# 设置 Android 平台和架构
set(ANDROID_PLATFORM android-24)
set(ANDROID_ABI arm64-v8a) # 针对 64 位 ARM 架构

# 引入 NDK 的 CMake 工具链
include(${ANDROID_NDK}/build/cmake/android.toolchain.cmake)

cmake_minimum_required(VERSION 3.18)
project(AnDbg)


# 添加可执行文件
add_executable(AnDbg 
  main.cpp
  debugger_core.cpp
  breakpoint_manager.cpp
  symbol_resolver.cpp
)

# 链接Android日志库
target_link_libraries(AnDbg log)
