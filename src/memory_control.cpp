#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <linux/uio.h>
#include <sstream>
#include <string>
#include <sys/uio.h>
#include <vector>
#include <sys/syscall.h>
#include <unistd.h>

#include "memory_control.hpp"
#include "log.hpp"
#include "fmt/format.h"
#include "register_control.hpp"
#include "utils.hpp"


const uint64_t MEM64_START = 0x10000;
const uint64_t MEM64_END = 0x7FFFFFFFFFFF;

bool MemoryControl::read_memory_ptrace(pid_t pid, uint64_t address, void* buffer, size_t size)
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

std::vector<MemoryRegion> MemoryControl::get_memory_regions(pid_t pid)
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

  // 按起始地址排序
  std::sort(regions.begin(), regions.end(),
    [](const MemoryRegion& a, const MemoryRegion& b) {
      return a.start_address < b.start_address;
  });

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

  try 
  {
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

  // 解析路径名
  std::getline(iss >> std::ws, region.pathname);

  // 处理空路径名的情况
  if (region.pathname.empty()) region.pathname = "[anonymous]";

  // 额外的完整性检查
  if (iss.fail() && !iss.eof()) LOG_WARNING("解析 maps 行时遇到流错误: {}", line);

  return true;
}

bool MemoryControl::check_address_permission(pid_t pid, uint64_t address, size_t size, bool need_write)
{
  if (size <= 0) {
    LOG_ERROR("size 必须大于 0");
    return false;
  }

  //  避免 address + size 溢出 64 位地址极端情况
  if (address > UINT64_MAX - size)
  {
    LOG_ERROR("address + size 溢出");
    return false;
  }
  const uint64_t end_address = address + size;


  std::vector<MemoryRegion> regions = get_memory_regions(pid);
  if (regions.empty())
  {
    LOG_ERROR("没有分配内存, PID: {}", pid);
    return false;
  }

  // 遍历内存区域, 检查区间是否完全覆盖
  uint64_t current_address = address;
  auto region_item = regions.begin();

  while (current_address < end_address) 
  {
    // 找到包含 current_address 的内存区域, 利用 regions 有序性，无需从头遍历
    while (region_item != regions.end() && !region_item->contains(current_address)) 
    {
      ++region_item;
    }
    // 无匹配区域
    if (region_item == regions.end())
    {
      LOG_ERROR("地址 0x{:x} 未映射内存", current_address);
      return false;
    }

    const auto& current_region = *region_item;

    // 权限校验
    if (!current_region.is_readable())
    {
      LOG_ERROR("内存 0x{:x}-0x{:x} 不可读", current_region.start_address, current_region.end_address);
      return false;
    }
    if (need_write && !current_region.is_writable())
    {
      LOG_ERROR("内存 0x{:x}-0x{:x} 不可写", current_region.start_address, current_region.end_address);
      return false;
    }

    // 继续检查下一个区域
    current_address = current_region.end_address;
    ++region_item;
  }

  // 所有区间都通过校验
  return true;
}

bool MemoryControl::read_memory(pid_t pid, uint64_t address, void* buffer, size_t size)
{
  if (buffer == nullptr || size == 0)
  {
    LOG_ERROR("错误的参数");
    return false;
  }

  // 检查地址权限
  if (!check_address_permission(pid, address, size, false))
  {
    LOG_ERROR("没有读取权限, address: 0x{:x}", address);
    return false;
  }

  // 使用 process_vm_readv 进行高效读取
  struct iovec loval_iov = {buffer, size};
  struct iovec remote_iov = {reinterpret_cast<void*>(address), size};
  ssize_t ret = process_vm_readv(pid, &loval_iov, 1, &remote_iov, 1, 0);
  if (ret == static_cast<ssize_t>(size)) return true;

  // 如果 process_vm_readv 失败, 回退到 ptrace
  LOG_WARNING("process_vm_readv 失败, 使用 ptrace");
  return read_memory_ptrace(pid, address, buffer, size);
}

bool MemoryControl::write_memory(pid_t pid, uint64_t address, const void* buffer, size_t size)
{
  if (buffer == nullptr || size == 0)
  {
    LOG_ERROR("错误的参数");
    return false;
  }

  // 检查地址权限
  if (!check_address_permission(pid, address, size, true))
  {
    LOG_ERROR("没有写入权限, address: 0x{:x}", address);
    return false;
  }

  // 使用 process_vm_writev 进行高效写入
  struct iovec loval_iov = {const_cast<void*>(buffer), size};
  struct iovec remote_iov = {reinterpret_cast<void*>(address), size};
  ssize_t ret = process_vm_writev(pid, &loval_iov, 1, &remote_iov, 1, 0);
  if (ret == static_cast<ssize_t>(size)) return true;

  // 如果 process_vm_writev 失败, 回退到 ptrace
  LOG_WARNING("process_vm_writev 失败, 使用 ptrace");
  return write_memory_ptrace(pid, address, buffer, size);
}

