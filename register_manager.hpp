#include <cstdint>
#include <memory>
#include <sched.h>
#pragma omce 

#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <elf.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstring>
#include <bits/types/struct_iovec.h>
#include <cstddef>
#include <sys/types.h>

#include "utils.hpp"


// ARM64 寄存器枚举
enum class Register
{
  // 通用寄存器
  X0, X1, X2, X3, X4, X5, X6, X7, X8, X9,
  X10, X11, X12, X13, X14, X15, X16, X17, X18, X19,
  X20, X21, X22, X23, X24, X25, X26, X27, X28, X29,
  X30, SP, PC, CPSR,
  
  // 别名
  FP = X29,         // 帧指针
  LR = X30,         // 链接寄存器
  PSTATE = CPSR,    // 程序状态寄存器
  
  // 浮点/SIMD 寄存器 (32位)
  S0, S1, S2, S3, S4, S5, S6, S7,
  S8, S9, S10, S11, S12, S13, S14, S15,
  S16, S17, S18, S19, S20, S21, S22, S23,
  S24, S25, S26, S27, S28, S29, S30, S31,
  
  // 双精度浮点寄存器 (64位)
  D0, D1, D2, D3, D4, D5, D6, D7,
  D8, D9, D10, D11, D12, D13, D14, D15,
  D16, D17, D18, D19, D20, D21, D22, D23,
  D24, D25, D26, D27, D28, D29, D30, D31,
  
  // 浮点状态寄存器
  FPSR, FPCR
};

#ifndef __user_pt_regs
// ARM64 通用寄存器结构体定义
struct user_pt_regs 
{
  uint64_t regs[31];  // x0-x30
  uint64_t sp;        // 栈指针
  uint64_t pc;        // 程序计数器
  uint64_t pstate;    // 程序状态寄存器
};
#endif

#ifndef __UINT128_TYPE__
typedef struct 
{
  uint64_t low;
  uint64_t high;
} uint128_t;
#endif

// ARM64 浮点寄存器结构体定义
struct user_fpsimd_state 
{
  uint128_t vregs[32];    // 128 位向量寄存器 v0-v31
  uint32_t fpsr;          // 浮点状态寄存器
  uint32_t fpcr;          // 浮点控制寄存器
};

class RegisterManager
{
private:
  // 寄存器缓存结构
  std::unique_ptr<struct user_pt_regs> gpr_cache;   // 通用寄存器
  std::unique_ptr<struct user_fpsimd_state> fpr_cache;    // 浮点/SIMD 寄存器
    
  // 缓存有效性标志
  bool gpr_cache_valid;
  bool fpr_cache_valid;

  // ARM64 寄存器集类型
  enum RegisterSet {
      REGSET_GPR = NT_PRSTATUS,        // 通用寄存器
      REGSET_FPR = NT_FPREGSET,        // 浮点/SIMD 寄存器
  };

  // ptrace PTRACE_GETREGSET 封装
  bool ptrace_get_regset(pid_t pid, void* data, size_t size, RegisterSet regset);
  // {
  //   struct iovec iov;
  //   iov.iov_base = data;
  //   iov.iov_len = size;

  //   return Utils::ptrace_wrapper(PTRACE_GETREGSET, pid, (void*)(uintptr_t)regset, &iov);
  // }

  // ptrace PTRACE_SETREGSET 封装
  bool ptrace_set_regset(pid_t pid, const void* data, size_t size, RegisterSet regset);
  // {
  //   struct iovec iov;
  //   iov.iov_base = const_cast<void*>(data);
  //   iov.iov_len = size;

  //   return Utils::ptrace_wrapper(PTRACE_SETREGSET, pid, (void*)(uintptr_t)regset, &iov);
  // }

  // 刷新通用寄存器缓存
  bool refresh_gpr_cache(pid_t pid);
  // {
  //   if (ptrace_get_regset(pid, &gpr_cache, sizeof(gpr_cache), REGSET_GPR))
  //   {
  //     gpr_cache_valid = true;
  //     return true;
  //   }
  //   else 
  //   {
  //     gpr_cache_valid = false;
  //     return false;
  //   }
  // }

  // 刷新浮点寄存器缓存
  bool refresh_fpr_cache(pid_t pid);

  // 提交通用寄存器缓存
  bool commit_gpr_cache(pid_t pid);

  // 提交浮点寄存器缓存
  bool commit_fpr_cache(pid_t pid);

public: 
  explicit RegisterManager();

