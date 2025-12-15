#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream> 
#include <linux/elf.h>
#include <vector>
#include <string>

#include "elf_resolver.hpp"
#include "log.hpp"

ELFResolver::ELFResolver()
  : data_(nullptr), size_(0), is_valid_(false),
    header_(nullptr), phdr_table_(nullptr), shdr_table_(nullptr),
    shstrtab_(nullptr), dynstr_(nullptr), dynsym_(nullptr), sym_entry_size_(0),
    rela_plt_(nullptr), rela_dyn_(nullptr),
    rela_plt_count_(0), rela_dyn_count_(0) {}

ELFResolver::~ELFResolver()
{
  cleanup();
}

ELFResolver::ELFResolver(ELFResolver&& other) noexcept
  : data_(other.data_),
    size_(other.size_),
    is_valid_(other.is_valid_),
    header_(other.header_),
    phdr_table_(other.phdr_table_),
    shdr_table_(other.shdr_table_),
    shstrtab_(other.shstrtab_),
    dynstr_(other.dynstr_),
    dynsym_(other.dynsym_),
    rela_plt_(other.rela_plt_),
    rela_dyn_(other.rela_dyn_),
    rela_plt_count_(other.rela_plt_count_),
    rela_dyn_count_(other.rela_dyn_count_),
    file_data_(std::move(other.file_data_)),
    segments_cache(std::move(other.segments_cache)),
    sections_cache(std::move(other.sections_cache)),
    symbols_cache(std::move(other.symbols_cache)),
    relocations_cache(std::move(other.relocations_cache))
{
  other.cleanup();
}

void ELFResolver::cleanup()
{
  // 清理指针, 属性
  data_ = nullptr;
  size_ = 0;
  is_valid_ = false;

  header_ = nullptr;
  phdr_table_ = nullptr;
  shdr_table_ = nullptr;

  shstrtab_ = nullptr;
  dynstr_ = nullptr;
  dynsym_ = nullptr;
  rela_plt_ = nullptr;
  rela_dyn_ = nullptr;

  rela_plt_count_ = 0;
  rela_dyn_count_ = 0;

  // 释放文件数据
  file_data_.clear();
  file_data_.shrink_to_fit();

  // 清理缓存
  segments_cache.clear();
  segments_cache.shrink_to_fit();
  sections_cache.clear();
  sections_cache.shrink_to_fit();
  symbols_cache.clear();
  symbols_cache.shrink_to_fit();
  relocations_cache.clear();
  relocations_cache.shrink_to_fit();
}

bool ELFResolver::load(const void* data, size_t size)
{
  // 参数校验
  if (data == nullptr || size < sizeof(Elf64_Ehdr)) 
  {
    LOG_ERROR("无效的 ELF 数据或大小");
    return false;
  }

  data_ = static_cast<const uint8_t*>(data);
  size_ = size;

  // 解析 ELF 头部
  header_ = reinterpret_cast<const Elf64_Ehdr*>(data_);

  // 验证
  if (!validate_elf())
  {
    LOG_ERROR("ELF 文件验证有误");
    cleanup();
    return false;
  }

  // 解析程序头表
  if (header_->e_phoff && header_->e_phnum) 
  {
    phdr_table_ = reinterpret_cast<const Elf64_Phdr*>(data_ + header_->e_phoff);
  }
  else  
  {
    LOG_ERROR("ELF 程序头表缺失");
    cleanup();
    return false;
  }
  
  // 解析节头表
  if (header_->e_shoff && header_->e_shnum)
  {
    shdr_table_ = reinterpret_cast<const Elf64_Shdr*>(data_ + header_->e_shoff);

    // 获取节名字符串表
    if (header_->e_shstrndx != SHN_UNDEF && header_->e_shstrndx < header_->e_shnum) 
    {
      const Elf64_Shdr& shstrtab_shdr = shdr_table_[header_->e_shstrndx];
      shstrtab_ = reinterpret_cast<const char*>(data_ + shstrtab_shdr.sh_offset);
    }
    else 
    {
      LOG_WARNING("ELF 节名字符串表缺失");
    }
  }
  else LOG_WARNING("ELF 节头表缺失");

  // 解析动态段信息
  if (!parse_dynamic_segment()) 
  {
    LOG_ERROR("ELF 动态段解析失败");
    cleanup();
    return false;
  }

  is_valid_ = true;
  return true;
}

