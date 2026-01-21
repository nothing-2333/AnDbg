#include <algorithm>
#include <cstdint>
#include <optional>
#include <unistd.h>

#include "elf_loader.hpp"
#include "elf_resolver.hpp"
#include "log.hpp"
#include "utils.hpp"


ELFLoader::ELFLoader()
{
  memory_control_ = &MemoryControl::get_instance();
}

LoadInfo ELFLoader::load_elf(const std::string& filename, pid_t target_pid, uint64_t preferred_base, SymbolTable external_symbols)
{
  ELFResolver resolver;
  resolver.load(filename);

  return load_elf(target_pid, resolver, preferred_base, external_symbols);  
}

LoadInfo ELFLoader::load_elf(std::vector<uint8_t> file_data, pid_t target_pid, uint64_t preferred_base, SymbolTable external_symbols)
{
  ELFResolver resolver;
  resolver.load(file_data);

  return load_elf(target_pid, resolver, preferred_base, external_symbols);  
}

LoadInfo ELFLoader::load_elf(pid_t target_pid, const ELFResolver& resolver, uint64_t preferred_base, SymbolTable external_symbols)
{
  LoadInfo info = {};

  if (target_pid == -1)
  {
    // 默认 target_pid == -1, 使用自身 pid
    target_pid = getpid();
  }

  if (resolver.is_valid())
  {
    LOG_ERROR("无效的 ELF 解析器");
    return {};
  }
  info.entry_point = resolver.entry_point();

  // 确定加载基址
  std::optional<uint64_t> load_base_opt = determine_load_base(target_pid, resolver, preferred_base);
  if (!load_base_opt.has_value())
  {
    LOG_ERROR("无法确定加载基地址");
    return {};
  }
  uint64_t load_base= load_base_opt.value();

  // 加载段到内存
  if (!load_segments(target_pid, resolver, load_base, info))
  {
    LOG_ERROR("加载段失败");
    unload_elf(target_pid, info);
    return {};
  }
  info.load_base = reinterpret_cast<uint64_t>(load_base);

  // 处理重定位
  if (!apply_relocations(target_pid, resolver, load_base, info, external_symbols))
  {
    LOG_ERROR("应用重定位失败");
    unload_elf(target_pid, info);
    return {};
  }
  
  LOG_DEBUG("ELF 加载成功: 基地址=0x{:x}, 入口点=0x{:x}", load_base, info.entry_point);
  return info;
}

void ELFLoader::unload_elf(pid_t target_pid, LoadInfo& info)
{
  for (const auto& segment : info.loaded_segments)
  {
    memory_control_->free_memory(target_pid, segment.start_address, segment.size);
  }

  info.loaded_segments.clear();
  info.symbol_table.clear();
  info.load_base = 0;
  info.entry_point = 0;
}

std::optional<uint64_t> ELFLoader::determine_load_base(pid_t target_pid, const ELFResolver& resolver, uint64_t preferred_base)
{
  size_t total_size = calculate_load_segments_total_size(resolver);
  std::optional<uint64_t> address_opt = find_available_address(target_pid, preferred_base, total_size);
  return address_opt;
}

size_t ELFLoader::calculate_load_segments_total_size(const ELFResolver& resolver)
{
  // 找到最低和最高的虚拟地址
  uint64_t min_vaddr = UINT64_MAX;
  uint64_t max_vaddr_end = 0;

  std::vector<Segment> segments = resolver.loadable_segments();
  if (segments.empty()) 
  {
    LOG_DEBUG("没有可加载的段");
    return 0;
  }

  for (const auto segment : segments)
  {
    uint64_t seg_vaddr = segment.virtual_address();
    uint64_t seg_vaddr_end = seg_vaddr + segment.memory_size();

    min_vaddr = std::min(min_vaddr, seg_vaddr);
    max_vaddr_end = std::max(max_vaddr_end, seg_vaddr_end);
  }

  if (min_vaddr == UINT64_MAX || max_vaddr_end == 0)
    return 0;

  size_t total_size = Utils::align_page_up(max_vaddr_end) - Utils::align_page_down(min_vaddr);
  LOG_DEBUG("计算加载段总大小: 最小地址=0x{:x}, 最大结束地址=0x{:x}, 总大小=0x{:x}", min_vaddr, max_vaddr_end, total_size);

  return total_size;
}

