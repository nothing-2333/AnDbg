#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "memory_manager.hpp"
#include "Log.hpp"
#include "fmt/format.h"
#include "utils.hpp"

bool MemoryManager::read_memory_ptrace(pid_t pid, uint64_t address, void* buffer, size_t size)
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

bool MemoryManager::write_memory_ptrace(pid_t pid, uint64_t address, void* buffer, size_t size)
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

std::vector<MemoryRegion> MemoryManager::get_memory_regions(pid_t pid)
{
  std::vector<MemoryRegion> regions;

  std::string maps_path = fmt::format("/proc/{}/maps", pid);
  std::ifstream maps_file(maps_path);
  if (!maps_file.is_open())
  {
    LOG_ERROR("打开 {} 失败: {}", maps_path, strerror(errno));
    return regions;
  }

  std::string line;
  while (std::getline(maps_file, line)) 
  {
    if (line.empty()) continue;

    MemoryRegion region;
    if (parse_maps_line(line, region))
    {
      regions.push_back(region);
    }
  }

  return regions;
}

bool MemoryManager::parse_maps_line(const std::string& line, MemoryRegion& region)
{
  std::istringstream iss(line);
  std::string address_range;
  std::string offset_string;
  std::string inode_string;

  if (!(iss >> address_range >> region.permissions >> offset_string >> region.dev >> inode_string))
  {
    LOG_ERROR("解析 maps 行失败, 字段不足: {}", line);
    return false;
  }

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
    region.size = region.end_address = region.start_address;

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

  // 解析文件偏移量
  try 
  {
    region.file_offset = std::stoull(offset_string, nullptr, 16);
  } 
  catch (const std::invalid_argument& e) 
  {
    LOG_ERROR("偏移量解析失败, 非十六进制: {} | 错误: {}", offset_string, e.what());
    return false;
  }
  catch (const std::out_of_range& e)
  {
    LOG_ERROR("偏移量超出 uint64_t 范围: {} | 错误: {}", offset_string, e.what());
    return false;
  }

  // 解析 inode 编号
  try 
  {
      region.inode = std::stoull(inode_string);
  } 
  catch (const std::invalid_argument& e) 
  {
    LOG_ERROR("inode 解析失败, 非数字: {} | 行内容: {}", inode_string, line);
    return false;
  } 
  catch (const std::out_of_range& e) 
  {
    LOG_ERROR("inode 超出 uint64_t 范围: {} | 行内容: {}", inode_string, line);
    return false;
  }

  // 解析路径名
  std::getline(iss >> std::ws, region.pathname);

  // 处理空路径名的情况
  if (region.pathname.empty()) region.pathname = "[anonymous]";

  // 额外的完整性检查
  if (iss.fail() && !iss.eof()) LOG_WARNING("解析 maps 行时遇到流错误: {}", line);

  return true;
}