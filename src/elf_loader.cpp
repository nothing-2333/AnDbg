#include "elf_loader.hpp"
#include "elf_resolver.hpp"
#include "log.hpp"
#include <cstdint>
#include <optional>


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

  }

  if (resolver.is_valid())
  {
    LOG_ERROR("无效的 ELF 解析器");
    return {};
  }
  info.entry_point = resolver.entry_point();

  // 获取加载段
  auto segments = resolver.loadable_segments();
  if (segments.empty())
  {
    LOG_ERROR("没有可加载的段");
    return {};
  }

  // 确定加载基址
  std::optional<uint64_t> load_base_opt = determine_load_base(target_pid, segments, preferred_base);
  if (!load_base_opt.has_value())
  {
    LOG_ERROR("无法确定加载基地址");
    return {};
  }
  uint64_t load_base= load_base_opt.value();
  info.load_base = reinterpret_cast<uint64_t>(load_base);

  // 加载段到内存
  if (!load_segments(target_pid, segments, load_base, info))
  {
    LOG_ERROR("加载段失败");
    unload_elf(target_pid, info);
    return {};
  }

  // 处理重定位
  if (!apply_relocations(target_pid, resolver, load_base, info, external_symbols))
  {
    LOG_ERROR("应用重定位失败");
    unload_elf(target_pid, info);
    return {};
  }

  // 设置内存保护
  if (!set_memory_protections(target_pid, segments, load_base))
  {
    LOG_WARNING("设置内存保护失败, 但已加载成功");
  }

  // 构建符号表
  build_symbol_table(load_base, resolver, info);

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

std::optional<uint64_t> ELFLoader::determine_load_base(pid_t target_pid, const std::vector<Segment>& segments, uint64_t preferred_base)
{
  size_t total_size = calculate_load_segments_total_size(segments);
  std::optional<uint64_t> address_opt = find_available_address(target_pid, total_size, preferred_base);
  return address_opt;
}

std::optional<uint64_t> find_available_address(pid_t target_pid, size_t total_size, uint64_t preferred_base)
{
  uint64_t start_address = 0x400000;

  if (preferred_base != 0)
    start_address = preferred_base;

  
}