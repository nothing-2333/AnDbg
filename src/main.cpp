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
}

int main()
{
  Core::DebuggerCore debugger;
  Base::RPCServer server;
  acp_init(server, debugger);
  server.start();
  return 0;
}