from rpc_client import RPCClient


def main():
    # 配置服务器地址
    SERVER_IP = '127.0.0.1'
    SERVER_PORT = 5073
    
    client = RPCClient(SERVER_IP, SERVER_PORT)
    
    if not client.connect():
        return
    
    try:
        response = client.send_command("attach", "com.example.andbgtest")
        print(f"服务器响应: {response}")
        
        # response = client.send_command("launch", "com.example.andbgtest/com.example.andbgtest.MainActivity")
        # print(f"服务器响应: {response}")

        # response = client.send_command("detach")
        # print(f"服务器响应: {response}")
        
        # response = client.send_command("kill")
        # print(f"服务器响应: {response}")
        
    finally:
        client.disconnect()

if __name__ == '__main__':
    main()