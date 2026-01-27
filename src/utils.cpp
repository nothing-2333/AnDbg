#include <dirent.h>
#include <vector>

#include "utils.hpp"
#include "log.hpp"

namespace Utils 
{

// 全局字节序判断
static const bool IS_LITTLE_ENDIAN = []() 
{
  const int endian_check = 1;
  // 小端序特征: 低地址存储低位字节(0x01), 大端序存储高位字节(0x00)
  return *reinterpret_cast<const unsigned char*>(&endian_check) == 1;
}();

// ptrace 包装
bool ptrace_wrapper(int request, pid_t pid, void *address, void* data, size_t data_size, long* result)
{
  // 参数检查
  if (pid == -1)
  {
    LOG_ERROR("传入无效的 pid");
    return false;
  }

  long int ret = 0;

  // 重置 errno
  errno = 0;

  if (request == PTRACE_GETREGSET || request == PTRACE_SETREGSET)
  {
    if (address == nullptr) 
    {
      LOG_ERROR("PTRACE_GETREGSET/SETREGSET 传入的 address 不能为空");
      ret = -1;
    }
    else  
    {
      ret = ptrace(request, static_cast<::pid_t>(pid), *reinterpret_cast<unsigned int*>(address), data);
    }
  }
  else  
    ret = ptrace(request, static_cast<::pid_t>(pid), address, data);

  // 记录日志
  LOG_DEBUG("ptrace(request: {}, pid: {}, address: {:p}, data: {:p}, data_size: {}, ret: 0x{:x})",
    request, pid, static_cast<const void*>(address), static_cast<const void*>(data), data_size, ret
  );

  // 如果有返回值保存返回值
  if (result != nullptr) *result = ret;

  
  if (ret == -1 && errno != 0) 
  {
    LOG_ERROR("ptrace 调用失败, errno: {}, 错误信息: {}", errno, strerror(errno));
    return false;
  }
  else return true;;
}

// waitpid 包装
bool waitpid_wrapper(pid_t pid, int* status, int __options)
{
  pid_t wpid = waitpid(pid, status, __options);

  LOG_DEBUG("等待进程 pid: {} 完成, 返回值: {}", pid, wpid);

  if (wpid == -1) 
  {
    LOG_ERROR("停止失败: {}", std::string(strerror(errno)));
    return false;
  }
  return true;
}


long get_page_size()
{
  static long page_size = sysconf(_SC_PAGE_SIZE);
  if (page_size <= 0) 
  {
    LOG_WARNING("获取系统页面大小失败, 使用默认大小 4096 字节");
    page_size = 4096;
  }
  return page_size;
}

// 对齐函数
uint64_t align_up(uint64_t value, uint64_t alignment)
{
  return (value + alignment - 1) & ~(alignment - 1);
}

uint64_t align_down(uint64_t value, uint64_t alignment)
{
  return value & ~(alignment - 1);
}

// 页对齐
uint64_t align_page_up(uint64_t value)
{
  return align_up(value, static_cast<uint64_t>(get_page_size()));
}

uint64_t align_page_down(uint64_t value)
{
  return align_down(value, static_cast<uint64_t>(get_page_size()));
}

// 判断是否是 sigtrap 信号, 在调试时, ptrace 相关操作通常会触发 SIGTRAP 信号
bool is_sigtrap(int status)
{
  return WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP;
}

// 等待 sigtrap 信号
bool waitpid_sigtrap(pid_t pid)
{
  int status = 0;
  if (!Utils::waitpid_wrapper(pid, &status, WUNTRACED))
    return false;
  return is_sigtrap(status);
}

// 用 ptrace 执行 syscall 指令
bool syscall_wrapper(pid_t pid)
{
  // 第一次 PTRACE_SYSCALL: 触发进程进入系统调用
  if (!Utils::ptrace_wrapper(PTRACE_SYSCALL, pid, nullptr, nullptr, 0))
  {
    LOG_ERROR("进程 {}: 第一次 PTRACE_SYSCALL 失败", pid);
    return false;
  }
  // 等待进程暂停
  if (!waitpid_sigtrap(pid))
  {
    LOG_ERROR("进程 {}: 等待第一次暂停失败", pid);
    return false;
  }

  // 第二次 PTRACE_SYSCALL: 触发进程退出系统调用
  if (!Utils::ptrace_wrapper(PTRACE_SYSCALL, pid, nullptr, nullptr, 0))
  {
    LOG_ERROR("进程 {}: 第二次 PTRACE_SYSCALL 失败", pid);
    return false;
  }
  // 等待进程暂停
  if (!waitpid_sigtrap(pid))
  {
    LOG_ERROR("进程 {}: 等待第二次暂停失败", pid);
    return false;
  }

  return true;
}

// 无符号整数转大端序（入参类型=返回类型）
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

bool contains_string(const std::string& src, const std::string& target, MatchMode mode)
{
  if (target.empty()) return false;

  switch (mode) 
  {
    case MatchMode::INSENSITIVE: 
      return std::search(src.begin(), src.end(), target.begin(), target.end(),
         [](char a, char b) 
        {
          return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); 
        }
      ) != src.end();
    case MatchMode::SENSITIVE:
    default: return src.find(target) != std::string::npos;
  }
}

}