std::vector<uint64_t> MemoryControl::search_memory(pid_t pid, const std::vector<uint8_t>& pattern)
{
  // 储存满足条件的地址
  std::vector<uint64_t> results;

  // 基础参数校验
  if (pattern.empty())
  {
    LOG_ERROR("搜索 pattern 为空");
    return results;
  }
  const size_t pattern_size = pattern.size();
  if (pattern_size == 0) return results;

  auto regions = get_memory_regions(pid);
  for (const auto& region : regions)
  {
    // 跳过不可读区域
    if (!region.is_readable()) continue;

    // 区域大小小于 pattern, 直接跳过
    if (region.size < pattern_size) continue;

    // 分块读取区域
    const size_t chunk_size = 4 * 1024 * 1024;
    // 缓冲区预分配
    std::vector<uint8_t> buffer(chunk_size); 

    uint64_t current_address = region.start_address;
    while (current_address < region.end_address) 
    {
      // 计算当前块的实际读取大小
      const size_t read_size = std::min(chunk_size, region.end_address - current_address);
      buffer.resize(read_size);

      if (!read_memory(pid, current_address, buffer.data(), read_size))
      {
        LOG_WARNING("search_memory: 读取区域 0x{:x}-0x{:x} 失败, 跳过", current_address, current_address + read_size);
        current_address += read_size;
        continue;
      }

      // 在当前块中搜索 pattern
      for (size_t i = 0; i <= read_size - pattern_size; ++i)
      {
        if (memcmp(&buffer[i], pattern.data(), pattern_size) == 0)
          results.push_back(current_address + i);
      }

      current_address += read_size;
    }
  }

  return results;
}

bool MemoryControl::dump_memory(pid_t pid, uint64_t start_address, uint64_t end_address, const std::string& filename)
{
  // 基础参数校验
  if (filename.empty()) 
  {
    LOG_ERROR("输出文件名为空");
    return false;
  }
  if (start_address >= end_address) 
  {
    LOG_ERROR("起始地址 0x{:x} >= 结束地址 0x{:x}, 无效区间", start_address, end_address);
    return false;
  }

  uint64_t size = end_address - start_address;

  // 权限校验
  if (!check_address_permission(pid, start_address, size, false))
  {
    LOG_ERROR("区间 0x{:x}-0x{:x} 存在不可读区域或地址无效", start_address, end_address);
    return false;
  }

  // 分块读取, 写入
  const size_t chunk_size = 4 * 1024 * 1024;
  std::vector<uint8_t> buffer(chunk_size);
  // trunc: 覆盖已有文件
  std::ofstream file(filename, std::ios::binary | std::ios::trunc);
  if (!file.is_open())
  {
    LOG_ERROR("创建文件 {} 失败: {}", filename, strerror(errno));
    return false;
  }
  
  uint64_t current_address = start_address;
  uint64_t remaining = size;

  while (remaining > 0) 
  {
    // 计算当前块实际读取大小
    size_t read_size = std::min(chunk_size, remaining);
    buffer.resize(read_size);

    // 读取当前块内存
    if (!read_memory(pid, current_address, buffer.data(), read_size)) {
      LOG_ERROR("读取地址 0x{:x} 失败, 已转存 {} bytes", current_address, size - remaining);
      file.close();
      return false;
    }

    // 写入文件
    file.write(reinterpret_cast<const char*>(buffer.data()), read_size);
    if (!file.good())
    {
      LOG_ERROR("写入文件 {} 失败: {}", filename, strerror(errno));
      file.close();
      return false;
    }

    current_address += read_size;
    remaining -= read_size;

    LOG_DEBUG("转存进度: {}/{} bytes ({}%)", size - remaining, size, (size - remaining) * 100 / size);
  }

  // 最终确认文件写入状态, 刷新缓冲区
  file.flush();
  if (file.good()) 
  {
    LOG_DEBUG("内存转存成功, 文件 {}, 大小 {} bytes(0x{:x})", filename, size, size);
    return true;
  } 
  else 
  {
    LOG_ERROR("文件刷新失败");
    return false;
  }
}

