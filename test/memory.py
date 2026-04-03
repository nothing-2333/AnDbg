from rpc_client import RPCClient
import argparse


def main():

    # 配置服务器地址
    SERVER_IP = "127.0.0.1"
    SERVER_PORT = 5073
    
    client = RPCClient(SERVER_IP, SERVER_PORT)
    
    if not client.connect():
        return

    try:
        response = client.send_command("launch", {"target": "com.example.andbgtest/com.example.andbgtest.MainActivity"})
        print(f"服务器响应: {response}")
      
        response = client.send_command("get_memory_regions")
        if response: print(f"服务器响应: {response}")
      
        response = client.send_command("allocate_memory", {"size": 0x10, "permissions": 0x7})
        if response: print(f"服务器响应: {response}")
        
        # response = client.send_command("write_memory", {"address": "0x12345678", "size": 16})
        # if response: print(f"服务器响应: {response}")
        
        
    finally:
        client.disconnect()

if __name__ == "__main__":
    main()