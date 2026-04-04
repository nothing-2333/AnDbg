#include "rpc_server.hpp"
#include "debugger_core.hpp"
#include "log.hpp"
#include "status.hpp"
#include <cstdint>
#include <string>
#include <sys/types.h>
#include "utils.hpp"


// todo: 参数检查


void acp_init(Base::RPCServer& server, Core::DebuggerCore& debugger)
{
  // 返回数据或者接受数据信息都要用 json 字符串, 如果没有信息可以穿空或者提示字符串

  server.register_handler("attach", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    if (!json_data.contains("target") || !json_data["target"].is_string())
      return Base::Status::fail("attach 需要 target 参数, 且必须是字符串");
    std::string target = json_data["target"];
    return debugger.attach(target);
  });

  server.register_handler("launch", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    if (!json_data.contains("target") || !json_data["target"].is_string())
      return Base::Status::fail("launch 需要 target 参数, 且必须是字符串");
    std::string program = json_data["target"];
    return debugger.launch(program);
  });

  server.register_handler("detach", [&debugger](const std::string& params) -> Base::Status
  {
    return debugger.detach();
  });

  server.register_handler("kill", [&debugger](const std::string& params) -> Base::Status
  {
    return debugger.kill();
  });

  server.register_handler("resume_thread", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    pid_t tid = json_data["tid"];
    return debugger.resume_thread(tid);
  });

  server.register_handler("resume", [&debugger](const std::string& params) -> Base::Status
  {
    return debugger.resume();
  });

  server.register_handler("step_into", [&debugger](const std::string& params) -> Base::Status
  {
    return debugger.step_into();
  });

  server.register_handler("step_over", [&debugger](const std::string& params) -> Base::Status
  {
    return debugger.step_over();
  });

  server.register_handler("pause", [&debugger](const std::string& params) -> Base::Status
  {
    return debugger.pause();
  });
  
  server.register_handler("read_memory", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    uint64_t address = json_data["address"];
    size_t size = json_data["size"];
    std::vector<char> buffer(size);
    Base::Status s = debugger.read_memory(address, buffer.data(), size);
    if (s.is_fail()) return s;
    else 
    {
      nlohmann::json result = 
      {
        {"address", address},
        {"size", size},
        {"data", buffer}
      };
      return Base::Status::success(result);
    }
  });

  server.register_handler("write_memory", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    if (!json_data.contains("address") || !json_data.contains("data")) 
      return Base::Status::fail("缺少参数: address / data");

    uint64_t address = json_data["address"];
    std::vector<uint8_t> buffer = json_data["data"].get<std::vector<uint8_t>>();
    
    return debugger.write_memory(address, buffer.data(), buffer.size());
  });

  server.register_handler("get_memory_regions", [&debugger](const std::string& params) -> Base::Status
  {
    std::vector<Core::MemoryRegion> regions;
    Base::Status s = debugger.get_memory_regions(regions);
    if (s.is_fail()) return s;
    else 
    {
      nlohmann::json result;
      for (const auto& region : regions) 
      {
        result.push_back({
          {"start_address", region.start_address},
          {"size", region.size},
          {"offset", region.offset},
          {"device", region.device},
          {"inode", region.inode},
          {"permissions", region.permissions},
          {"pathname", region.pathname}
        });
      }
      return Base::Status::success(result);
    }
  });

  server.register_handler("allocate_memory", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    size_t size = json_data["size"];
    int permissions = json_data["permissions"];

    uint64_t allocated_address;
    Base::Status s = debugger.allocate_memory(size, permissions, allocated_address);
    if (s.is_fail()) return s;
    else 
    {
      nlohmann::json result;
      result["allocated_address"] = allocated_address;
      return Base::Status::success(result);
    }
  });

  server.register_handler("deallocate_memory", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    uint64_t address = json_data["address"];
    return debugger.deallocate_memory(address);
  });

  server.register_handler("read_registers", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    nlohmann::json result;
    Base::Status s = debugger.read_registers(json_data, result);
    if (s.is_fail()) return s;
    else return Base::Status::success(result);
  });

  server.register_handler("write_registers", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    return debugger.write_registers(json_data);
  });

  server.register_handler("set_breakpoint", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    int type = json_data["type"];
    uint64_t address = json_data["address"];
    int breakpoint_id;
    return debugger.set_breakpoint(static_cast<Core::BreakpointType>(type), address, breakpoint_id);
  });

  server.register_handler("remove_breakpoint", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    int breakpoint_id = json_data["breakpoint_id"];
    return debugger.remove_breakpoint(breakpoint_id);
  });

  server.register_handler("enable_breakpoint", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    int breakpoint_id = json_data["breakpoint_id"];
    return debugger.enable_breakpoint(breakpoint_id);
  });

  server.register_handler("disable_breakpoint", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    int breakpoint_id = json_data["breakpoint_id"];
    return debugger.disable_breakpoint(breakpoint_id);
  });

  server.register_handler("get_breakpoints", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    if (json_data.contains("tid") && !json_data["tid"].is_null())
    {
      pid_t tid = json_data["tid"];
      std::vector<Core::Breakpoint> breakpoints;
      Base::Status s = debugger.get_breakpoints(tid, breakpoints);
      if (s.is_fail()) return s;
      else 
      {
        nlohmann::json result;
        for (const auto& bp : breakpoints) 
        {
          result.push_back({
            {"id", bp.id},
            {"tid", bp.tid},
            {"address", bp.address},
            {"type", static_cast<int>(bp.type)},
            {"enabled", bp.enabled}
          });
        }
        return Base::Status::success(result);
      }
    }
    else 
    {
      std::vector<Core::Breakpoint> breakpoints;
      Base::Status s = debugger.get_breakpoints(breakpoints);
      if (s.is_fail()) return s;
      else 
      {
        nlohmann::json result;
        for (const auto& bp : breakpoints) 
        {
          result.push_back({
            {"id", bp.id},
            {"tid", bp.tid},
            {"address", bp.address},
            {"type", static_cast<int>(bp.type)},
            {"enabled", bp.enabled}
          });
        }
        return Base::Status::success(result);
      }
    }
  });

  server.register_handler("get_breakpoint", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    if (json_data.contains("address") && !json_data["address"].is_null())
    {
      uint64_t address = json_data["address"];
      Core::Breakpoint breakpoint;
      Base::Status s = debugger.get_breakpoint(address, breakpoint);
      if (s.is_fail()) return s;
      else 
      {
        nlohmann::json result = {
          {"id", breakpoint.id},
          {"tid", breakpoint.tid},
          {"address", breakpoint.address},
          {"type", static_cast<int>(breakpoint.type)},
          {"enabled", breakpoint.enabled}
        };
        return Base::Status::success(result);
      }
    }
    else if (json_data.contains("breakpoint_id") && !json_data["breakpoint_id"].is_null())
    {
      int breakpoint_id = json_data["breakpoint_id"];
      Core::Breakpoint breakpoint;
      Base::Status s = debugger.get_breakpoint(breakpoint_id, breakpoint);
      if (s.is_fail()) return s;
      else 
      {
        nlohmann::json result = {
          {"id", breakpoint.id},
          {"tid", breakpoint.tid},
          {"address", breakpoint.address},
          {"type", static_cast<int>(breakpoint.type)},
          {"enabled", breakpoint.enabled}
        };
        return Base::Status::success(result);
      }
    }
    else return Base::Status::fail("参数错误");
  });

  server.register_handler("disassemble", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    uint64_t address = json_data["address"];
    size_t count = json_data["count"];
    std::vector<Assembly::Instruction> result;
    Base::Status s = debugger.disassemble(address, count, result);
    if (s.is_fail()) return s;
    else 
    {
      nlohmann::json j_result;
      for (const auto& insn : result) 
      {
        j_result.push_back({
          {"data", Utils::vec_to_str(insn.data)},
          {"mnemonic", insn.mnemonic},
          {"operands", insn.op_str}
        });
      }
      return Base::Status::success(j_result);
    }
  });

  server.register_handler("get_threads", [&debugger](const std::string& params) -> Base::Status
  {
    std::vector<pid_t> threads;
    Base::Status s = debugger.get_threads(threads);
    if (s.is_fail()) return s;
    else 
    {
      nlohmann::json result;
      for (const auto& tid : threads) 
      {
        result.push_back(tid);
      }
      return Base::Status::success(result);
    }
  });

  server.register_handler("switch_thread", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    pid_t tid = json_data["tid"];
    return debugger.switch_thread(tid);
  });

  server.register_handler("get_pid", [&debugger](const std::string& params) -> Base::Status
  {
    pid_t pid;
    Base::Status s = debugger.get_pid(pid);
    if (s.is_fail()) return s;
    else 
    {
      nlohmann::json result;
      result["pid"] = pid;
      return Base::Status::success(result);
    } 
    
  });

  server.register_handler("get_current_tid", [&debugger](const std::string& params) -> Base::Status
  {
    pid_t tid;
    Base::Status s = debugger.get_current_tid(tid);
    if (s.is_fail()) return s;
    else 
    {
      nlohmann::json result;
      result["tid"] = tid;
      return Base::Status::success(result);
    }
  });

}

int main()
{
  Core::DebuggerCore debugger;
  Base::RPCServer server;
  acp_init(server, debugger);
  server.start(5073);
  return 0;
}