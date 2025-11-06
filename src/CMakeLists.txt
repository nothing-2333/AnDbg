# 自动查找
file(GLOB_RECURSE SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# 生成可执行文件
add_executable(AnDbg ${SRC_FILES})

# 头文件路径配置, 搜索 src/ 目录
target_include_directories(AnDbg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