uint64_t MemoryControl::allocate_memory(pid_t pid, size_t size, uint64_t address, int prot)
{
  // 基础参数校验
  if (pid <= 0) 
  {
    LOG_ERROR("无效的 PID: {}", pid);
    return 0;
  }
  if (size == 0) 
  {
    LOG_ERROR("分配内存大小不能为 0");
    return 0;
  }
  if (address < 0)
  {
    LOG_ERROR("无效的地址: {}", address);
    return 0;
  }
  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC | PROT_NONE)) != 0)
  {
    LOG_ERROR("无效内存保护属性 prot: 0x{:x}", prot);
    return 0;
  }

  // 内存大小页对齐处理
  size = Utils::align_page_up(size);

  // 保存原始寄存器
  auto gpr_opt = RegisterControl::get_instance().get_all_gpr(pid);
  if (!gpr_opt) 
  {
    LOG_ERROR("获取进程 {} 寄存器失败", pid);
    return 0;
  }
  const auto& gpr = gpr_opt.value();

  // 准备 mmap 系统调用参数
  struct user_pt_regs regs = {0};
  memcpy(&regs, &gpr, sizeof(user_pt_regs));  
  regs.regs[8] = __NR_mmap;                   // 系统调用号
  regs.regs[0] = address;                     // 默认是 0, 内核自动分配地址
  regs.regs[1] = static_cast<uint64_t>(size); // 分配大小
  regs.regs[2] = static_cast<uint64_t>(prot); // 内存保护属性
  regs.regs[3] = MAP_PRIVATE | MAP_ANONYMOUS; // 匿名私有映射
  regs.regs[4] = static_cast<uint64_t>(-1);   // 匿名映射无需文件描述符
  regs.regs[5] = 0;                           // 页偏移量

  // 设置寄存器
  if (!RegisterControl::get_instance().set_all_gpr(pid, regs))
  {
    LOG_ERROR("设置进程 {} 寄存器失败", pid);
    // 恢复原始寄存器
    RegisterControl::get_instance().set_all_gpr(pid, gpr); 
    return 0;
  }

  // 执行系统调用
  if (!Utils::syscall_wrapper(pid))
  {
    LOG_ERROR("进程 {} 执行 mmap2 系统调用失败", pid);
    RegisterControl::get_instance().set_all_gpr(pid, gpr); 
    return 0;
  }

  // 获取系统调用返回值
  auto result_gpr_opt = RegisterControl::get_instance().get_all_gpr(pid);
  if (!result_gpr_opt)
  {
    LOG_ERROR("获取进程 {} 系统调用返回寄存器失败", pid);
    RegisterControl::get_instance().set_all_gpr(pid, gpr); 
    return 0; 
  }
  const auto& result_gpr = result_gpr_opt.value();
  uint64_t mmap_address = result_gpr.regs[0];

  // 校验返回值
  int64_t signed_address = static_cast<int64_t>(mmap_address);
  if (signed_address < 0 && signed_address >= -4095)
  {
    int error = static_cast<int>(-signed_address);
    char err_buf[256] = {0};
    strerror_r(error, err_buf, sizeof(err_buf));
    LOG_ERROR("进程 {}: mmap2 系统调用失败, 大小: {} 字节, prot: 0x{:x}, 错误: {} ({})", 
      pid, size, prot, error, err_buf);
    RegisterControl::get_instance().set_all_gpr(pid, gpr);
    return 0;
  }

  // 恢复原始寄存器
  if (!RegisterControl::get_instance().set_all_gpr(pid, gpr))
  {
    LOG_ERROR("恢复进程 {} 寄存器失败", pid);
    return 0; 
  }

  LOG_DEBUG("在进程 {} 中分配内存成功, 地址: 0x{:x}, 大小: {} 字节, prot: 0x{:x}", pid, mmap_address, size, prot);
  return mmap_address;
}

