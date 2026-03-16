#pragma once

#include <cstddef>
#include <cstdio>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>

#include "log.hpp"


// 全局字节序判断
static const bool IS_LITTLE_ENDIAN = []() 
{
  const int endian_check = 1;
  // 小端序特征: 低地址存储低位字节(0x01), 大端序存储高位字节(0x00)
  return *reinterpret_cast<const unsigned char*>(&endian_check) == 1;
}();


namespace Utils 
{

// ptrace 包装
bool ptrace_wrapper(int request, pid_t pid, void *address, void* data, size_t data_size, long* result = nullptr);

// waitpid 包装
// @param options 等待选项:
// - 0: 阻塞模式, 直到目标子进程状态变化(退出/暂停)
// - WNOHANG: 非阻塞模式, 无状态变化时立即返回0
// - WUNTRACED: 监听子进程暂停事件(如收到SIGSTOP), 即使未跟踪该进程
// - WCONTINUED: 监听子进程恢复运行事件(收到SIGCONT)
// 返回值:
//   - >0: 已退出/暂停的子进程 PID
//   -  0: 非阻塞模式下, 子进程仍在运行
//   - -1: 调用失败, errno 会被设置
pid_t waitpid_wrapper(pid_t pid, int* status, int __options);

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

// 无符号整数转大端序(入参类型=返回类型)
template <typename T>  T to_big_endian(T host_val) 
{
  // 编译期校验: 仅允许指定的4种无符号整数类型
  static_assert(
    std::is_same<T, uint8_t>::value ||
    std::is_same<T, uint16_t>::value ||
    std::is_same<T, uint32_t>::value ||
    std::is_same<T, uint64_t>::value,
    "仅仅支持 uint8_t/uint16_t/uint32_t/uint64_t!"
  );

  // 大端序主机: 无需转换, 直接返回原值
  if (!IS_LITTLE_ENDIAN) 
    return host_val;

  // 小端序主机: 按类型字节数反转字节序, 返回同类型大端序值
  T big_val = 0;

  constexpr size_t byte_size = sizeof(T);
  for (size_t i = 0; i < byte_size; ++i) 
  {
    // 逐字节提取原数据, 重新拼接
    big_val = (big_val << 8) | (host_val & 0xFF);
    host_val >>= 8;
  }

  return static_cast<T>(big_val);
}

// 大端序无符号整数转主机序
template <typename T> T from_big_endian(T big_val) 
{
  static_assert(
    std::is_same<T, uint8_t>::value ||
    std::is_same<T, uint16_t>::value ||
    std::is_same<T, uint32_t>::value ||
    std::is_same<T, uint64_t>::value,
    "仅仅支持 uint8_t/uint16_t/uint32_t/uint64_t!"
  );

  if (!IS_LITTLE_ENDIAN)
    return big_val;

  T host_val = 0;

  constexpr size_t byte_size = sizeof(T);
  for (size_t i = 0; i < byte_size; ++i) 
  {
    host_val = (host_val << 8) | (big_val & 0xFF);
    big_val >>= 8;
  }

  return static_cast<T>(host_val);
}

// 匹配字符串
enum class MatchMode 
{
  INSENSITIVE,  // 忽略大小写
  SENSITIVE,    // 严格匹配, 大小写敏感
};
bool contains_string(const std::string& src, const std::string& target, MatchMode mode);


}

