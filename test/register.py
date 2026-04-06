import json
from rpc_client import RPCClient


def main():
    # 配置服务器地址
    SERVER_IP = '127.0.0.1'
    SERVER_PORT = 5073
    
    client = RPCClient(SERVER_IP, SERVER_PORT)
    
    if not client.connect():
        return
    
    try:
        r1 = {
            "GPR": ["x1", "x2", "pc"],
            "FPR": ["v1", "fpsr"]
        }
        response = client.send_command("read_registers", json.dumps(r1))
        print(f"服务器响应: {response}")
        
        # w1 = {
        #     "GPR": {
        #         "x1": "0xaabbccdd",
        #         "x2": "0x11223344"
        #     },
        #     "FPR": {
        #         "v1": "0xdeadbeef",
        #         "fpsr": "0x80000000"
        #     }
        # }
        
        # response = client.send_command("write_registers", json.dumps(w1))
        # print(f"服务器响应: {response}")
        
        # r2 = {
        #     "GPR": "all",
        #     "FPR": "all"
        # }
        
        # response = client.send_command("read_registers", json.dumps(r2))
        # print(f"服务器响应: {response}")
        

    finally:
        client.disconnect()

if __name__ == '__main__':
    main()