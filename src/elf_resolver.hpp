#pragma once

#include <elf.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>


// ARM64 特定定义
namespace ARM64 
{
  // 重定位类型
  static constexpr uint32_t R_NONE = 0;
  static constexpr uint32_t R_ABS64 = 257;
  static constexpr uint32_t R_ABS32 = 258;
  static constexpr uint32_t R_ABS16 = 259;
  static constexpr uint32_t R_GLOB_DAT = 1025;
  static constexpr uint32_t R_JUMP_SLOT = 1026;
  static constexpr uint32_t R_RELATIVE = 1027;
  static constexpr uint32_t R_IRELATIVE = 1032;

}

// 段信息封装
class Segment
{
private:

  const Elf64_Phdr* table_;
  const void* data_;
  size_t size_;

public:

  Segment(const Elf64_Phdr* table, const void* data, size_t size)
    : table_(table), data_(data), size_(size) {}
    
  uint32_t type() const { return table_ ? table_->p_type : 0; }
  uint64_t virtual_address() const { return table_ ? table_->p_vaddr : 0; }
  uint64_t file_size() const { return table_ ? table_->p_filesz : 0; }
  uint64_t memory_size() const { return table_ ? table_->p_memsz : 0; }
  uint32_t flags() const { return table_ ? table_->p_flags : 0; }
  uint64_t offset() const { return table_ ? table_->p_offset : 0; }
  uint64_t alignment() const { return table_ ? table_->p_align : 0; }
    
  bool is_readable() const { return flags() & PF_R; }
  bool is_writable() const { return flags() & PF_W; }
  bool is_executable() const { return flags() & PF_X; }
  bool is_loadable() const { return type() == PT_LOAD; }
    
  const void* data() const { return data_; }
  size_t size() const { return size_; }
};

// 节信息封装
class Section
{
private:

  const Elf64_Shdr* table_;
  std::string name_;
  const void* data_;

public:

  Section(const Elf64_Shdr* table, const char* name, const void* data)
    : table_(table), name_(name), data_(data) {}
  
  const std::string& name() const { return name_; }
  uint32_t type() const { return table_ ? table_->sh_type : 0; }
  uint64_t virtual_address() const { return table_ ? table_->sh_addr : 0; }
  uint64_t size() const { return table_ ? table_->sh_size : 0; }
  uint64_t offset() const { return table_ ? table_->sh_offset : 0; }
  uint64_t alignment() const { return table_ ? table_->sh_addralign : 0; }
  uint64_t entry_size() const { return table_ ? table_->sh_entsize : 0; }
  
  bool is_null() const { return type() == SHT_NULL; }
  bool is_progbits() const { return type() == SHT_PROGBITS; }
  bool is_nobits() const { return type() == SHT_NOBITS; }
  bool is_strtab() const { return type() == SHT_STRTAB; }
  bool is_symtab() const { return type() == SHT_SYMTAB; }
  bool is_dynsym() const { return type() == SHT_DYNSYM; }
  
  const void* data() const { return data_; }
};

// 符号信息封装
class Symbol
{ 
private:

  const Elf64_Sym* sym_;
  std::string name_;
  uint64_t value_;

public:

  Symbol(const Elf64_Sym* sym, const char* name, uint64_t value)
    : sym_(sym), name_(name), value_(value) {}
  
  const std::string& name() const { return name_; }
  uint64_t value() const { return value_; }
  uint64_t size() const { return sym_ ? sym_->st_size : 0; }
  uint32_t binding() const { return sym_ ? ELF64_ST_BIND(sym_->st_info) : 0; }
  uint32_t type() const { return sym_ ? ELF64_ST_TYPE(sym_->st_info) : 0; }
  uint16_t section_index() const { return sym_ ? sym_->st_shndx : 0; }
  
  bool is_function() const { return type() == STT_FUNC; }
  bool is_object() const { return type() == STT_OBJECT; }
  bool is_undefined() const { return section_index() == SHN_UNDEF; }
};

// 重定位信息封装
class Relocation
{
private:

  const Elf64_Rela* rela_;
  uint32_t type_;
  uint32_t sym_index_;
  std::string sym_name_;
  int64_t addend_;

public:

  Relocation(const Elf64_Rela* rela, uint32_t type, uint32_t sym_index, const std::string& sym_name, uint64_t addend)
    : rela_(rela), type_(type), sym_index_(sym_index), sym_name_(sym_name), addend_(addend) {}
    
    uint64_t offset() const { return rela_ ? rela_->r_offset : 0; }
    uint32_t type() const { return type_; }
    uint32_t symbol_index() const { return sym_index_; }
    const std::string& symbol_name() const { return sym_name_; }
    int64_t addend() const { return rela_ ? rela_->r_addend : 0; }
};

class ELFResolver
{
private:

  // 指向文件数据
  const uint8_t* data_;
  // 保存文件数据(如果需要)
  std::vector<uint8_t> fill_data_;
  // 文件大小
  size_t size_;
  // 是否已加载
  bool is_loaded_;
  // 是否有效 ELF 文件
  bool is_valid_;
  
  // ELF 头部
  const Elf64_Ehdr* header_;
  // 程序头表
  const Elf64_Phdr* phdr_table_;
  // 节头表
  const Elf64_Shdr* shdr_table_;
  
  // 节头表名称
  const char* shstrtab_;
  // 动态字符串表
  const char* dynstr_;
  // 动态符号项
  const Elf64_Sym* dynsym_;
  // 重定位项(PLT)
  const Elf64_Rela* rela_plt_;
  // 重定位项(DYN)
  const Elf64_Rela* rela_dyn_;
  
  // 重定位项(PLT)数量
  size_t rela_plt_count_;
  // 重定位项(DYN)数量
  size_t rela_dyn_count_;
  
public:
  ELFResolver();
  ~ELFResolver();
  
  // 禁止拷贝
  ELFResolver(const ELFResolver&) = delete;
  ELFResolver& operator=(const ELFResolver&) = delete;
  
  // 移动语义
  ELFResolver(ELFResolver&& other) noexcept;
  ELFResolver& operator=(ELFResolver&& other) noexcept;

  // 读取 ELF 信息
  bool load(const void* data, size_t size);
  bool load(const std::string& filename);

  // 清理资源
  void cleanup();

  // 状态查询
  bool is_loaded() const { return is_loaded_; }
  bool is_valid() const { return is_valid_; }

  // 检查文件类型
  bool is_executable() const;
  bool is_shared_library() const;

  // 入口地址
  uint64_t entry_point() const;
  // 段数量
  uint16_t segment_count() const;
  // 节数量
  uint16_t section_count() const;

  // 段操作
  std::vector<Segment> segments() const;
  Segment segment(uint16_t index) const;
  Segment find_segment(uint32_t type) const;
  std::vector<Segment> loadable_segments() const;

  // 节操作
  std::vector<Section> sections() const;
  Section section(uint16_t index) const;
  Section find_section(const std::string& name, uint32_t type = 0) const;

  // 符号段操作
  std::vector<Symbol> symbols() const;
  Symbol find_symbol(const std::string& name) const;

  // 重定位信息操作
  std::vector<Relocation> relocations() const;
  bool apply_relocations(void* load_base) const;

private:

  // 检验 ELF 文件格式
  bool validate_elf() const;

  // 解析动态段
  bool parse_dynamic_segment();
};