bool ELFResolver::load(const std::vector<uint8_t> file_data)
{
  cleanup();

  // 保存文件数据
  file_data_ = std::move(file_data);
  return load(file_data_.data(), file_data_.size());
}

bool ELFResolver::load(const std::string& filename)
{
  cleanup();

  // 读取文件
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) 
  {
    LOG_ERROR("无法打开 ELF 文件: {}", filename);
    return false;
  }

  std::streamsize file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  // 保存文件数据
  file_data_.resize(file_size);
  if (!file.read(reinterpret_cast<char*>(file_data_.data()), file_size))
  {
    LOG_ERROR("读取 ELF 文件失败: {}", filename);
    file_data_.clear();
    return false;
  }

  return load(file_data_.data(), file_size);
} 

bool ELFResolver::is_executable() const
{
  return is_valid_ && header_ && header_->e_type == ET_EXEC;
}

bool ELFResolver::is_shared_library() const
{
  return is_valid_ && header_ && header_->e_type == ET_DYN;
}

uint64_t ELFResolver::entry_point() const 
{
    return is_valid_ && header_ ? header_->e_entry : 0;
}

uint16_t ELFResolver::segment_count() const 
{
  return is_valid_ && header_ ? header_->e_phnum : 0;
}

uint16_t ELFResolver::section_count() const 
{
  return is_valid_ && header_ ? header_->e_shnum : 0;
}

bool ELFResolver::validate_elf() const
{
  if (!header_) return false;

  // 检查 ELF 魔数
  if (header_->e_ident[EI_MAG0] != ELFMAG0 ||
      header_->e_ident[EI_MAG1] != ELFMAG1 ||
      header_->e_ident[EI_MAG2] != ELFMAG2 ||
      header_->e_ident[EI_MAG3] != ELFMAG3) 
  {
    LOG_ERROR("ELF 魔数不匹配");
    return false;
  }

  // 检查 ELF 类别(64 位)
  if (header_->e_ident[EI_CLASS] != ELFCLASS64) 
  {
    LOG_ERROR("仅支持 64 位 ELF 文件");
    return false;
  }

  // 检查 ELF 架构(ARM64)
  if (header_->e_machine != EM_AARCH64) 
  {
    LOG_ERROR("仅支持 ARM64 架构的 ELF 文件");
    return false;
  }

  // 检查数据编码(小端)
  if (header_->e_ident[EI_DATA] != ELFDATA2LSB) 
  {
    LOG_ERROR("仅支持小端编码的 ELF 文件");
    return false;
  }

  return true;
}

uint64_t ELFResolver::virtual_to_file_offset(uint64_t vaddr) const
{
  auto load_segs = loadable_segments();

  for (const auto& seg : load_segs)
  {
    uint64_t seg_vaddr = seg.virtual_address();
    uint64_t seg_memsz = seg.memory_size();
    uint64_t seg_offset = seg.offset();

    // 检查 vaddr 是否在该段的虚拟地址范围内
    if (vaddr >= seg_vaddr && vaddr < seg_vaddr + seg_memsz)
    {
      // 计算文件偏移: vaddr - 段虚拟地址 + 段文件偏移
      return seg_offset + (vaddr - seg_vaddr);
    }
  }

  // 未找到, 返回 0
  return 0; 
}