  // 批量获取所有通用寄存器
  struct user_pt_regs get_all_gpr(pid_t pid);

  // 批量设置所有通用寄存器
  bool set_all_gpr(pid_t pid, const struct user_pt_regs& regs);

  // 获取单个通用寄存器
  uint64_t get_gpr(pid_t pid, Register reg);

  // 设置单个通用寄存器
  bool set_gpr(pid_t pid, Register reg, uint64_t value);

  // 批量获取所有浮点寄存器
  struct user_fpsimd_state get_all_fpr(pid_t pid);

  // 批量设置所有浮点寄存器
  bool set_all_fpr(pid_t pid, const struct user_fpsimd_state& fpr);

  // 获取浮点寄存器值(32位)
  uint32_t get_fpr32(pid_t pid, Register reg);
  uint64_t get_fpr64(pid_t pid, Register reg);

  // 获取浮点状态寄存器
  uint32_t get_fpsr(pid_t pid);
  uint32_t get_fpcr(pid_t pid);

  // 设置浮点寄存器值
  bool set_fpr32(pid_t pid, Register reg, uint32_t value);
  bool set_fpr64(pid_t pid, Register reg, uint64_t value);
  
  // 获取浮点状态寄存器
  uint32_t set_fpsr(pid_t pid);
  uint32_t set_fpcr(pid_t pid);

};

