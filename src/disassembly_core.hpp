#pragma once

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "fmt/format.h"
#include "singleton_base.hpp"
#include "capstone/capstone.h"


// 指令类型枚举
enum class InstructionType {
  UNKNOWN,              // 未知指令
  NORMAL,               // 普通指令
  UNCONDITIONAL_BRANCH, // 无条件跳转指令
  CONDITIONAL_BRANCH,   // 有条件跳转指令
  AUTHENTICATED_BRANCH, // 带认证的跳转指令
  RETURN,               // 返回指令
  SYSCALL,              // 系统调用指令
  INTERRUPT,            // 中断指令
  EXCEPTION             // 异常指令
};

// 反汇编结果结构体
struct DisassembleResult 
{
  uint64_t address;               // 指令地址
  std::string mnemonic;           // 指令助记符
  std::string operands;           // 操作数字符串
  InstructionType type;           // 指令类型
  size_t size;                    // 指令大小(字节)

  // 构造函数
  DisassembleResult() = default;

  DisassembleResult(uint64_t address_, const std::string& mnemonic_, const std::string& operands_, InstructionType type_, size_t size_)
    : address(address_), mnemonic(mnemonic_), operands(operands_), type(type_), size(size_) {}

  // 转换为字符串表示
  std::string to_string() const { return fmt::format("0x{:x}: {} {}", address, mnemonic, operands); }

  // 反汇编字符串
  std::string full_disassemble() const { return fmt::format("{} {}", mnemonic, operands); }

  // 判断是跳转指令类型
  inline bool is_branch(InstructionType type) const 
  {  
    if (type == InstructionType::UNCONDITIONAL_BRANCH ||
        type == InstructionType::CONDITIONAL_BRANCH ||
        type == InstructionType::AUTHENTICATED_BRANCH || 
        type == InstructionType::RETURN) 
    {
      return true;
    }
    return false;
  }

};

class DisassembleCore : public SingletonBase<DisassembleCore>
{
private:

  // 友元声明, 允许基类访问子类的私有构造函数
  friend class SingletonBase<DisassembleCore>; 

  // 私有构造函数, 析构函数
  DisassembleCore();
  ~DisassembleCore();

  // capstone 句柄
  csh handle_;

  // 缓存
  cs_insn* instruction_cache_;

public:

  // 反汇编单条指令
  std::optional<DisassembleResult> disassemble_single(pid_t pid, uint64_t address);

  // 反汇编
  std::optional<std::vector<DisassembleResult>> disassemble(pid_t pid, uint64_t address, size_t max_count);

  // 检查 capstone 是否初始化成功
  inline bool is_initialized() const { return handle_ != 0 && instruction_cache_ != nullptr; } 

private:

  // 将 capstone 指令转换为 DisassembleResult
  DisassembleResult convert_capstone_instruction(const cs_insn& insn);

  // 系统调用指令
  bool is_syscall_instruction(const cs_insn& insn);

  // 中断指令
  bool is_interrupt_instruction(const cs_insn& insn);

  // 异常指令
  bool is_exception_instruction(const cs_insn& insn);

  // 无条件跳转指令
  bool is_unconditional_branch_instruction(const cs_insn& insn);

  // 有条件跳转指令
  bool is_conditional_branch_instruction(const cs_insn& insn);

  // 带认证的跳转指令
  bool is_authenticated_branch_instruction(const cs_insn& insn);

  // 返回指令
  bool is_return_instruction(const cs_insn& insn);
};