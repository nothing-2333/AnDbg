#pragma once

#include <cstdio>
#include <memory>
#include <optional>
#include <unordered_map>

#include "elf_resolver.hpp"

// 重定位类型定义
enum class RelocationType : uint32_t
{
  R_NONE = 0,
  R_ABS64 = 257,
  R_ABS32 = 258,
  R_ABS16 = 259,
  R_GLOB_DAT = 1025,
  R_JUMP_SLOT = 1026,
  R_RELATIVE = 1027,
  R_IRELATIVE = 1032
};

// 注入的 ELF 信息
struct InjectedELFInfo
{
  ELFResolver elf;      // ELF 解析器实例
  uint64_t load_base;   // ELF 加载基址
  size_t size;          // ELF 大小
  std::unordered_map<std::string, uint64_t> symbol_addresses; // 符号地址映射
};

class ELFInjector 
{
private: 

public:

  ELFInjector();
  ~ELFInjector();

  // 注入 ELF 到目标进程
  std::optional<InjectedELFInfo> inject_elf(pid_t pid, const ELFResolver& elf, uint64_t preferred_base = 0);

  // 查找符号地址
  std::optional<uint64_t> find_symbol_address(uint64_t load_base, const std::string& symbol_name) const;

private:

  // 加载 ELF 到目标进程地址空间
  bool load_elf(pid_t pid, const ELFResolver& elf, uint64_t preferred_base);

  // 链接 ELF
  bool link_elf(pid_t pid, const ELFResolver& elf, uint64_t load_base, InjectedELFInfo& info);

  // 应用重定位
  bool apply_relocations(pid_t pid, const ELFResolver& elf, uint64_t load_base, InjectedELFInfo& info) const;

  // 应用单个重定位
  bool apply_relocation(pid_t pid, const ELFResolver& elf, uint64_t load_base, const Relocation& relocation) const;

  // 解析符号地址
  bool resolve_symbols(pid_t pid, const ELFResolver& elf) const;
};