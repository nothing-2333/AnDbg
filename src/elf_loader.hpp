#pragma once 

#include <cstddef>
#include <cstdint>
#include <stddef.h>
#include <unordered_map>
#include <vector>

#include "elf_resolver.hpp"
#include "memory_control.hpp"

using SymbolTable = std::unordered_map<std::string, uint64_t>;

// 加载信息结构
struct LoadInfo
{
  uint64_t load_base;                         // 加载基地址
  uint64_t entry_point;                       // 程序入口点
  std::vector<MemoryRegion> loaded_segments;  // 已加载的段
  SymbolTable symbol_table;                   // 符号表
};


class ELFLoader
{
public:
  ELFLoader();
  ~ELFLoader() = default;
  
  // 禁止拷贝
  ELFLoader(const ELFLoader&) = delete;
  ELFLoader& operator=(const ELFLoader&) = delete;
  

private:
  MemoryControl* memory_control_;

public:
  // 加载 ELF 文件
  LoadInfo load_elf(const std::string& filename, pid_t target_pid = -1, uint64_t preferred_base = 0, SymbolTable external_symbols = {});
  LoadInfo load_elf(std::vector<uint8_t> file_data, pid_t target_pid = -1, uint64_t preferred_base = 0, SymbolTable external_symbols = {});

  // 卸载 elf
  void unload_elf(pid_t target_pid, LoadInfo& info);


private:
  // 实际实现的函数
  LoadInfo load_elf(pid_t target_pid, const ELFResolver& resolver, uint64_t base_addr, SymbolTable external_symbols);

  // 确定加载基地址
  std::optional<uint64_t> determine_load_base(pid_t target_pid, const ELFResolver& resolver, uint64_t preferred_base);

  // 计算要加载到内存的总大小 
  size_t calculate_load_segments_total_size(const ELFResolver& resolver);

  // 找到一个大小足够的内存空间
  std::optional<uint64_t> find_available_address(pid_t target_pid, uint64_t preferred_base, size_t total_size);

  // 加载段到内存, 
  std::optional<uint64_t> load_segments(pid_t target_pid, const ELFResolver& resolver, uint64_t load_base, LoadInfo& info);
  
  // 应用重定位
  bool apply_relocations(pid_t target_pid, const ELFResolver& resolver, uint64_t load_base, LoadInfo& info, const SymbolTable& external_symbols);
  
  // 应用单个重定位
  bool apply_relocation(pid_t target_pid, const Relocation& relocation, uint64_t load_base, LoadInfo& info, const SymbolTable& external_symbols, const ELFResolver& resolver);
  
  // 解析符号地址
  uint64_t resolve_symbol(const ELFResolver& resolver, const std::string& name, uint64_t load_base, LoadInfo& info, const SymbolTable& external_symbols);
};