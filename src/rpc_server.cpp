#include <arpa/inet.h>
#include <cerrno>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unistd.h>
#include <vector>

#include "log.hpp"
#include "rpc_server.hpp"
#include "utils.hpp"


// // 64位主机序 → 网络序(大端序)
// static uint64_t htonll(uint64_t host64) 
// {
//   static const int endian = 1;
//   if (*reinterpret_cast<const char*>(&endian) == 1) 
//     return ((uint64_t)htonl((uint32_t)(host64 & 0xFFFFFFFF)) << 32) | htonl((uint32_t)(host64 >> 32));
//   else 
//     return host64;
// }

// // 64位网络序 → 主机序
// static uint64_t ntohll(uint64_t net64) 
// {
//   static const int endian = 1;
//   if (*reinterpret_cast<const char*>(&endian) == 1) 
//     return ((uint64_t)ntohl((uint32_t)(net64 & 0xFFFFFFFF)) << 32) | ntohl((uint32_t)(net64 >> 32));
//   else 
//     return net64;
// }

RPCServer::RPCServer()
{
  // 注册一些默认处理函数
  register_handler("ping", [](std::vector<char>& params) -> std::vector<char> 
  {
    if (params.empty()) 
    {
      std::vector<char> response = {'p', 'o', 'n', 'g'};
      return response;
    }
    else 
    {
      std::vector<char> response(params.begin(), params.end());
      return response;
    }
  });
}

RPCServer::~RPCServer() 
{
  stop();
}

bool RPCServer::is_running()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return running_;
}

bool RPCServer::is_connected()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

int RPCServer::get_port()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return port_;
}

void RPCServer::register_handler(const std::string& command, Handler handler)
{
  std::lock_guard<std::mutex> lock(mutex_);
  handlers_[command] = handler;
}

bool RPCServer::start(uint16_t port)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
    {
      LOG_DEBUG("rpc 服务已经启动");
      return true;
    }
  }

  // 创建 socket
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0)
  {
    LOG_ERROR("创建 socket 失败: {}", strerror(errno));
    return false;
  }

  // 设置 SO_REUSEADDR 选项
  int opt = 1;
  if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    LOG_ERROR("设置 SO_REUSEADDR 选项失败: %s\n", strerror(errno));
    safe_close_fd(server_fd_);
    return false;
  }

  // 绑定端口
  sockaddr_in server_addr{};
  server_addr.sin_family = AF_INET;           // ipv4
  server_addr.sin_addr.s_addr = INADDR_ANY;   // 监听所有接口
  server_addr.sin_port = htons(port);         // 指定端口
  if (bind(server_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
  {
    LOG_ERROR("绑定端口 {} 失败: {}", port, strerror(errno));
    safe_close_fd(server_fd_);
    return false;
  }

  // 监听
  if (listen(server_fd_, 1) < 0)
  {
    LOG_ERROR("监听端口 {} 失败: {}", port, strerror(errno));
    safe_close_fd(server_fd_);
    return false;
  }

  port_ = port;
  running_ = true;
  server_loop();
  // server_thread_ = std::thread(&RPCServer::server_loop, this);

  LOG_DEBUG("RPC 服务器启动, 端口 {}", port);
  return true;
}

void RPCServer::stop()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_)
      return;

    // 关闭服务器 socket, 以打断 accept 调用
    safe_close_fd(server_fd_);
    running_ = false;

    if (connected_)
    {
      safe_close_fd(current_client_fd_);
      connected_ = false;
    }
  }

  // 等待服务器线程退出
  if (server_thread_.joinable())
    server_thread_.join();

  LOG_DEBUG("RPC 服务器已停止");
}

void RPCServer::safe_close_fd(int& fd)
{
  if (fd >= 0)
  {
    close(fd);
    fd = -1;
  }
}

void RPCServer::server_loop()
{
  LOG_DEBUG("服务启动");

  while (true)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_)
        break;
    }

    // 接受客户端连接
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0)
    {
      if (errno == EINTR)
        continue; // 被信号中断, 继续接受连接

      if (running_) // 如果是因为停止服务器导致的 accept 失败, 则不打印错误日志
        LOG_ERROR("接受客户端连接失败: {}", strerror(errno));

      break;
    }

    // 如果有现有连接, 先关闭
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (connected_)
      {
        safe_close_fd(current_client_fd_);
        LOG_DEBUG("新链接顶替现有连接");
      }

      // 记录当前客户端连接
      current_client_fd_ = client_fd;
      connected_ = true;
    }


    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    LOG_DEBUG("客户端连接: {}:<{}>", client_ip, ntohs(client_addr.sin_port));

    // 处理客户端请求
    handle_client(client_fd);

    // 关闭客户端连接
    {
      std::lock_guard<std::mutex> lock(mutex_);
      safe_close_fd(current_client_fd_);
      connected_ = false;
    }
    LOG_DEBUG("客户端已断开连接");
  }

  LOG_DEBUG("服务关闭");
}

