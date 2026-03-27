#include "rpc_server.hpp"
#include "debugger_core.hpp"
#include "log.hpp"
#include "status.hpp"
#include <string>
#include "utils.hpp"


void acp_init(Base::RPCServer& server, Core::DebuggerCore& debugger)
{
  server.register_handler("attach", [&debugger](const std::string& params) -> Base::Status
  {
    return debugger.attach(params);
  });

  server.register_handler("launch", [&debugger](const std::string& params) -> Base::Status
  {
    return debugger.launch(params);
  });

  server.register_handler("detach", [&debugger](const std::string& params) -> Base::Status
  {
    return debugger.detach();
  });

  server.register_handler("kill", [&debugger](const std::string& params) -> Base::Status
  {
    return debugger.kill();
  });

  server.register_handler("read_registers", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    nlohmann::json result;
    Base::Status s = debugger.read_registers(json_data, result);
    if (s.is_fail()) return s;
    else return Base::Status::success(result.dump());
  });

  server.register_handler("write_registers", [&debugger](const std::string& params) -> Base::Status
  {
    nlohmann::json json_data = nlohmann::json::parse(params);
    return debugger.write_registers(json_data);
  });
}

int main()
{
  Core::DebuggerCore debugger;
  Base::RPCServer server;
  acp_init(server, debugger);
  server.start();
  return 0;
}