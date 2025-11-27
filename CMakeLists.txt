

# 指定 NDK 路径
set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
if(NOT ANDROID_NDK)
    message(FATAL_ERROR "未设置 ANDROID_NDK_HOME 环境变量！请配置 NDK 路径")
endif()


# 设置 Android 平台和架构
set(ANDROID_PLATFORM android-24)
# 针对 64 位 ARM 架构
set(ANDROID_ABI arm64-v8a) 
# 生成语法提示所需文件
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 引入 NDK 的 CMake 工具链
include(${ANDROID_NDK}/build/cmake/android.toolchain.cmake)

# 先引入 NDK, 否则会有问题
cmake_minimum_required(VERSION 3.18)
project(AnDbg)

# 编译第三方库
set(FMT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fmt)
if(NOT EXISTS ${FMT_SOURCE_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "fmt 源码缺失")
endif()
add_subdirectory(${FMT_SOURCE_DIR}) 

set(CAPSTONE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/capstone)
if(NOT EXISTS ${CAPSTONE_SOURCE_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "capstone 源码缺失")
endif()
add_subdirectory(${CAPSTONE_SOURCE_DIR}) 

# 编译核心文件
add_subdirectory(src)

# 链接
target_link_libraries(AnDbg PRIVATE fmt::fmt log) # log 是 Android 系统日志库

# 全局 C++ 标准配置
set_target_properties(AnDbg PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

# 统一可执行文件输出路径
set_target_properties(AnDbg PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)