bool ELFResolver::parse_dynamic_segment()
{
  auto dynamic_segment = find_segment(PT_DYNAMIC);
  if (dynamic_segment.size() == 0) 
  {
    LOG_WARNING("未找到动态段, 静态链接 ELF");
    return true;
  }
  
  const Elf64_Dyn* dynamic = static_cast<const Elf64_Dyn*>(dynamic_segment.data());

  for (; dynamic->d_tag != DT_NULL; ++dynamic)
  {
    switch (dynamic->d_tag) 
    {
      case DT_STRTAB:
      {
        uint64_t strtab_offset = virtual_to_file_offset(dynamic->d_un.d_ptr);
        if (strtab_offset < size_)
          dynstr_ = reinterpret_cast<const char*>(data_ + strtab_offset);
        break;
      }
      case DT_SYMTAB:
      {
        uint64_t symtab_offset = virtual_to_file_offset(dynamic->d_un.d_ptr);
        if (symtab_offset < size_)
          dynsym_ = reinterpret_cast<const Elf64_Sym*>(data_ + symtab_offset);
        break;
      }
      case DT_SYMENT:
        sym_entry_size_ = dynamic->d_un.d_val;
        break;
      case DT_JMPREL:
        rela_plt_ = reinterpret_cast<const Elf64_Rela*>(data_ + dynamic->d_un.d_val);
        break;
      case DT_PLTRELSZ:
        rela_plt_count_ = dynamic->d_un.d_val / sizeof(Elf64_Rela);
        break;
      case DT_RELA:
        rela_dyn_ = reinterpret_cast<const Elf64_Rela*>(data_ + dynamic->d_un.d_val);
        break;
      case DT_RELASZ:
        rela_dyn_count_ = dynamic->d_un.d_val / sizeof(Elf64_Rela);
        break;
    }
  }

  // 检查是否缺失关键数据
  if (dynstr_ == nullptr || dynsym_ == nullptr || sym_entry_size_ == 0)
  {
    LOG_ERROR("动态段缺失关键信息");
    return false;
  }

  return true;
}

std::vector<Segment> ELFResolver::segments() const
{
  if (!is_valid_ || !phdr_table_) return {};

  if (segments_cache.empty())
  {
    for (uint16_t i = 0; i < segment_count(); ++i)
    {
      const Elf64_Phdr* phdr = &phdr_table_[i];
      const void* segment_data = data_ + phdr->p_offset;
      size_t segment_size = std::min(phdr->p_filesz, size_ - phdr->p_offset);

      segments_cache.emplace_back(phdr, segment_data, segment_size);
    }
  }
  
  return segments_cache;
}

Segment ELFResolver::segment(uint16_t index) const
{
  if (!is_valid_ || !phdr_table_ || index >= segment_count()) 
    return Segment(nullptr, nullptr, 0);

  const Elf64_Phdr* phdr = &phdr_table_[index];
  const void* segment_data = data_ + phdr->p_offset;
  size_t segment_size = std::min(phdr->p_filesz, size_ - phdr->p_offset);

  return Segment(phdr, segment_data, segment_size);
}

Segment ELFResolver::find_segment(uint32_t type) const
{
  auto segs = segments();

  for (const auto& seg : segs)
  {
    if (seg.type() == type) return seg;
  }

  return Segment(nullptr, nullptr, 0);
}

std::vector<Segment> ELFResolver::loadable_segments() const
{
  std::vector<Segment> result;
  auto segs = segments();

  for (const auto& seg : segs)
  {
    if (seg.is_loadable()) result.push_back(seg);
  }

  return result;
}

std::vector<Section> ELFResolver::sections() const
{
  if (!is_valid_ || !shdr_table_ || !shstrtab_) return {};

  if (sections_cache.empty())
  {
    for (uint16_t i = 0; i < section_count(); ++i)
    {
      const Elf64_Shdr* shdr = &shdr_table_[i];
      const char* name = shstrtab_ + shdr->sh_name;
      const void* section_data = data_ + shdr->sh_offset;

      sections_cache.emplace_back(shdr, name, section_data);
    }
  }

  return sections_cache;
}