void RPCServer::handle_client(int client_fd)
{
  while (true)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_)
        break;
    }

    // 读取消息
    std::vector<char> data = read_message(client_fd);
    if (data.empty())
    {
      LOG_WARNING("读取消息为空, 读取失败或连接关闭");
      break;
    }

    // 反序列化消息
    Message message = deserialize_message(data);
    LOG_DEBUG("收到命令: {}", message.command);

    // 查找处理函数
    Handler handler = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = handlers_.find(message.command);
      if (it != handlers_.end())
      {
        handler = it->second;
      }
    }

    Message response;
    if (handler)
    {
      try 
      {
        // 调用处理函数
        response.content = handler(message.content);
        LOG_DEBUG("response.content.size {}", response.content.size());
        response.command = "success";
        LOG_DEBUG("命令 {} 处理完成", message.command);
      }
      catch (const std::exception& e) 
      {
        LOG_ERROR("处理命令 {} 时发生异常: {}", message.command, e.what());
        response.command = "error";
        const char* err_msg = "处理命令时发生异常";
        response.content = std::vector<char>(err_msg, err_msg + strlen(err_msg));
      }
    }
    else  
    {
      LOG_ERROR("未知命令: {}", message.command);
      response.command = "error";
      const char* err_msg = "未知命令";
      response.content = std::vector<char>(err_msg, err_msg + strlen(err_msg)); 
    }

    // 序列化响应消息
    std::vector<char> response_data = serialize_message(response);
    // 发送响应
    if (!send_message(client_fd, response_data))
    {
      LOG_ERROR("发送响应失败, 关闭连接");
      break;
    }
  }
}

std::vector<char> RPCServer::read_message(int client_fd)
{
  // 读取消息长度 (8 字节)
  uint64_t net_length;
  ssize_t n = recv(client_fd, &net_length, sizeof(net_length), MSG_WAITALL);
  if (n != sizeof(net_length))
  {
    if (n == 0)
      LOG_DEBUG("客户端关闭连接");
    else
      LOG_ERROR("读取消息长度失败: {}", strerror(errno));
    return {}; // 连接关闭或读取失败
  }

  uint64_t length = Utils::from_big_endian(net_length);
  if (length == 0)
    return {}; // 空消息

  // 读取消息内容
  std::vector<char> data(length);
  n = recv(client_fd, data.data(), length, MSG_WAITALL);
  if (n != static_cast<ssize_t>(length))
  {
    LOG_ERROR("读取消息内容失败");
    return {}; // 连接关闭或读取失败
  }

  return data;
}

bool RPCServer::send_message(int client_fd, const std::vector<char>& data)
{
  uint64_t length = static_cast<uint64_t>(data.size());
  uint64_t net_length = Utils::to_big_endian(length);

  // 构建响应: 8 字节长度 + 数据
  std::vector<char> response;
  response.resize(8 + length);

  memcpy(response.data(), &net_length, 8);
  memcpy(response.data() + 8, data.data(), length);

  // 发送消息长度
  ssize_t n = send(client_fd, response.data(), response.size(), 0);
  if (n != static_cast<ssize_t>(response.size()))
  {
    LOG_ERROR("发送消息失败: {}", strerror(errno));
    return false;
  }

  return true;
}

Message RPCServer::deserialize_message(const std::vector<char>& data)
{
  Message message;
  
  auto pos = std::find(data.begin(), data.end(), '|');
  if (pos == data.end())
  {
    LOG_ERROR("反序列化消息失败: 格式错误");
    return message;
  }

  message.command = std::string(data.begin(), pos);
  message.content = std::vector<char>(pos + 1, data.end());

  return message;
}

std::vector<char> RPCServer::serialize_message(const Message& message)
{
  std::vector<char> data;
  // 命令
  data.insert(data.end(), message.command.begin(), message.command.end());
  // 分隔符
  data.push_back('|');
  // 参数
  data.insert(data.end(), message.content.begin(), message.content.end());

  return data;
}