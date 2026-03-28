#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <linux/uio.h>
#include <sstream>
#include <string>
#include <sys/uio.h>
#include <vector>
#include <sys/syscall.h>
#include <unistd.h>

#include "memory_control.hpp"
#include "log.hpp"
#include "fmt/format.h"
#include "register_control.hpp"
#include "utils.hpp"
#include "process.hpp"


namespace Core 
{

bool MemoryControl::read_memory_ptrace(pid_t pid, uint64_t address, void* buffer, size_t size)
{
  uint8_t* byte_buffer = static_cast<uint8_t*>(buffer);
  size_t bytes_read = 0;

  while (bytes_read < size)
  {
    long word;
    if (Utils::ptrace_wrapper(PTRACE_PEEKDATA, pid, 
      reinterpret_cast<void*>(address + bytes_read), nullptr, 0, &word))
      return false;
      
    size_t copy_size = std::min(sizeof(word), size - bytes_read);
    memcpy(byte_buffer + bytes_read, &word, copy_size);
    bytes_read += copy_size;
  }

  return true;
}

bool MemoryControl::write_memory_ptrace(pid_t pid, uint64_t address, const void* buffer, size_t size)
{
  const uint8_t* byte_buffer = static_cast<const uint8_t*>(buffer);
  size_t bytes_written = 0;

  while (bytes_written < size) 
  {
    long word = 0;
    size_t remain_size = size - bytes_written;

    // 如果不足一个字, 需要先读取原始数据
    if (remain_size < sizeof(word))
    {
      if (Utils::ptrace_wrapper(PTRACE_PEEKDATA, pid, 
      reinterpret_cast<void*>(address + bytes_written), nullptr, 0, &word))
        return false;
    }

    size_t copy_size = std::min(sizeof(word), remain_size);
    memcpy(&word, byte_buffer + bytes_written, copy_size);

    if (Utils::ptrace_wrapper(PTRACE_POKEDATA, pid, 
      reinterpret_cast<void*>(address + bytes_written), reinterpret_cast<void*>(word), sizeof(word)))
      return false;

    bytes_written += copy_size;
  }

  return true;
}

std::vector<MemoryRegion> MemoryControl::get_memory_regions(pid_t pid)
{
  std::vector<MemoryRegion> regions;

  static std::optional<Process::ProcFile> maps_file = Process::ProcFile::open(pid, Process::ProcFileType::MAPS);
  if (!maps_file || !maps_file->is_open()) 
  {
    LOG_ERROR("解析进程状态失败: 无法打开/proc/{}/maps", pid);
    return regions;
  }

  std::vector<std::string> lines = maps_file.value().read_lines();
  for (const auto& line : lines)
  {
    if (line.empty()) continue;

    MemoryRegion region;
    if (parse_maps_line(line, region))
    {
      regions.push_back(region);
    }
  }

  // 按起始地址排序
  std::sort(regions.begin(), regions.end(),
    [](const MemoryRegion& a, const MemoryRegion& b) {
      return a.start_address < b.start_address;
  });

  return regions;
}

bool MemoryControl::parse_maps_line(const std::string& line, MemoryRegion& region)
{
  std::istringstream iss(line);
  std::string address_range;
  iss >> address_range;

  // 解析地址范围
  size_t hyphen_position = address_range.find('-');
  if (hyphen_position == std::string::npos)
  {
    LOG_ERROR("地址范围格式错误, 缺少 '-': {}", line);
    return false;
  }

  try 
  {
    region.start_address = std::stoull(address_range.substr(0, hyphen_position), nullptr, 16);
    region.end_address = std::stoull(address_range.substr(hyphen_position + 1), nullptr, 16);
    region.size = region.end_address - region.start_address;

    // 验证区域有效性
    if (region.size == 0)
    {
      LOG_WARNING("跳过大小为 0 的内存区域: {}", line);
      return false;
    }

    // 结束地址必须大于起始地址
    if (region.end_address <= region.start_address)
    {
      LOG_ERROR("地址范围无效，结束地址 <= 起始地址: {}", address_range);
      return false;
    }
  } 
  catch (const std::invalid_argument& e) 
  {
    LOG_ERROR("地址解析失败, 非十六进制: {} | 错误: {}", address_range, e.what());
    return false;
  }
  catch (const std::out_of_range& e)
  {
    LOG_ERROR("地址超出 uint64_t 范围: {} | 错误: {}", address_range, e.what());
    return false;
  }

  // 验证权限字段格式
  if (region.permissions.empty() || region.permissions.size() > 5)
    LOG_WARNING("权限字段格式异常: {} | 行内容: {}", region.permissions, line);

  // 解析路径名
  std::getline(iss >> std::ws, region.pathname);

  // 处理空路径名的情况
  if (region.pathname.empty()) region.pathname = "[anonymous]";

  // 额外的完整性检查
  if (iss.fail() && !iss.eof()) LOG_WARNING("解析 maps 行时遇到流错误: {}", line);

  return true;
}

bool MemoryControl::read_memory(pid_t pid, uint64_t address, void* buffer, size_t size)
{
  if (buffer == nullptr || size == 0)
  {
    LOG_ERROR("错误的参数");
    return false;
  }

  // 使用 process_vm_readv 进行高效读取
  struct iovec loval_iov = {buffer, size};
  struct iovec remote_iov = {reinterpret_cast<void*>(address), size};
  ssize_t ret = process_vm_readv(pid, &loval_iov, 1, &remote_iov, 1, 0);
  if (ret == static_cast<ssize_t>(size)) return true;

  // 如果 process_vm_readv 失败, 回退到 ptrace
  LOG_WARNING("process_vm_readv 失败, 使用 ptrace");
  return read_memory_ptrace(pid, address, buffer, size);
}

bool MemoryControl::write_memory(pid_t pid, uint64_t address, const void* buffer, size_t size)
{
  if (buffer == nullptr || size == 0)
  {
    LOG_ERROR("错误的参数");
    return false;
  }

  // 使用 process_vm_writev 进行高效写入
  struct iovec loval_iov = {const_cast<void*>(buffer), size};
  struct iovec remote_iov = {reinterpret_cast<void*>(address), size};
  ssize_t ret = process_vm_writev(pid, &loval_iov, 1, &remote_iov, 1, 0);
  if (ret == static_cast<ssize_t>(size)) return true;

  // 如果 process_vm_writev 失败, 回退到 ptrace
  LOG_WARNING("process_vm_writev 失败, 使用 ptrace");
  return write_memory_ptrace(pid, address, buffer, size);
}

uint64_t MemoryControl::allocate_memory(pid_t pid, size_t size, uint64_t address, int prot)
{
  // 基础参数校验
  if (pid <= 0) 
  {
    LOG_ERROR("无效的 PID: {}", pid);
    return 0;
  }
  if (size == 0) 
  {
    LOG_ERROR("分配内存大小不能为 0");
    return 0;
  }
  if (address < 0)
  {
    LOG_ERROR("无效的地址: {}", address);
    return 0;
  }
  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_NONE)) != 0)
  {
    LOG_ERROR("无效内存保护属性 prot: 0x{:x}", prot);
    return 0;
  }

  // 内存大小页对齐处理
  size = Utils::align_page_up(size);

  // 保存原始寄存器
  auto gpr_opt = RegisterControl::get_instance().get_all_gpr(pid);
  if (!gpr_opt) 
  {
    LOG_ERROR("获取进程 {} 寄存器失败", pid);
    return 0;
  }
  const auto& gpr = gpr_opt.value();

  // 准备 mmap 系统调用参数
  struct user_pt_regs regs = {0};
  memcpy(&regs, &gpr, sizeof(user_pt_regs));  
  regs.regs[8] = __NR_mmap;                   // 系统调用号
  regs.regs[0] = address;                     // 默认是 0, 内核自动分配地址
  regs.regs[1] = static_cast<uint64_t>(size); // 分配大小
  regs.regs[2] = static_cast<uint64_t>(prot); // 内存保护属性
  regs.regs[3] = MAP_PRIVATE | MAP_ANONYMOUS; // 匿名私有映射
  regs.regs[4] = static_cast<uint64_t>(-1);   // 匿名映射无需文件描述符
  regs.regs[5] = 0;                           // 页偏移量

  // 设置寄存器
  if (!RegisterControl::get_instance().set_all_gpr(pid, regs))
  {
    LOG_ERROR("设置进程 {} 寄存器失败", pid);
    // 恢复原始寄存器
    RegisterControl::get_instance().set_all_gpr(pid, gpr); 
    return 0;
  }

  // 执行系统调用
  if (!Utils::syscall_wrapper(pid))
  {
    LOG_ERROR("进程 {} 执行 mmap2 系统调用失败", pid);
    RegisterControl::get_instance().set_all_gpr(pid, gpr); 
    return 0;
  }

  // 获取系统调用返回值
  auto result_gpr_opt = RegisterControl::get_instance().get_all_gpr(pid);
  if (!result_gpr_opt)
  {
    LOG_ERROR("获取进程 {} 系统调用返回寄存器失败", pid);
    RegisterControl::get_instance().set_all_gpr(pid, gpr); 
    return 0; 
  }
  const auto& result_gpr = result_gpr_opt.value();
  uint64_t mmap_address = result_gpr.regs[0];

  // 校验返回值
  int64_t signed_address = static_cast<int64_t>(mmap_address);
  if (signed_address < 0 && signed_address >= -4095)
  {
    int error = static_cast<int>(-signed_address);
    char err_buf[256] = {0};
    strerror_r(error, err_buf, sizeof(err_buf));
    LOG_ERROR("进程 {}: mmap2 系统调用失败, 大小: {} 字节, prot: 0x{:x}, 错误: {} ({})", 
      pid, size, prot, error, err_buf);
    RegisterControl::get_instance().set_all_gpr(pid, gpr);
    return 0;
  }

  // 恢复原始寄存器
  if (!RegisterControl::get_instance().set_all_gpr(pid, gpr))
  {
    LOG_ERROR("恢复进程 {} 寄存器失败", pid);
    return 0; 
  }

  LOG_DEBUG("在进程 {} 中分配内存成功, 地址: 0x{:x}, 大小: {} 字节, prot: 0x{:x}", pid, mmap_address, size, prot);
  return mmap_address;
}

