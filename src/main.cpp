#include "rpc_server.hpp"
#include "debugger_core.hpp"
#include "log.hpp"


void acp_init(RPCServer& server, DebuggerCore& debugger)
{
  server.register_handler("launch", [&debugger](std::vector<char>& params) -> std::vector<char> 
  {

    LaunchInfo launch_info(std::move("com.ss.android.ugc.aweme/.splash.SplashActivity"));
    bool ret = debugger.launch(launch_info);

    std::vector<char> result;
    if (ret) 
    {
      result.push_back(1); // 1 表示启动成功
    } else 
    {
      result.push_back(0); // 0 表示启动失败
    }
    return result;
  });
}

int main()
{
  DebuggerCore debugger;
  RPCServer server;
  acp_init(server, debugger);
  server.start();
  return 0;
}