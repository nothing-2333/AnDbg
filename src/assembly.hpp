#pragma once

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "fmt/format.h"
#include "singleton_base.hpp"
#include "capstone/capstone.h"

namespace Assembly 
{

struct Operand 
{
  enum class type 
  {
    REG,  // 寄存器: x0, sp, pc
    IMM,  // 立即数: #1, #0x100
    MEM,  // 内存: [x0], [x0 + #0x100], [x0, x1, LSL#2]
    OTHER,
  };

  type type;
  uint32_t reg;   // 寄存器 ID
  uint32_t reg2;  // 寄存器 ID, 只有内存 [x0, x1, LSL#2] 会用到
  int64_t  imm;   // 立即数
};

// 反汇编结果结构体
struct Instruction 
{
  // 指令类型枚举
  enum class Type 
  {
    UNKNOWN,              // 未知指令
    NORMAL,               // 普通指令
    UNCONDITIONAL_BRANCH, // 无条件跳转指令
    CONDITIONAL_BRANCH,   // 有条件跳转指令
    RETURN,               // 返回指令
    SYSCALL,              // 系统调用指令
  };

  Type type;           // 指令类型
  std::vector<char> data;         // 指令元数据
  std::string mnemonic;           // 指令助记符
  std::string op_str;             // 操作数字符串
  Operand ops[8];                 // 操作数数组
  uint8_t op_count = 0;           // 真实数量
  
  // 构造函数
  Instruction() = default;

  Instruction(Type type_, std::vector<char> data, const std::string& mnemonic_, 
  const std::string& op_str_, const Operand* ops_, uint8_t op_count_)
    : type(type_), data(data), mnemonic(mnemonic_), op_str(op_str_), op_count(op_count_)
  {
    for (int i = 0; i < op_count; i++) 
    {
      ops[i] = ops_[i];
    }
  }

  // 转换为字符串表示
  std::string to_string() const { return fmt::format("{} {}", mnemonic, op_str); }

  // 判断是跳转指令类型
  inline bool is_branch(Type type) const 
  {  
    if (type == Type::UNCONDITIONAL_BRANCH ||
        type == Type::CONDITIONAL_BRANCH ||
        type == Type::RETURN) 
    {
      return true;
    }
    return false;
  }

};

class DisassemblyControl : public SingletonBase<DisassemblyControl>
{
private:

  // 友元声明, 允许基类访问子类的私有构造函数
  friend class SingletonBase<DisassemblyControl>; 

  // 私有构造函数, 析构函数
  DisassemblyControl();
  ~DisassemblyControl();

  // capstone 句柄
  csh handle_;

public:
  // 反汇编
  std::optional<std::vector<Instruction>> disassemble(const std::vector<char>& codes);

private:
  // 系统调用指令
  bool is_syscall_instruction(const cs_insn& insn);

  // 无条件跳转指令
  bool is_unconditional_branch_instruction(const cs_insn& insn);

  // 有条件跳转指令
  bool is_conditional_branch_instruction(const cs_insn& insn);

  // 返回指令
  bool is_return_instruction(const cs_insn& insn);
};

};
