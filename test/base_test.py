import socket
import struct


class RPCClient:
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None
    
    def connect(self):
        """连接到RPC服务器"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.sock.connect((self.host, self.port))
            print(f"已连接到服务器 {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"连接失败: {e}")
            return False
    
    def disconnect(self):
        """断开连接"""
        if self.sock:
            self.sock.close()
            self.sock = None
            print("已断开连接")
    
    def send_command(self, command, params=None):
        """
        发送命令到服务器并获取响应
        :param command: 命令字符串
        :param params: 参数字节列表(可选)
        :return: 服务器响应数据
        """
        if not self.sock:
            print("未连接到服务器")
            return None
        
        # 构造消息: 命令|参数
        msg = command.encode('utf-8') + b'|'
        if params:
            msg += bytes(params)
        
        # 发送消息: 8字节长度(网络字节序) + 消息内容
        length = len(msg)
        try:
            self.sock.sendall(struct.pack('!Q', length) + msg)
            
            # 接收响应长度(8字节)
            resp_len_data = self._recv_all(8)
            if not resp_len_data:
                print("接收响应长度失败")
                return None
            
            resp_len = struct.unpack('!Q', resp_len_data)[0]
            
            # 接收响应内容
            response = self._recv_all(resp_len)
            return response.decode('utf-8')
        except Exception as e:
            print(f"通信错误: {e}")
            return None
    
    def _recv_all(self, length):
        """接收指定长度的数据"""
        data = b''
        while len(data) < length:
            chunk = self.sock.recv(length - len(data))
            if not chunk:
                return None
            data += chunk
        return data

def main():
    # 配置服务器地址(替换为手机的实际IP)
    SERVER_IP = '127.0.0.1'  # 修改为你的手机IP
    SERVER_PORT = 5073
    
    client = RPCClient(SERVER_IP, SERVER_PORT)
    
    if not client.connect():
        return
    
    try:
        # 测试ping命令(无参数)
        print("\n测试ping命令(无参数):")
        response = client.send_command("ping")
        print(f"服务器响应: {response}")
        
        # 测试ping命令(带参数)
        print("\n测试ping命令(带参数):")
        response = client.send_command("ping", "hello".encode())
        print(f"服务器响应: {response}")
        
        # 测试未知命令
        print("\n测试未知命令:")
        response = client.send_command("unknown_command")
        print(f"服务器响应: {response}")
        
        # 测试大消息
        print("\n测试大消息:")
        large_data = bytearray([53 for i in range(5000)])
        response = client.send_command("ping", large_data)
        print(f"收到响应长度: {len(response) if response else 0}")
        
    finally:
        client.disconnect()

if __name__ == '__main__':
    main()