std::optional<uint64_t> ELFLoader::find_available_address(pid_t target_pid, uint64_t preferred_base, size_t total_size)
{
  // 检查大小
  if (total_size <= 0)
  {
    LOG_ERROR("加载段总大小为 0");
    return std::nullopt;
  }

  // 优先使用 preferred_base
  if (memory_control_->can_capacity(target_pid, preferred_base, total_size))
  {
    return preferred_base;
  }

  // 自动查找空闲内存地址
  uint64_t vacant_addr = memory_control_->find_vacant_memory(target_pid, total_size);
  if (vacant_addr != 0)
  {
    LOG_DEBUG("自动分配内存地址: 0x{:x} (大小: 0x{:x})", vacant_addr, total_size);
    return vacant_addr;
  }

  LOG_ERROR("无可用内存地址");
  return std::nullopt;
}

inline int segment_perms_to_prot(const Segment& segment)
{
    int prot = 0;
    if (segment.is_readable())  prot |= PROT_READ;
    if (segment.is_writable())  prot |= PROT_WRITE;
    if (segment.is_executable()) prot |= PROT_EXEC;

    // 默认至少可读
    if (prot == 0) prot = PROT_READ;

    return prot;
}

std::optional<uint64_t> ELFLoader::load_segments(pid_t target_pid, const ELFResolver& resolver, uint64_t load_base, LoadInfo& info)
{
  // 遍历所有可加载段
  std::vector<Segment> segments = resolver.loadable_segments();
  if (segments.size() == 0) 
  {
    LOG_DEBUG("没有可加载的段");
    return std::nullopt;
  }

  for (const auto& segment: segments)
  {
    uint64_t seg_vaddr = segment.virtual_address();
    uint64_t seg_memsz = segment.memory_size();
    uint64_t seg_filesz = segment.file_size();

    if (seg_memsz == 0) 
    {
      LOG_DEBUG("跳过空段: 虚拟地址=0x{:x}", seg_vaddr);
      continue;
    }

    // 计算实际加载地址
    uint64_t target_addr = load_base + seg_vaddr;

    // 对齐到页面边界
    uint64_t aligned_addr = Utils::align_page_down(target_addr);
    uint64_t alignment_offset = target_addr - aligned_addr;
    size_t aligned_size = Utils::align_page_up(seg_memsz + alignment_offset);
    LOG_DEBUG("加载段: 虚拟地址=0x{:x}, 内存大小={}, 文件大小={}, 对齐地址=0x{:x}", seg_vaddr, seg_memsz, seg_filesz, target_addr);

    // 分配内存
    int prot = segment_perms_to_prot(segment);
    uint64_t allocated_addr = memory_control_->allocate_memory(
      target_pid, 
      aligned_size, 
      aligned_addr,
      prot
    );

    if (allocated_addr == 0) 
    {
      LOG_ERROR("分配内存失败: 大小={}, 保护=0x{:x}", aligned_size, prot);
      return std::nullopt;
    }

    // 检查分配的地址是否正确
    if (allocated_addr != aligned_addr) {
      LOG_WARNING("地址不匹配: 期望=0x{:x}, 实际=0x{:x}", aligned_addr, allocated_addr);
      // 继续使用实际分配的地址
    }

    // 记录已分配的段
    MemoryRegion region;
    region.start_address = allocated_addr + alignment_offset;
    region.end_address = region.start_address + seg_memsz;
    region.size = seg_memsz;
    region.permissions = "";
    if (segment.is_readable()) region.permissions += "r";
    if (segment.is_writable()) region.permissions += "w";
    if (segment.is_executable()) region.permissions += "x";
    region.pathname = "[loaded_elf]";
    info.loaded_segments.push_back(region);
    
    // 复制段数据
    if (seg_filesz > 0)
    {
      const void* segment_data = segment.data();
      if (segment_data) 
      {
        uint64_t write_addr = allocated_addr + alignment_offset;
        
        LOG_DEBUG("写入段数据: 地址=0x{:x}, 大小={}", write_addr, seg_filesz);
        
        if (!memory_control_->write_memory(target_pid, write_addr, segment_data, seg_filesz)) 
        {
          LOG_ERROR("写入段数据失败: 地址=0x{:x}, 大小={}", write_addr, seg_filesz);
          return std::nullopt;
        }
      }
    }

    // 清零剩余部分(.bss)
    if (seg_filesz < seg_memsz) 
    {
      uint64_t bss_start = allocated_addr + alignment_offset + seg_filesz;
      size_t bss_size = seg_memsz - seg_filesz;
      
      if (bss_size > 0) 
      {
        LOG_DEBUG("清零 BSS: 地址=0x{:x}, 大小={}", bss_start, bss_size);
        
        std::vector<uint8_t> zero_buffer(bss_size, 0);
        if (!memory_control_->write_memory(target_pid, bss_start, zero_buffer.data(), bss_size)) 
        {
          LOG_ERROR("清零 BSS 失败");
          return std::nullopt;
        }
      }
    }
  }

  LOG_DEBUG("段加载完成, 共加载 {} 个段", info.loaded_segments.size());
  return true;
}

