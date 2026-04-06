from rpc_client import RPCClient
import argparse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-d", "--disassemble", action="store_true")
    parser.add_argument("-gt", "--get_threads", action="store_true")
    parser.add_argument("-s", "--switch_thread", action="store_true")
    parser.add_argument("-gp", "--get_pid", action="store_true")
    parser.add_argument("-gct", "--get_current_tid", action="store_true")

    args = parser.parse_args()
    
    # 配置服务器地址
    SERVER_IP = "127.0.0.1"
    SERVER_PORT = 5073
    
    client = RPCClient(SERVER_IP, SERVER_PORT)
    
    if not client.connect():
        return

    try:
        if args.get_threads:
            response = client.send_command("get_threads")

        elif args.switch_thread:
            response = client.send_command("switch_thread", {"tid": 1234})  

        elif args.get_pid:
            response = client.send_command("get_pid")

        elif args.get_current_tid:
            response = client.send_command("get_current_tid")

        else: 
            response = "请指定操作"

        print(f"服务器响应: {response}")
        
    finally:
        client.disconnect()

if __name__ == "__main__":
    main()