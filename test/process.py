from rpc_client import RPCClient
import argparse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-a", "--attach", action="store_true", help="附加进程: 包名")
    parser.add_argument("-l", "--launch", action="store_true", help="启动应用: 包名/Activity")
    parser.add_argument("-d", "--detach", action="store_true", help="分离进程")
    parser.add_argument("-k", "--kill", action="store_true", help="杀死进程")
    args = parser.parse_args()
    
    # 配置服务器地址
    SERVER_IP = "127.0.0.1"
    SERVER_PORT = 5073
    
    client = RPCClient(SERVER_IP, SERVER_PORT)
    
    if not client.connect():
        return

    try:
        if args.attach:
            response = client.send_command("attach", {"target": "com.example.andbgtest"})

        elif args.launch:
            response = client.send_command("launch", {"target": "com.example.andbgtest/com.example.andbgtest.MainActivity"})

        elif args.detach:
            response = client.send_command("detach")

        elif args.kill:
            response = client.send_command("kill")
            
        else: 
            response = "请指定操作: -a attach, -l launch, -d detach, -k kill"

        if response: print(f"服务器响应: {response}")
        
    finally:
        client.disconnect()

if __name__ == "__main__":
    main()