from rpc_client import RPCClient
import argparse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-r", "--resume", action="store_true")
    parser.add_argument("-si", "--step_into", action="store_true")
    parser.add_argument("-so", "--step_over", action="store_true")
    parser.add_argument("-p", "--pause", action="store_true")

    args = parser.parse_args()
    
    # 配置服务器地址
    SERVER_IP = "127.0.0.1"
    SERVER_PORT = 5073
    
    client = RPCClient(SERVER_IP, SERVER_PORT)
    
    if not client.connect():
        return

    try:
        if args.resume:
            response = client.send_command("resume")

        elif args.step_into:
            response = client.send_command("step_into")

        elif args.step_over:
            response = client.send_command("step_over")

        elif args.pause:
            response = client.send_command("pause")
            
        else: 
            response = "请指定操作: -r resume, -si step_into, -so step_over, -p pause"

        print(f"服务器响应: {response}")
        
    finally:
        client.disconnect()

if __name__ == "__main__":
    main()