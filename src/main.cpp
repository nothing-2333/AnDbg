#include "rpc_server.hpp"
#include "debugger_core.hpp"
#include "log.hpp"
#include "status.hpp"
#include <string>
#include "utils.hpp"


void acp_init(Base::RPCServer& server, Core::DebuggerCore& debugger)
{
  server.register_handler("attach", [&debugger](std::vector<char>& params) -> std::vector<char> 
  {
    Base::Status status = debugger.attach(Utils::vec_to_str(params));

    if (status.is_success()) 
    {
      return Utils::str_to_vec("成功");
    }
    else 
    {
      return Utils::str_to_vec(status.to_string());
    }
  });

  server.register_handler("launch", [&debugger](std::vector<char>& params) -> std::vector<char> 
  {
    Base::Status status = debugger.launch(Utils::vec_to_str(params));

    if (status.is_success()) 
    {
      return Utils::str_to_vec("成功");
    }
    else 
    {
      return Utils::str_to_vec(status.to_string());
    }
  });

  server.register_handler("detach", [&debugger](std::vector<char>& params) -> std::vector<char> 
  {

    Base::Status status = debugger.detach();

    if (status.is_success()) 
    {
      return Utils::str_to_vec("成功");
    }
    else 
    {
      return Utils::str_to_vec(status.to_string());
    }
  });

  server.register_handler("kill", [&debugger](std::vector<char>& params) -> std::vector<char> 
  {

    Base::Status status = debugger.kill();

    if (status.is_success()) 
    {
      return Utils::str_to_vec("成功");
    }
    else 
    {
      return Utils::str_to_vec(status.to_string());
    }
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