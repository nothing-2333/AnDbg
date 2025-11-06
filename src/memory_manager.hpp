#pragma once

#include <cstdint>
#include <string>
#include <fmt/format.h>

class MemoryRegion
{
  uint64_t start_address;
  uint64_t end_address;
  uint64_t size;
  std::string permissions;  // 内存区域的访问权限标志
  uint64_t file_offset;     // 文件映射的偏移量
  std::string dev;          // 设备标识符
  uint64_t inode;           // 文件系统的 inode 编号
  std::string pathname;     // 映射的文件路径或区域描述

  std::string to_string() const 
  {
    return fmt::format("{:016x}-{:016x} {} {:08x} {} {} {}", 
      start_address, end_address, permissions, file_offset, dev, inode, pathname);
  }
};

class MemoryManager
{
private:

public:

};
/*
struct MemoryRegion {
    uint64_t start_addr;
    uint64_t end_addr;
    uint64_t size;
    std::string perms;
    uint64_t offset;
    std::string dev;
    uint64_t inode;
    std::string pathname;

    // 改进的辅助方法
    bool is_readable() const { return perms.find('r') != std::string::npos; }
    bool is_writable() const { return perms.find('w') != std::string::npos; }
    bool is_executable() const { return perms.find('x') != std::string::npos; }
    bool is_private() const { return perms.find('p') != std::string::npos; }
    bool is_shared() const { return perms.find('s') != std::string::npos; }
    bool is_anonymous() const { 
        return pathname.empty() || 
               pathname.find("[anon]") != std::string::npos ||
               pathname.find("//anon") != std::string::npos;
    }
    bool is_stack() const { 
        return pathname == "[stack]" || pathname.find("[stack") != std::string::npos;
    }
    bool is_heap() const { 
        return pathname == "[heap]" || pathname.find("[heap") != std::string::npos;
    }
    bool is_vdso() const { return pathname == "[vdso]"; }
    bool is_vvar() const { return pathname == "[vvar]"; }
    bool is_vsyscall() const { return pathname == "[vsyscall]"; }

    // 格式化输出
    std::string to_string() const {
        return fmt::format("{:016x}-{:016x} {} {:08x} {} {} {}",
                          start_addr, end_addr, perms, offset, dev, inode, pathname);
    }

    // 检查地址是否在区域内
    bool contains(uint64_t addr) const {
        return addr >= start_addr && addr < end_addr;
    }
};

class MemoryManager {
private:
    pid_t m_pid;
    std::vector<MemoryRegion> m_cached_regions;
    bool m_regions_cached;
    std::unordered_map<uint64_t, uint64_t> m_breakpoints; // 断点管理

public:
    explicit MemoryManager(pid_t pid = -1) 
        : m_pid(pid), m_regions_cached(false) {}

    // -------------------------- 缓存管理 --------------------------
    void invalidate_cache() {
        m_regions_cached = false;
        m_cached_regions.clear();
    }

    // 刷新内存区域缓存
    bool refresh_memory_regions() {
        m_cached_regions = get_memory_regions_impl();
        m_regions_cached = !m_cached_regions.empty();
        return m_regions_cached;
    }

    // -------------------------- 改进的内存读写 --------------------------
    bool read_memory(uint64_t addr, void* buf, size_t size) {
        if (m_pid == -1) {
            fmt::print(stderr, "MemoryManager: PID not set\n");
            return false;
        }
        
        if (buf == nullptr || size == 0) {
            fmt::print(stderr, "MemoryManager: Invalid buffer or size\n");
            return false;
        }

        // 检查地址权限
        if (!check_addr_permission(addr, false, false)) {
            fmt::print(stderr, "MemoryManager: No read permission at 0x{:x}\n", addr);
            return false;
        }

        // 使用 process_vm_readv 进行高效读取
        struct iovec local_iov = {buf, size};
        struct iovec remote_iov = {reinterpret_cast<void*>(addr), size};

        ssize_t ret = process_vm_readv(m_pid, &local_iov, 1, &remote_iov, 1, 0);
        if (ret == static_cast<ssize_t>(size)) {
            return true;
        }

        // 如果 process_vm_readv 失败，回退到 ptrace
        fmt::print("process_vm_readv failed ({}), falling back to ptrace\n", strerror(errno));
        return read_memory_ptrace(addr, buf, size);
    }

private:
    // 使用 ptrace 读取内存（备选方案）
    bool read_memory_ptrace(uint64_t addr, void* buf, size_t size) {
        auto* byte_buf = static_cast<uint8_t*>(buf);
        size_t bytes_read = 0;

        while (bytes_read < size) {
            errno = 0;
            long word = ptrace(PTRACE_PEEKDATA, m_pid, addr + bytes_read, nullptr);
            if (errno != 0) {
                fmt::print(stderr, "ptrace PEEKDATA failed at 0x{:x}: {}\n", 
                          addr + bytes_read, strerror(errno));
                return false;
            }

            size_t bytes_to_copy = std::min(sizeof(word), size - bytes_read);
            memcpy(byte_buf + bytes_read, &word, bytes_to_copy);
            bytes_read += bytes_to_copy;
        }

        return true;
    }

    // 使用 ptrace 写入内存（备选方案）
    bool write_memory_ptrace(uint64_t addr, const void* buf, size_t size) {
        const auto* byte_buf = static_cast<const uint8_t*>(buf);
        size_t bytes_written = 0;

        while (bytes_written < size) {
            long word = 0;
            size_t bytes_remaining = size - bytes_written;
            
            // 如果不足一个字，需要先读取原始数据
            if (bytes_remaining < sizeof(word)) {
                errno = 0;
                word = ptrace(PTRACE_PEEKDATA, m_pid, addr + bytes_written, nullptr);
                if (errno != 0) {
                    fmt::print(stderr, "ptrace PEEKDATA failed for partial write: {}\n", 
                              strerror(errno));
                    return false;
                }
            }

            size_t bytes_to_copy = std::min(sizeof(word), bytes_remaining);
            memcpy(&word, byte_buf + bytes_written, bytes_to_copy);

            if (ptrace(PTRACE_POKEDATA, m_pid, addr + bytes_written, word) == -1) {
                fmt::print(stderr, "ptrace POKEDATA failed at 0x{:x}: {}\n", 
                          addr + bytes_written, strerror(errno));
                return false;
            }

            bytes_written += bytes_to_copy;
        }

        return true;
    }

public:
    bool write_memory(uint64_t addr, const void* buf, size_t size) {
        if (m_pid == -1) {
            fmt::print(stderr, "MemoryManager: PID not set\n");
            return false;
        }
        
        if (buf == nullptr || size == 0) {
            fmt::print(stderr, "MemoryManager: Invalid buffer or size\n");
            return false;
        }

        // 检查地址权限
        if (!check_addr_permission(addr, true, false)) {
            fmt::print(stderr, "MemoryManager: No write permission at 0x{:x}\n", addr);
            return false;
        }

        // 使用 process_vm_writev 进行高效写入
        struct iovec local_iov = {const_cast<void*>(buf), size};
        struct iovec remote_iov = {reinterpret_cast<void*>(addr), size};

        ssize_t ret = process_vm_writev(m_pid, &local_iov, 1, &remote_iov, 1, 0);
        if (ret == static_cast<ssize_t>(size)) {
            return true;
        }

        // 如果 process_vm_writev 失败，回退到 ptrace
        fmt::print("process_vm_writev failed ({}), falling back to ptrace\n", strerror(errno));
        return write_memory_ptrace(addr, buf, size);
    }

    // -------------------------- 改进的内存布局查询 --------------------------
    std::vector<MemoryRegion> get_memory_regions() {
        if (m_regions_cached) {
            return m_cached_regions;
        }
        
        auto regions = get_memory_regions_impl();
        if (!regions.empty()) {
            m_cached_regions = regions;
            m_regions_cached = true;
        }
        return regions;
    }

private:
    std::vector<MemoryRegion> get_memory_regions_impl() {
        std::vector<MemoryRegion> regions;
        if (m_pid == -1) {
            return regions;
        }

        std::string maps_path = fmt::format("/proc/{}/maps", m_pid);
        std::ifstream maps_file(maps_path);
        if (!maps_file.is_open()) {
            fmt::print(stderr, "Failed to open {}: {}\n", maps_path, strerror(errno));
            return regions;
        }

        std::string line;
        while (std::getline(maps_file, line)) {
            if (line.empty()) continue;
            
            MemoryRegion region;
            if (parse_maps_line(line, region)) {
                regions.push_back(region);
            } else {
                fmt::print(stderr, "Failed to parse maps line: {}\n", line);
            }
        }

        return regions;
    }

    // 改进的 maps 解析器
    bool parse_maps_line(const std::string& line, MemoryRegion& region) {
        // 使用 sscanf 更健壮的解析
        char perms[8] = {0};
        char dev[16] = {0};
        char pathname[1024] = {0};
        
        int result = sscanf(line.c_str(), "%lx-%lx %7s %lx %15s %lu %1023[^\n]",
                          &region.start_addr, &region.end_addr, perms,
                          &region.offset, dev, &region.inode, pathname);
        
        if (result < 6) {
            return false;
        }

        region.size = region.end_addr - region.start_addr;
        region.perms = perms;
        region.dev = dev;
        region.pathname = (result >= 7) ? pathname : "";

        // 清理路径名中的空格
        if (!region.pathname.empty()) {
            size_t start = region.pathname.find_first_not_of(' ');
            if (start != std::string::npos) {
                region.pathname = region.pathname.substr(start);
            }
        }

        return true;
    }

public:
    // -------------------------- 高级内存操作 --------------------------
    
    // 搜索内存中的模式
    std::vector<uint64_t> search_memory(const std::vector<uint8_t>& pattern, 
                                       uint64_t start_addr = 0, 
                                       uint64_t end_addr = 0) {
        std::vector<uint64_t> results;
        
        auto regions = get_memory_regions();
        for (const auto& region : regions) {
            // 跳过不可读区域
            if (!region.is_readable()) continue;
            
            // 检查地址范围
            uint64_t search_start = std::max(region.start_addr, start_addr);
            uint64_t search_end = end_addr ? std::min(region.end_addr, end_addr) : region.end_addr;
            
            if (search_start >= search_end) continue;
            
            // 读取整个区域进行搜索（优化：可以分块读取）
            std::vector<uint8_t> buffer(search_end - search_start);
            if (read_memory(search_start, buffer.data(), buffer.size())) {
                // 简单的模式匹配
                for (size_t i = 0; i <= buffer.size() - pattern.size(); ++i) {
                    if (memcmp(&buffer[i], pattern.data(), pattern.size()) == 0) {
                        results.push_back(search_start + i);
                    }
                }
            }
        }
        
        return results;
    }

    // 转储内存到文件
    bool dump_memory(uint64_t start_addr, uint64_t end_addr, const std::string& filename) {
        size_t size = end_addr - start_addr;
        if (size == 0 || size > 1024 * 1024 * 1024) { // 限制1GB
            return false;
        }
        
        std::vector<uint8_t> buffer(size);
        if (!read_memory(start_addr, buffer.data(), size)) {
            return false;
        }
        
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        return file.good();
    }

    // 分配内存（在目标进程中）
    uint64_t allocate_memory(size_t size, int prot = PROT_READ | PROT_WRITE) {
        // 通过 mmap 系统调用分配内存
        RegisterManager reg_manager(m_pid);
        struct user_pt_regs regs, orig_regs;
        
        if (!reg_manager.get_all_gpr(m_pid, regs)) {
            return 0;
        }
        orig_regs = regs;

        // ARM64 mmap 系统调用参数
        regs.regs[0] = 0;          // addr (0 = 由系统选择)
        regs.regs[1] = size;        // length
        regs.regs[2] = prot;        // prot
        regs.regs[3] = MAP_PRIVATE | MAP_ANONYMOUS; // flags
        regs.regs[4] = -1;          // fd
        regs.regs[5] = 0;           // offset
        regs.regs[8] = 222;         // __NR_mmap (ARM64)

        if (!reg_manager.set_all_gpr(m_pid, regs) ||
            !execute_syscall(m_pid) ||
            !reg_manager.get_all_gpr(m_pid, regs)) {
            reg_manager.set_all_gpr(m_pid, orig_regs);
            return 0;
        }

        reg_manager.set_all_gpr(m_pid, orig_regs);
        return (regs.regs[0] > (uint64_t)-4096) ? 0 : regs.regs[0]; // 检查错误
    }

private:
    // 执行系统调用的辅助函数
    bool execute_syscall(pid_t pid) {
        if (ptrace(PTRACE_CONT, pid, nullptr, 0) == -1) {
            return false;
        }
        
        int status;
        return waitpid(pid, &status, 0) != -1 && WIFSTOPPED(status);
    }
};

class DebuggerCore {
private:
    pid_t m_pid;
    std::vector<pid_t> m_tids;
    std::unique_ptr<RegisterManager> m_reg_manager;
    std::unique_ptr<MemoryManager> m_mem_manager;
    bool m_attached;

public:
    DebuggerCore() : m_pid(-1), m_attached(false) {}

    bool launch(LaunchInfo& launch_info) {
        // ... 原有的启动逻辑 ...
        m_pid = pid;
        m_mem_manager = std::make_unique<MemoryManager>(m_pid);
        m_reg_manager = std::make_unique<RegisterManager>(m_pid);
        // ...
    }

    bool attach(pid_t pid) {
        // ... 原有的附加逻辑 ...
        m_pid = pid;
        m_mem_manager = std::make_unique<MemoryManager>(m_pid);
        m_reg_manager = std::make_unique<RegisterManager>(m_pid);
        // ...
    }

    // 内存操作委托
    bool read_memory(uint64_t addr, void* buf, size_t size) {
        return m_mem_manager ? m_mem_manager->read_memory(addr, buf, size) : false;
    }

    template<typename T>
    bool read_memory(uint64_t addr, T& value) {
        return m_mem_manager ? m_mem_manager->read_memory(addr, value) : false;
    }

    bool write_memory(uint64_t addr, const void* buf, size_t size) {
        return m_mem_manager ? m_mem_manager->write_memory(addr, buf, size) : false;
    }

    template<typename T>
    bool write_memory(uint64_t addr, const T& value) {
        return m_mem_manager ? m_mem_manager->write_memory(addr, value) : false;
    }

    // 高级内存功能
    std::vector<MemoryRegion> get_memory_map() {
        return m_mem_manager ? m_mem_manager->get_memory_regions() : std::vector<MemoryRegion>{};
    }

    std::string read_string(uint64_t addr, size_t max_len = 4096) {
        return m_mem_manager ? m_mem_manager->read_cstring(addr, max_len) : "";
    }

    std::vector<uint64_t> search_memory(const std::vector<uint8_t>& pattern) {
        return m_mem_manager ? m_mem_manager->search_memory(pattern) : std::vector<uint64_t>{};
    }

    // 获取内存管理器（直接访问高级功能）
    MemoryManager* memory() { return m_mem_manager.get(); }
    RegisterManager* registers() { return m_reg_manager.get(); }
};
*/