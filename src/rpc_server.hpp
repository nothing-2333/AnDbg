#pragma once

#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>

struct Message
{
  std::string command;
  std::vector<char> content;
};

using Handler = std::function<std::vector<char>(std::vector<char>& content)>;

class RpcServer
{
private:
  bool running_{false};
  bool connected_{false};

  std::thread server_thread_;
  std::mutex mutex_;

  int server_fd_{-1};
  int current_client_fd_{-1};
  int port_{0};

  std::unordered_map<std::string, Handler> handlers_;
  
public:
  RpcServer();
  ~RpcServer();

  // 禁止拷贝
  RpcServer(const RpcServer&) = delete;
  RpcServer& operator=(const RpcServer&) = delete;

  // 启动服务器
  bool start(uint16_t port=5073);

  // 停止服务器
  void stop();

  // 注册命令处理函数
  void register_handler(const std::string& command, Handler handler);

  // 状态查询接口
  bool is_running();
  bool is_connected();
  int get_port();

private:
  // 服务器主循环
  void server_loop();

  // 处理客户端请求
  void handle_client(int client_fd);

  // 读取消息: 8 字节长度 + 数据
  std::vector<char> read_message(int client_fd);

  // 反序列化消息: (命令|参数)std::vector<char> -> Message
  Message deserialize_message(const std::vector<char>& data);

  // 系列化消息: Message -> std::vector<char>
  std::vector<char> serialize_message(const Message& message);

  // 发送消息: 8 字节长度 + 数据
  bool send_message(int client_fd, const std::vector<char>& data);

  // 安全关闭文件句柄
  void safe_close_fd(int& fd);
};