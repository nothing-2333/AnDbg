#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <linux/uio.h>
#include <sstream>
#include <string>
#include <sys/uio.h>
#include <vector>
#include <sys/syscall.h>
#include <unistd.h>
#include "memory_control.hpp"
#include "log.hpp"
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
    if (!Utils::ptrace_wrapper(PTRACE_PEEKDATA, pid, 
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
      if (!Utils::ptrace_wrapper(PTRACE_PEEKDATA, pid, 
      reinterpret_cast<void*>(address + bytes_written), nullptr, 0, &word))
        return false;
    }

    size_t copy_size = std::min(sizeof(word), remain_size);
    memcpy(&word, byte_buffer + bytes_written, copy_size);

    if (!Utils::ptrace_wrapper(PTRACE_POKEDATA, pid, 
      reinterpret_cast<void*>(address + bytes_written), reinterpret_cast<void*>(&word), sizeof(word)))
      return false;

    bytes_written += copy_size;
  }

  return true;
}

std::vector<MemoryRegion> MemoryControl::get_memory_regions(pid_t pid)
{
  std::vector<MemoryRegion> regions;

  std::optional<Process::ProcFile> maps_file = Process::ProcFile::open(pid, Process::ProcFileType::MAPS);
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

  LOG_DEBUG("pid {} 找到 {} 个内存区域", pid, regions.size());

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
  
  iss >> region.permissions;
  if (region.permissions.empty() || region.permissions.size() > 5)
    LOG_WARNING("权限字段格式异常: {} | 行内容: {}", region.permissions, line);

  std::string offset_str;
  iss >> offset_str;
  region.offset = std::stoull(offset_str, nullptr, 16);

  iss >> region.device;

  std::string inode_str;
  iss >> inode_str;
  region.inode = std::stoull(inode_str);

  // 解析路径名
  std::getline(iss >> std::ws, region.pathname);

  // 处理空路径名的情况
  if (region.pathname.empty()) region.pathname = "[anonymous]";

  return true;
}

bool MemoryControl::read_memory(pid_t pid, uint64_t address, void* buffer, size_t size)
{
  // 使用 process_vm_readv 进行高效读取
  struct iovec loval_iov = {buffer, size};
  struct iovec remote_iov = {reinterpret_cast<void*>(address), size};
  ssize_t ret = process_vm_readv(pid, &loval_iov, 1, &remote_iov, 1, 0);
  if (ret == static_cast<ssize_t>(size)) return true;

  LOG_ERROR("process_vm_readv 失败 | pid: {} | addr: 0x{:x} | 大小: {} | 错误: {} ({})",
  pid, address, size, strerror(errno), errno);

  // 如果 process_vm_readv 失败, 回退到 ptrace
  LOG_WARNING("process_vm_readv 失败, 使用 ptrace");
  return read_memory_ptrace(pid, address, buffer, size);
}

bool MemoryControl::write_memory(pid_t pid, uint64_t address, const void* buffer, size_t size)
{
  // 使用 process_vm_writev 进行高效写入
  
  struct iovec local_iov = {const_cast<void*>(buffer), size};
  struct iovec remote_iov = {reinterpret_cast<void*>(address), size};
  ssize_t ret = process_vm_writev(pid, &local_iov, 1, &remote_iov, 1, 0);
  if (ret == static_cast<ssize_t>(size)) return true;

  LOG_ERROR("process_vm_writev 失败 | pid: {} | addr: 0x{:x} | 大小: {} | 错误: {} ({})",
  pid, address, size, strerror(errno), errno);

  // 如果 process_vm_writev 失败, 回退到 ptrace
  LOG_WARNING("process_vm_writev 失败, 使用 ptrace");
  return write_memory_ptrace(pid, address, buffer, size);
}

}
