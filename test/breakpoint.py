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
        response = client.send_command("set_breakpoint", {"type": 1, "address": 0x12345678})
        print(f"服务器响应: {response}")
        response = client.send_command("get_breakpoints")
        print(f"服务器响应: {response}")
        response = client.send_command("remove_breakpoint", {"type": 1, "address": 0x12345678})
        print(f"服务器响应: {response}")
        response = client.send_command("get_breakpoints")
        print(f"服务器响应: {response}")
        
    finally:
        client.disconnect()

if __name__ == "__main__":
    main()