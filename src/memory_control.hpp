#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <vector>
#include <sys/mman.h> 

#include "fmt/format.h"
#include "singleton_base.hpp"

struct MemoryRegion
{
  uint64_t start_address;
  uint64_t end_address;
  uint64_t size;
  std::string permissions;  // 内存区域的访问权限标志
  std::string pathname;     // 映射的文件路径或区域描述

  bool is_readable() const { return permissions.find('r') != std::string::npos; }
  bool is_writable() const { return permissions.find('w') != std::string::npos; }
  bool is_executable() const { return permissions.find('x') != std::string::npos; }
  bool is_private() const { return permissions.find('p') != std::string::npos; }
  bool is_shared() const { return permissions.find('s') != std::string::npos; }

  std::string to_string() const 
  {
    return fmt::format("{:016x}-{:016x} {} {}", 
      start_address, end_address, permissions, pathname);
  }

  bool contains(uint64_t address) const {
    return address >= start_address && address < end_address;
  }
};

class MemoryControl : public SingletonBase<MemoryControl>
{
private:

  // 友元声明, 允许基类访问子类的私有构造函数
  friend class SingletonBase<MemoryControl>; 

  // 私有化构造函数, 析构函数
  MemoryControl() = default;
  ~MemoryControl() = default;

  // 使用 ptrace 读取内存
  bool read_memory_ptrace(pid_t pid, uint64_t address, void* buffer, size_t size);

  // 使用 ptrace 写入内存
  bool write_memory_ptrace(pid_t pid, uint64_t address, const void* buffer, size_t size);

  // maps 解析器
  bool parse_maps_line(const std::string& line, MemoryRegion& region);

  // 检查内存地址权限
  bool check_address_permission(pid_t pid, uint64_t address, size_t size, bool need_write);

public:

  // 读取内存
  bool read_memory(pid_t pid, uint64_t address, void* buffer, size_t size);

  // 写入内存
  bool write_memory(pid_t pid, uint64_t address, const void* buffer, size_t size);

  // 获取内存布局, 返回结果地址升序排列
  std::vector<MemoryRegion> get_memory_regions(pid_t pid);

  // 搜索内存
  std::vector<uint64_t> search_memory(pid_t pid, const std::vector<uint8_t>& pattern);

  // 转储内存到文件
  bool dump_memory(pid_t pid, uint64_t start_address, uint64_t end_address, const std::string& filename);
  
  // 在目标进程中分配内存
  uint64_t allocate_memory(pid_t pid, size_t size, int prot = PROT_READ | PROT_WRITE);

  // 在目标进程中释放内存
  bool free_memory(pid_t pid, uint64_t address, size_t size);
};