/*
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <elf.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstring>

class RegisterManager {
private:
    pid_t pid;
    
    // 寄存器缓存结构
    struct user_pt_regs gpr_cache;          // 通用寄存器
    struct user_fpsimd_state fpr_cache;     // 浮点/SIMD 寄存器
    
    // 缓存有效性标志
    bool gpr_cache_valid = false;
    bool fpr_cache_valid = false;

    // ARM64 寄存器集类型
    enum RegisterSet {
        REGSET_GPR = NT_PRSTATUS,        // 通用寄存器
        REGSET_FPR = NT_FPREGSET,        // 浮点/SIMD 寄存器
    };

    // 现代 ptrace 接口封装
    bool ptraceGetRegset(void* data, size_t size, RegisterSet regset) {
        struct iovec iov;
        iov.iov_base = data;
        iov.iov_len = size;
        
        return ptrace(PTRACE_GETREGSET, pid, (void*)(uintptr_t)regset, &iov) != -1;
    }

    bool ptraceSetRegset(const void* data, size_t size, RegisterSet regset) {
        struct iovec iov;
        iov.iov_base = const_cast<void*>(data);
        iov.iov_len = size;
        
        return ptrace(PTRACE_SETREGSET, pid, (void*)(uintptr_t)regset, &iov) != -1;
    }

    // 刷新通用寄存器缓存
    bool refreshGPRCache() {
        if (ptraceGetRegset(&gpr_cache, sizeof(gpr_cache), REGSET_GPR)) {
            gpr_cache_valid = true;
            return true;
        }
        gpr_cache_valid = false;
        return false;
    }

    // 刷新浮点寄存器缓存
    bool refreshFPRCache() {
        if (ptraceGetRegset(&fpr_cache, sizeof(fpr_cache), REGSET_FPR)) {
            fpr_cache_valid = true;
            return true;
        }
        fpr_cache_valid = false;
        return false;
    }

    // 提交通用寄存器缓存
    bool commitGPRCache() {
        if (!gpr_cache_valid) return false;
        return ptraceSetRegset(&gpr_cache, sizeof(gpr_cache), REGSET_GPR);
    }

    // 提交浮点寄存器缓存
    bool commitFPRCache() {
        if (!fpr_cache_valid) return false;
        return ptraceSetRegset(&fpr_cache, sizeof(fpr_cache), REGSET_FPR);
    }

public:
    // ARM64 寄存器枚举
    enum class Register {
        // 通用寄存器
        X0, X1, X2, X3, X4, X5, X6, X7, X8, X9,
        X10, X11, X12, X13, X14, X15, X16, X17, X18, X19,
        X20, X21, X22, X23, X24, X25, X26, X27, X28, X29,
        X30, SP, PC, CPSR,
        
        // 别名
        FP = X29,    // 帧指针
        LR = X30,    // 链接寄存器
        PSTATE = CPSR, // 程序状态寄存器
        
        // 浮点/SIMD 寄存器 (32位)
        S0, S1, S2, S3, S4, S5, S6, S7,
        S8, S9, S10, S11, S12, S13, S14, S15,
        S16, S17, S18, S19, S20, S21, S22, S23,
        S24, S25, S26, S27, S28, S29, S30, S31,
        
        // 双精度浮点寄存器 (64位)
        D0, D1, D2, D3, D4, D5, D6, D7,
        D8, D9, D10, D11, D12, D13, D14, D15,
        D16, D17, D18, D19, D20, D21, D22, D23,
        D24, D25, D26, D27, D28, D29, D30, D31,
        
        // 浮点状态寄存器
        FPSR, FPCR
    };

    explicit RegisterManager(pid_t target_pid) : pid(target_pid) {
        // 初始化缓存
        memset(&gpr_cache, 0, sizeof(gpr_cache));
        memset(&fpr_cache, 0, sizeof(fpr_cache));
    }

    // ===== 通用寄存器操作 =====
    
    // 批量获取所有通用寄存器
    bool getAllGPR(struct user_pt_regs& regs) {
        if (ptraceGetRegset(&regs, sizeof(regs), REGSET_GPR)) {
            gpr_cache = regs;
            gpr_cache_valid = true;
            return true;
        }
        gpr_cache_valid = false;
        return false;
    }

    // 批量设置所有通用寄存器
    bool setAllGPR(const struct user_pt_regs& regs) {
        if (ptraceSetRegset(&regs, sizeof(regs), REGSET_GPR)) {
            gpr_cache = regs;
            gpr_cache_valid = true;
            return true;
        }
        return false;
    }

    // 获取单个通用寄存器
    uint64_t getGPR(Register reg) {
        if (!gpr_cache_valid && !refreshGPRCache()) {
            return static_cast<uint64_t>(-1);
        }

        switch (reg) {
            case Register::X0:  return gpr_cache.regs[0];
            case Register::X1:  return gpr_cache.regs[1];
            case Register::X2:  return gpr_cache.regs[2];
            case Register::X3:  return gpr_cache.regs[3];
            case Register::X4:  return gpr_cache.regs[4];
            case Register::X5:  return gpr_cache.regs[5];
            case Register::X6:  return gpr_cache.regs[6];
            case Register::X7:  return gpr_cache.regs[7];
            case Register::X8:  return gpr_cache.regs[8];
            case Register::X9:  return gpr_cache.regs[9];
            case Register::X10: return gpr_cache.regs[10];
            case Register::X11: return gpr_cache.regs[11];
            case Register::X12: return gpr_cache.regs[12];
            case Register::X13: return gpr_cache.regs[13];
            case Register::X14: return gpr_cache.regs[14];
            case Register::X15: return gpr_cache.regs[15];
            case Register::X16: return gpr_cache.regs[16];
            case Register::X17: return gpr_cache.regs[17];
            case Register::X18: return gpr_cache.regs[18];
            case Register::X19: return gpr_cache.regs[19];
            case Register::X20: return gpr_cache.regs[20];
            case Register::X21: return gpr_cache.regs[21];
            case Register::X22: return gpr_cache.regs[22];
            case Register::X23: return gpr_cache.regs[23];
            case Register::X24: return gpr_cache.regs[24];
            case Register::X25: return gpr_cache.regs[25];
            case Register::X26: return gpr_cache.regs[26];
            case Register::X27: return gpr_cache.regs[27];
            case Register::X28: return gpr_cache.regs[28];
            case Register::X29: return gpr_cache.regs[29];
            case Register::X30: return gpr_cache.regs[30];
            case Register::SP:  return gpr_cache.sp;
            case Register::PC:  return gpr_cache.pc;
            case Register::CPSR:return gpr_cache.pstate;
            default: return static_cast<uint64_t>(-1);
        }
    }

    // 设置单个通用寄存器
    bool setGPR(Register reg, uint64_t value) {
        if (!gpr_cache_valid && !refreshGPRCache()) {
            return false;
        }

        switch (reg) {
            case Register::X0:  gpr_cache.regs[0] = value; break;
            case Register::X1:  gpr_cache.regs[1] = value; break;
            case Register::X2:  gpr_cache.regs[2] = value; break;
            case Register::X3:  gpr_cache.regs[3] = value; break;
            case Register::X4:  gpr_cache.regs[4] = value; break;
            case Register::X5:  gpr_cache.regs[5] = value; break;
            case Register::X6:  gpr_cache.regs[6] = value; break;
            case Register::X7:  gpr_cache.regs[7] = value; break;
            case Register::X8:  gpr_cache.regs[8] = value; break;
            case Register::X9:  gpr_cache.regs[9] = value; break;
            case Register::X10: gpr_cache.regs[10] = value; break;
            case Register::X11: gpr_cache.regs[11] = value; break;
            case Register::X12: gpr_cache.regs[12] = value; break;
            case Register::X13: gpr_cache.regs[13] = value; break;
            case Register::X14: gpr_cache.regs[14] = value; break;
            case Register::X15: gpr_cache.regs[15] = value; break;
            case Register::X16: gpr_cache.regs[16] = value; break;
            case Register::X17: gpr_cache.regs[17] = value; break;
            case Register::X18: gpr_cache.regs[18] = value; break;
            case Register::X19: gpr_cache.regs[19] = value; break;
            case Register::X20: gpr_cache.regs[20] = value; break;
            case Register::X21: gpr_cache.regs[21] = value; break;
            case Register::X22: gpr_cache.regs[22] = value; break;
            case Register::X23: gpr_cache.regs[23] = value; break;
            case Register::X24: gpr_cache.regs[24] = value; break;
            case Register::X25: gpr_cache.regs[25] = value; break;
            case Register::X26: gpr_cache.regs[26] = value; break;
            case Register::X27: gpr_cache.regs[27] = value; break;
            case Register::X28: gpr_cache.regs[28] = value; break;
            case Register::X29: gpr_cache.regs[29] = value; break;
            case Register::X30: gpr_cache.regs[30] = value; break;
            case Register::SP:  gpr_cache.sp = value; break;
            case Register::PC:  gpr_cache.pc = value; break;
            case Register::CPSR:gpr_cache.pstate = value; break;
            default: return false;
        }

        return commitGPRCache();
    }

    // ===== 浮点寄存器操作 =====
    
    // 批量获取所有浮点寄存器
    bool getAllFPR(struct user_fpsimd_state& fpr) {
        if (ptraceGetRegset(&fpr, sizeof(fpr), REGSET_FPR)) {
            fpr_cache = fpr;
            fpr_cache_valid = true;
            return true;
        }
        fpr_cache_valid = false;
        return false;
    }

    // 批量设置所有浮点寄存器
    bool setAllFPR(const struct user_fpsimd_state& fpr) {
        if (ptraceSetRegset(&fpr, sizeof(fpr), REGSET_FPR)) {
            fpr_cache = fpr;
            fpr_cache_valid = true;
            return true;
        }
        return false;
    }

    // 获取浮点寄存器值（32位）
    uint32_t getFPR32(Register reg) {
        if (!fpr_cache_valid && !refreshFPRCache()) {
            return static_cast<uint32_t>(-1);
        }

        if (reg < Register::S0 || reg > Register::S31) {
            return static_cast<uint32_t>(-1);
        }

        uint32_t index = static_cast<uint32_t>(reg) - static_cast<uint32_t>(Register::S0);
        return fpr_cache.vregs[index] & 0xFFFFFFFF;
    }

    // 获取浮点寄存器值（64位）
    uint64_t getFPR64(Register reg) {
        if (!fpr_cache_valid && !refreshFPRCache()) {
            return static_cast<uint64_t>(-1);
        }

        if (reg < Register::D0 || reg > Register::D31) {
            return static_cast<uint64_t>(-1);
        }

        uint32_t index = static_cast<uint32_t>(reg) - static_cast<uint32_t>(Register::D0);
        return fpr_cache.vregs[index];
    }

    // 获取浮点状态寄存器
    uint32_t getFPSR() {
        if (!fpr_cache_valid && !refreshFPRCache()) {
            return static_cast<uint32_t>(-1);
        }
        return fpr_cache.fpsr;
    }

    uint32_t getFPCR() {
        if (!fpr_cache_valid && !refreshFPRCache()) {
            return static_cast<uint32_t>(-1);
        }
        return fpr_cache.fpcr;
    }

    // 设置浮点寄存器值
    bool setFPR32(Register reg, uint32_t value) {
        if (!fpr_cache_valid && !refreshFPRCache()) {
            return false;
        }

        if (reg < Register::S0 || reg > Register::S31) {
            return false;
        }

        uint32_t index = static_cast<uint32_t>(reg) - static_cast<uint32_t>(Register::S0);
        
        // 保持高32位不变，只修改低32位
        uint64_t current = fpr_cache.vregs[index];
        fpr_cache.vregs[index] = (current & 0xFFFFFFFF00000000) | value;
        
        return commitFPRCache();
    }

    bool setFPR64(Register reg, uint64_t value) {
        if (!fpr_cache_valid && !refreshFPRCache()) {
            return false;
        }

        if (reg < Register::D0 || reg > Register::D31) {
            return false;
        }

        uint32_t index = static_cast<uint32_t>(reg) - static_cast<uint32_t>(Register::D0);
        fpr_cache.vregs[index] = value;
        
        return commitFPRCache();
    }

    bool setFPSR(uint32_t value) {
        if (!fpr_cache_valid && !refreshFPRCache()) {
            return false;
        }
        fpr_cache.fpsr = value;
        return commitFPRCache();
    }

    bool setFPCR(uint32_t value) {
        if (!fpr_cache_valid && !refreshFPRCache()) {
            return false;
        }
        fpr_cache.fpcr = value;
        return commitFPRCache();
    }

    // ===== 便捷方法 =====
    
    uint64_t getProgramCounter() { return getGPR(Register::PC); }
    uint64_t getStackPointer() { return getGPR(Register::SP); }
    uint64_t getFramePointer() { return getGPR(Register::FP); }
    uint64_t getLinkRegister() { return getGPR(Register::LR); }

    bool setProgramCounter(uint64_t pc) { return setGPR(Register::PC, pc); }
    bool setStackPointer(uint64_t sp) { return setGPR(Register::SP, sp); }
    bool setLinkRegister(uint64_t lr) { return setGPR(Register::LR, lr); }

    // ===== 缓存管理 =====
    
    void invalidateAllCaches() {
        gpr_cache_valid = false;
        fpr_cache_valid = false;
    }

    void invalidateGPRCache() { gpr_cache_valid = false; }
    void invalidateFPRCache() { fpr_cache_valid = false; }

    bool isGPRCacheValid() const { return gpr_cache_valid; }
    bool isFPRCacheValid() const { return fpr_cache_valid; }

    // ===== 调试和显示 =====
    
    void dumpGPR() {
        struct user_pt_regs regs;
        if (getAllGPR(regs)) {
            printf("=== ARM64 General Purpose Registers ===\n");
            for (int i = 0; i < 31; ++i) {
                printf("x%-2d = 0x%016lx\n", i, regs.regs[i]);
            }
            printf("sp  = 0x%016lx\n", regs.sp);
            printf("pc  = 0x%016lx\n", regs.pc);
            printf("cpsr= 0x%016lx\n", regs.pstate);
        } else {
            printf("Failed to read GPR\n");
        }
    }

    void dumpFPR() {
        struct user_fpsimd_state fpr;
        if (getAllFPR(fpr)) {
            printf("=== ARM64 Floating Point Registers ===\n");
            
            // 显示 32 位浮点寄存器 (S0-S31)
            printf("32-bit registers (S0-S31):\n");
            for (int i = 0; i < 32; i += 4) {
                printf("S%-2d: 0x%08x  S%-2d: 0x%08x  S%-2d: 0x%08x  S%-2d: 0x%08x\n",
                       i, getFPR32(static_cast<Register>(static_cast<int>(Register::S0) + i)),
                       i+1, getFPR32(static_cast<Register>(static_cast<int>(Register::S0) + i + 1)),
                       i+2, getFPR32(static_cast<Register>(static_cast<int>(Register::S0) + i + 2)),
                       i+3, getFPR32(static_cast<Register>(static_cast<int>(Register::S0) + i + 3)));
            }

            // 显示 64 位浮点寄存器 (D0-D31)
            printf("\n64-bit registers (D0-D31):\n");
            for (int i = 0; i < 32; i += 2) {
                printf("D%-2d: 0x%016lx  D%-2d: 0x%016lx\n",
                       i, getFPR64(static_cast<Register>(static_cast<int>(Register::D0) + i)),
                       i+1, getFPR64(static_cast<Register>(static_cast<int>(Register::D0) + i + 1)));
            }

            // 显示浮点状态寄存器
            printf("\nFP Status Registers:\n");
            printf("FPSR: 0x%08x  FPCR: 0x%08x\n", getFPSR(), getFPCR());
        } else {
            printf("Failed to read FPR\n");
        }
    }

    // 获取进程ID
    pid_t getPid() const { return pid; }
};
*/

