#pragma once 

#include <stddef.h>
#include <unordered_map>
#include <vector>

#include "elf_resolver.hpp"
#include "memory_control.hpp"

struct LoadInfo
{
  void* base_address;               // 加载基址
  size_t load_size;                 // 加载大小
  uint64_t entry_point;             // 入口点
  std::vector<void*> segments;      // 已加载的段地址
  std::vector<MemoryRegion> memory_regions;
};

class ELFLoader
{
public:
  using SymbolTable = std::unordered_map<std::string, void*>;
  using Dependencies = std::vector<std::shared_ptr<ELFLoader>>;
  
  ELFLoader() = default;
  ~ELFLoader();
  
  // 禁止拷贝
  ELFLoader(const ELFLoader&) = delete;
  ELFLoader& operator=(const ELFLoader&) = delete;
  
  // 允许移动
  ELFLoader(ELFLoader&& other) noexcept;
  ELFLoader& operator=(ELFLoader&& other) noexcept;

private:
  ELFResolver resolver_;
  LoadInfo load_info_;
  SymbolTable symbol_table_;
  Dependencies dependencies_;

public:
  // 加载 ELF 文件
  LoadInfo load_elf_binary(const std::string& filename, void* base_addr = nullptr);
  LoadInfo load_elf_binary(const void* data, size_t size, void* base_addr = nullptr);

  // 解析符号并链接
  bool link(const SymbolTable& external_symbols = {});

  // 获取加载信息
  const LoadInfo& get_load_info() const { return load_info_; }

  // 获取符号表
  const SymbolTable& get_symbols() const { return symbol_table_; }

  // 获取依赖项
  const Dependencies& get_dependencies() const { return dependencies_; }

  // 添加依赖项
  void add_dependency(std::shared_ptr<ELFLoader> dep) { dependencies_.push_back(dep); }

  // 查找符号
  void* find_symbol(const std::string& name) const;

private:
  // 加载段
  bool load_segments(void* base_addr);

  // 处理动态段
  bool process_dynamic_segment(void* base_addr);

  // 处理重定位
  bool process_relocations(void* base_addr, const SymbolTable& external_symbols);

  // 处理单个重定位条目
  bool apply_relocation(const Relocation& reloc, void* base_addr, const SymbolTable& symbol_table, void* reloc_addr);

  // 查找符号地址
  void* resolve_symbol_address(const std::string& name, const SymbolTable& symbol_table) const;

  // 构建内部符号表
  void build_symbol_table(void* base_addr);
};