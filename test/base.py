from rpc_client import RPCClient


def main():
    # 配置服务器地址
    SERVER_IP = '127.0.0.1'  
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