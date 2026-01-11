#include "rpc_server.hpp"
#include "debugger_core.hpp"


int main()
{
  DebuggerCore debugger;
  RPCServer server;

  
  server.start();

  return 0;
}