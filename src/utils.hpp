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

/**
 * @brief waitpid 系统调用的包装函数
 * 
 * @param pid 目标进程ID, 指定等待哪个进程: 
 *            - >0: 等待特定 PID 的进程
 *            - -1: 等待任意进程
 *            - 0: 等待与调用进程同进程组的任意进程
 *            - <-1: 等待进程组 ID 等于 pid 绝对值的任意进程
 * 
 * @param status 输出参数, 存储进程状态信息。可使用以下宏检查: 
 *               - WIFEXITED(status): 正常退出
 *               - WEXITSTATUS(status): 获取退出状态码(仅当 WIFEXITED 为真)
 *               - WIFSIGNALED(status): 因信号终止
 *               - WTERMSIG(status): 获取终止信号的编号(仅当 WIFSIGNALED 为真)
 *               - WIFSTOPPED(status): 因信号暂停
 *               - WSTOPSIG(status): 获取暂停信号的编号(仅当 WIFSTOPPED 为真)
 *               - WIFCONTINUED(status): 已从暂停状态恢复
 * 
 * @param __options 等待选项: 
 *                  - 0: 阻塞模式, 直到目标进程状态变化(退出/暂停)
 *                  - WNOHANG(0x00000001): 非阻塞模式, 无状态变化时立即返回0
 *                  - WUNTRACED(0x00000002): 监听进程暂停事件(如收到 SIGSTOP), 即使未跟踪该进程
 *                  - WEXITED(0x00000004): 监听已终止的进程
 *                  - WCONTINUED(0x00000008): 监听进程恢复运行事件(收到 SIGCONT)
 *                  - WNOWAIT(0x01000000): 保持进程在等待状态, 可再次 waitpid
 *                  - __WNOTHREAD(0x20000000): 不等待兄弟线程的进程
 *                  - __WALL(0x40000000): 监听所有类型的进程状态变化
 *                  - __WCLONE(0x80000000): 仅等待 clone 创建的进程
 * 
 * @return pid_t 返回值含义: 
 *              - >0: 状态已变化的进程PID
 *              -  0: 非阻塞模式下, 进程仍在运行且状态未变化
 *              - -1: 调用失败, 检查errno: 
 *                    - ECHILD: 指定的 pid 进程不存在
 *                    - EINVAL: 无效的 options 参数
 *                    - EINTR:  被信号中断
 */
pid_t waitpid_wrapper(pid_t pid, int* status, int __options);

long get_page_size();

// 对齐函数
uint64_t align_up(uint64_t value, uint64_t alignment);

uint64_t align_down(uint64_t value, uint64_t alignment);

// 页对齐
uint64_t align_page_up(uint64_t value);

uint64_t align_page_down(uint64_t value);

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
bool contains_string(const std::string& src, const std::string& target, bool is_sensitivity);

// 类型转换
inline std::vector<char> str_to_vec(const std::string& str)
{
  return std::vector<char>(str.begin(), str.end());
}
inline std::string vec_to_str(const std::vector<char>& vec) 
{
  return std::string(vec.begin(), vec.end());
}

// 字符串小写
std::string to_lower(std::string str);

// 按空格分割字符串
std::vector<std::string> split_by_space(const std::string& s);

// 十六进制字符串解析
template <typename T>
std::optional<T> hex_str_to_num(const std::string& hex_str)
{
  static_assert(std::is_unsigned_v<T>, "hex_str_to_num 只支持无符号类型");

  T result = 0;
  size_t start = 0;

  // 去除前缀
  if (hex_str.size() >= 2)
  {
    std::string prefix = hex_str.substr(0, 2);
    if (prefix == "0x" || prefix == "0X")
      start = 2;
  }

  // 只有前缀
  if (start >= hex_str.size())
    return std::nullopt;

  // 逐字符解析十六进制
  for (size_t i = start; i < hex_str.size(); ++i)
  {
    char c = hex_str[i];
    T digit = 0;

    if (c >= '0' && c <= '9') 
      digit = static_cast<T>(c - '0');
    else if (c >= 'a' && c <= 'f')
      digit = static_cast<T>(10 + c - 'a');
    else if (c >= 'A' && c <= 'F')
      digit = static_cast<T>(10 + c - 'A');
    else 
      return std::nullopt;
    
    // 只做位移 + 加数字, 依赖硬件自动截断溢出
    result <<= 4; 
    result += digit;
  }

  return result;
}

template <typename T>
std::optional<std::string> num_to_hex_str(T num, bool with_prefix = true)
{
  static_assert(std::is_unsigned_v<T>, "num_to_hex_str 只支持无符号类型");

  if (num == 0) 
  {
    if (with_prefix) return "0x0";
    else return "0";
  }

  constexpr const char* hex_digits = "0123456789abcdef";
  std::string hex_str;
  
  // 从低位到高位逐位解析数值
  while (num > 0)
  {
    // 取最后 4 位
    uint8_t nibble = static_cast<uint8_t>(num & 0xF);
    hex_str.push_back(hex_digits[nibble]);
    num >>= 4;
  }
  std::reverse(hex_str.begin(), hex_str.end());

  if (with_prefix) hex_str = "0x" + hex_str;

  return hex_str;
}

}