bool ELFLoader::apply_relocations(pid_t target_pid, const ELFResolver& resolver, uint64_t load_base, LoadInfo& info, const SymbolTable& external_symbols)
{
  auto relocations = resolver.relocations();
  if (relocations.empty()) 
  {
    LOG_DEBUG("没有重定位需要处理");
    return true;
  } 

  LOG_DEBUG("开始处理 {} 个重定位", relocations.size());

  for (const auto& relocation : relocations) {
    if (!apply_relocation(target_pid, relocation, load_base, info, external_symbols, resolver)) 
    {
      LOG_ERROR("处理重定位失败: 偏移=0x{:x}, 类型={}, 符号={}", relocation.offset(), relocation.type(), relocation.symbol_name());
      return false;
    }
  }
  
  LOG_DEBUG("重定位处理完成");
  return true;
}

bool ELFLoader::apply_relocation(pid_t target_pid, const Relocation& relocation, uint64_t load_base, 
  LoadInfo& info, const SymbolTable& external_symbols, const ELFResolver& resolver)
{
  uint64_t reloc_offset = relocation.offset();
  uint32_t reloc_type = relocation.type();
  int64_t addend = relocation.addend();

  // 计算重定位地址
  uint64_t reloc_addr = load_base + reloc_offset;
  
  // 解析符号地址
  uint64_t symbol_addr = resolve_symbol(resolver, relocation.symbol_name(), load_base, info, external_symbols);

  // 根据重定位类型处理
  switch (reloc_type) 
  {
    case R_AARCH64_ABS64: 
    {
      uint64_t value = symbol_addr + addend;
      LOG_DEBUG("ABS64 重定位: 地址=0x{:x}, 值=0x{:x}", reloc_addr, value);
      return memory_control_->write_memory(target_pid, reloc_addr, &value, sizeof(value));
    }
    case R_AARCH64_GLOB_DAT:
    case R_AARCH64_JUMP_SLOT: 
    {  
      LOG_DEBUG("GLOB_DAT/JUMP_SLOT 重定位: 地址=0x{:x}, 符号地址=0x{:x}", reloc_addr, symbol_addr);
      return memory_control_->write_memory(target_pid, reloc_addr, &symbol_addr, sizeof(symbol_addr));
    }
    case R_AARCH64_RELATIVE: 
    {
      uint64_t value = load_base + addend;
      LOG_DEBUG("RELATIVE 重定位: 地址=0x{:x}, 值=0x{:x}", reloc_addr, value);
      return memory_control_->write_memory(target_pid, reloc_addr, &value, sizeof(value));
    }
    default: 
    {
      LOG_WARNING("不支持的重定位类型: {}", reloc_type);
      return true;  // 跳过不支持的重定位
    }
  }
}

uint64_t ELFLoader::resolve_symbol(const ELFResolver& resolver, const std::string& name, uint64_t load_base,
  LoadInfo& info, const SymbolTable& external_symbols)
{
  // 检查外部符号
  auto ext_it = external_symbols.find(name);
  if (ext_it != external_symbols.end()) 
    return ext_it->second;

  // 检查已加载的符号表
  auto info_it = info.symbol_table.find(name);
  if (info_it != info.symbol_table.end()) 
    return info_it->second;

  // 在解析器中查找符号
  auto sym = resolver.find_symbol(name);
  if (sym.value() != 0) 
  {
    uint64_t result_address = load_base + sym.value();
    // 更新符号表
    info.symbol_table[name] = result_address;
    return result_address;
  }
  
  LOG_WARNING("未找到符号: {}", name);
  return 0;
}

