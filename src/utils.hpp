#pragma once

#include <cstddef>
#include <cstdio>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>

#include "log.hpp"


namespace Utils 
{

// ptrace 包装
bool ptrace_wrapper(int request, pid_t pid, void *address, void* data, size_t data_size, long* result = nullptr);

// waitpid 包装
bool waitpid_wrapper(pid_t pid, int* status, int __options);

long get_page_size();

// 对齐函数
uint64_t align_up(uint64_t value, uint64_t alignment);

uint64_t align_down(uint64_t value, uint64_t alignment);

// 页对齐
uint64_t align_page_up(uint64_t value);

uint64_t align_page_down(uint64_t value);

// 判断是否是 sigtrap 信号, 在调试时, ptrace 相关操作通常会触发 SIGTRAP 信号
bool is_sigtrap(int status);

// 等待 sigtrap 信号
bool waitpid_sigtrap(pid_t pid);

// 用 ptrace 执行 syscall 指令
bool syscall_wrapper(pid_t pid);

// 无符号整数转大端序（入参类型=返回类型）
template <typename T> T to_big_endian(T host_val);

// 大端序无符号整数转主机序
template <typename T> T from_big_endian(T big_val);

// 匹配字符串
enum class MatchMode 
{
  INSENSITIVE,  // 忽略大小写
  SENSITIVE,    // 严格匹配, 大小写敏感
};
bool contains_string(const std::string& src, const std::string& target, MatchMode mode);


}