Section ELFResolver::section(uint16_t index) const 
{
  if (!is_valid_ || !shdr_table_ || index >= section_count()) return Section(nullptr, "", nullptr);

  const Elf64_Shdr* shdr = &shdr_table_[index];
  const char* name = shstrtab_ + shdr->sh_name;
  const void* section_data = data_ + shdr->sh_offset;

  return Section(shdr, name, section_data);
}

Section ELFResolver::find_section(const std::string& name, uint32_t type) const
{
  auto secs = sections();
  for (const auto& sec : secs)
  {
    if ((type == 0 || sec.type() == type) && sec.name() == name) return sec;
  }

  return Section(nullptr, "", nullptr);
}

std::vector<Symbol> ELFResolver::symbols() const
{
  if (!is_valid_ || !dynsym_ || !dynstr_) return {};

  if (symbols_cache.empty())
  {
    const Elf64_Sym* sym = dynsym_;
    // 能超出 ELF 数据的末尾, 且至少保留一个符号的空间
    const uint8_t* sym_end = data_ + size_ - sym_entry_size_;
    while (reinterpret_cast<const uint8_t*>(sym) <= sym_end) 
    {
      const char* name = "";
      if (sym->st_name != 0 && dynstr_)
      {
        const char* sym_name = dynstr_ + sym->st_name;
        if (sym_name >= dynstr_ && sym_name < reinterpret_cast<const char*>(data_ + size_))
        {
          name = sym_name;
        }
      }
      symbols_cache.emplace_back(sym, name, sym->st_value);

      // 下一个符号
      sym = reinterpret_cast<const Elf64_Sym*>(reinterpret_cast<const uint8_t*>(sym) + sym_entry_size_);
      // 遇到 st_name 为 0 且 st_value 为 0 的空符号, 停止遍历
      if (sym->st_name == 0 && sym->st_value == 0)
        break;
    }
  }

  return symbols_cache;
}

Symbol ELFResolver::find_symbol(const std::string& name) const
{
  auto syms = symbols();
  for (const auto& sym : syms)
  {
    if (sym.name() == name) return sym;
  }

  return Symbol(nullptr, "", 0);
}

std::vector<Relocation> ELFResolver::relocations() const
{
  if (relocations_cache.empty())
  {
    // 收集动态重定位
    if (rela_dyn_)
    {
      for (size_t i = 0; i < rela_dyn_count_; ++i)
      {
        const Elf64_Rela* rela = &rela_dyn_[i];

        // 低 32 位: 重定位类型
        uint32_t type = ELF64_R_TYPE(rela->r_info);
        // 高 32 位: 符号表索引
        uint32_t sym_index = ELF64_R_SYM(rela->r_info);

        std::string sym_name;
        if (sym_index && dynsym_ && dynstr_)
        {
          const Elf64_Sym* sym = &dynsym_[sym_index];
          if (sym->st_name)
            sym_name = dynstr_ + sym->st_name;
        }

        relocations_cache.emplace_back(rela, type, sym_index, sym_name, rela->r_addend);
      }
    }

    // 收集 PLT 重定位
    if (rela_plt_)
    {
      for (size_t i = 0; i < rela_plt_count_; ++i)
      {
        const Elf64_Rela* rela = &rela_plt_[i];

        // 低 32 位: 重定位类型
        uint32_t type = ELF64_R_TYPE(rela->r_info);
        // 高 32 位: 符号表索引
        uint32_t sym_index = ELF64_R_SYM(rela->r_info);

        std::string sym_name;
        if (sym_index && dynsym_ && dynstr_)
        {
          const Elf64_Sym* sym = &dynsym_[sym_index];
          if (sym->st_name)
            sym_name = dynstr_ + sym->st_name;
        }

        relocations_cache.emplace_back(rela, type, sym_index, sym_name, rela->r_addend);
      }
    }
  }

  return relocations_cache;
}