bool MemoryControl::deallocate_memory(pid_t pid, uint64_t address, size_t size)
{
  // 参数校验
  if (pid <= 0) 
  {
    LOG_ERROR("无效的 PID: {}", pid);
    return false;
  }
  if (size == 0)
  {
    LOG_ERROR("释放内存大小不能为 0");
    return false;
  }
  if (address == 0 || reinterpret_cast<void*>(address) == MAP_FAILED)
  {
      LOG_ERROR("无效的内存地址: 0x{:x}", address);
      return false;
  }

  // 对齐检查, munmap 要求地址和大小都页对齐
  size = Utils::align_page_up(size);
  address = Utils::align_page_up(address);

  // 保存原始寄存器
  auto gpr_opt = RegisterControl::get_instance().get_all_gpr(pid);
  if (!gpr_opt) 
  {
      LOG_ERROR("获取进程 {} 寄存器失败", pid);
      return false;
  }
  const auto& gpr = gpr_opt.value();

  // 准备 munmap 系统调用参数
  struct user_pt_regs regs = {0};
  memcpy(&regs, &gpr, sizeof(user_pt_regs));

  regs.regs[8] = __NR_munmap;                       // 系统调用号
  regs.regs[0] = static_cast<uint64_t>(address);    // 要释放的地址
  regs.regs[1] = static_cast<uint64_t>(size);       // 要释放的大小  

  // 设置寄存器
  if (!RegisterControl::get_instance().set_all_gpr(pid, regs))
  {
    LOG_ERROR("设置进程 {} 寄存器失败", pid);
    RegisterControl::get_instance().set_all_gpr(pid, gpr); 
    return false;
  }

  // 执行系统调用
  if (!Utils::syscall_wrapper(pid))
  {
    LOG_ERROR("进程 {} 执行 munmap 系统调用失败", pid);
    RegisterControl::get_instance().set_all_gpr(pid, gpr);
    return false;
  }

  // 获取系统调用返回值
  auto result_gpr_opt = RegisterControl::get_instance().get_all_gpr(pid);
  if (!result_gpr_opt)
  {
    LOG_ERROR("获取进程 {} 系统调用返回寄存器失败", pid);
    RegisterControl::get_instance().set_all_gpr(pid, gpr); 
    return false;
  }
  const auto& result_gpr = result_gpr_opt.value();
  uint64_t munmap_result = result_gpr.regs[0];

  // 校验返回值
  int64_t signed_result = static_cast<int64_t>(munmap_result);
  if (signed_result != 0) 
  {
    int error = static_cast<int>(-signed_result);
    char err_buf[256] = {0};
    strerror_r(error, err_buf, sizeof(err_buf));
    LOG_ERROR("进程 {}: munmap 系统调用失败, 地址: 0x{:x}, 大小: {} 字节, 错误: {} ({})", 
      pid, address, size, error, err_buf);
    RegisterControl::get_instance().set_all_gpr(pid, gpr);
    return false;
  }

  // 恢复原始寄存器
  if (!RegisterControl::get_instance().set_all_gpr(pid, gpr))
  {
    LOG_ERROR("恢复进程 {} 寄存器失败", pid);
    return false; 
  }

  LOG_DEBUG("在进程 {} 中释放内存成功, 地址: 0x{:x}, 大小: {} 字节", pid, address, size);
  return true;
}

}