bool MemoryControl::free_memory(pid_t pid, uint64_t address, size_t size)
{
  // 参数校验
  if (pid <= 0) 
  {
    LOG_ERROR("无效的 PID: {}", pid);
    return false;
  }
  if (size == 0)
  {
    LOG_ERROR("释放内存大小不能为 0");
    return false;
  }
  if (address == 0 || reinterpret_cast<void*>(address) == MAP_FAILED)
  {
      LOG_ERROR("无效的内存地址: 0x{:x}", address);
      return false;
  }

  // 对齐检查, munmap 要求地址和大小都页对齐
  size = Utils::align_page_up(size);
  address = Utils::align_page_up(address);

  // 保存原始寄存器
  auto gpr_opt = RegisterControl::get_instance().get_all_gpr(pid);
  if (!gpr_opt) 
  {
      LOG_ERROR("获取进程 {} 寄存器失败", pid);
      return false;
  }
  const auto& gpr = gpr_opt.value();

  // 准备 munmap 系统调用参数
  struct user_pt_regs regs = {0};
  memcpy(&regs, &gpr, sizeof(user_pt_regs));

  regs.regs[8] = __NR_munmap;                       // 系统调用号
  regs.regs[0] = static_cast<uint64_t>(address);    // 要释放的地址
  regs.regs[1] = static_cast<uint64_t>(size);       // 要释放的大小  

  // 设置寄存器
  if (!RegisterControl::get_instance().set_all_gpr(pid, regs))
  {
    LOG_ERROR("设置进程 {} 寄存器失败", pid);
    RegisterControl::get_instance().set_all_gpr(pid, gpr); 
    return false;
  }

  // 执行系统调用
  if (!Utils::syscall_wrapper(pid))
  {
    LOG_ERROR("进程 {} 执行 munmap 系统调用失败", pid);
    RegisterControl::get_instance().set_all_gpr(pid, gpr);
    return false;
  }

  // 获取系统调用返回值
  auto result_gpr_opt = RegisterControl::get_instance().get_all_gpr(pid);
  if (!result_gpr_opt)
  {
    LOG_ERROR("获取进程 {} 系统调用返回寄存器失败", pid);
    RegisterControl::get_instance().set_all_gpr(pid, gpr); 
    return false;
  }
  const auto& result_gpr = result_gpr_opt.value();
  uint64_t munmap_result = result_gpr.regs[0];

  // 校验返回值
  int64_t signed_result = static_cast<int64_t>(munmap_result);
  if (signed_result != 0) 
  {
    int error = static_cast<int>(-signed_result);
    char err_buf[256] = {0};
    strerror_r(error, err_buf, sizeof(err_buf));
    LOG_ERROR("进程 {}: munmap 系统调用失败, 地址: 0x{:x}, 大小: {} 字节, 错误: {} ({})", 
      pid, address, size, error, err_buf);
    RegisterControl::get_instance().set_all_gpr(pid, gpr);
    return false;
  }

  // 恢复原始寄存器
  if (!RegisterControl::get_instance().set_all_gpr(pid, gpr))
  {
    LOG_ERROR("恢复进程 {} 寄存器失败", pid);
    return false; 
  }

  LOG_DEBUG("在进程 {} 中释放内存成功, 地址: 0x{:x}, 大小: {} 字节", pid, address, size);
  return true;
}

uint64_t MemoryControl::find_vacant_memory(pid_t pid, size_t total_size)
{
  if (pid <= 0)
  {
    LOG_ERROR("无效的 PID: {}", pid);
    return 0;
  }
  if (total_size == 0)
  {
    LOG_ERROR("内存大小不能为 0");
    return 0;
  }

  // 页对齐
  total_size = Utils::align_page_up(total_size);

  // 获取所有内存区域
  std::vector<MemoryRegion> regions = get_memory_regions(pid);
  if (regions.empty()) 
  {
    LOG_ERROR("无法获取进程 {} 的内存映射信息", pid);
    return 0;
  }

  uint64_t prev_end = MEM64_END;

  // 遍历所有已映射区域, 查找区域之间的空闲间隙
  for (const auto& region : regions)
  {
    uint64_t gap_start = prev_end;
    uint64_t gap_end   = region.start_address;
    uint64_t gap_size  = gap_end - gap_start;

    // 找到足够大的空闲间隙
    if (gap_size >= total_size)
    {
      LOG_DEBUG("找到空闲内存区间: 0x{:x}-0x{:x}, 大小:0x{:x}, 满足需求:0x{:x}", gap_start, gap_end, gap_size, total_size);
      return gap_start;
    }

    prev_end = region.end_address;
  }

  // 检查最后一个区域到内存上限的间隙
  if (MEM64_END - prev_end >= total_size)
  {
    LOG_DEBUG("找到尾部空闲内存区间:0x{:x}-0x{:x}, 满足需求", prev_end, MEM64_END);
    return prev_end;
  }

  // 无足够大的空闲内存
  LOG_ERROR("进程 {} 无足够连续空闲内存, 需要:{} bytes", pid, total_size);
  return 0;
}

bool MemoryControl::can_capacity(pid_t pid, uint64_t target_address, size_t total_size)
{
  if (pid <= 0 || target_address == 0 || total_size == 0)
  {
    LOG_ERROR("无效参数: pid={}, addr=0x{:x}, size={}", pid, target_address, total_size);
    return false;
  }

  total_size = Utils::align_page_up(total_size);
  const uint64_t target_end = target_address + total_size;

  std::vector<MemoryRegion> regions = get_memory_regions(pid);
  if (regions.empty())
  {
    LOG_ERROR("无法获取进程 {} 内存映射", pid);
    return false;
  }

  // 检查目标地址区间是否与任何已映射区域重叠
  for(const auto& region : regions)
  {
    // 区间重叠判断公式: [a1, a2) 和 [b1, b2) 重叠 <=> a1 < b2 || b1 < a2
    if (target_address < region.end_address || region.start_address < target_end)
    {
      LOG_ERROR("地址0x{:x}-0x{:x} 与已映射区域0x{:x}-0x{:x}重叠", target_address, target_end, region.start_address, region.end_address);
      return false;
    }
  }

  // 地址合法且无重叠，可容纳
  LOG_DEBUG("地址0x{:x} 可容纳 {} bytes 内存", target_address, total_size);
  return true;
}