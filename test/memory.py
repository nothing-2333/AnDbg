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
        response = client.send_command("attach", {"target": "com.example.andbgtest"})
        print(f"服务器响应: {response}")
      
        response = client.send_command("get_memory_regions")
        print(f"服务器响应: {response}")
        
        response = client.send_command("read_memory", {"address": 501575921664, "size": 16})
        print(f"服务器响应: {response}")
        
        response = client.send_command("write_memory", {"address": 501575921664, "data": [0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99]})
        print(f"服务器响应: {response}")
        
        response = client.send_command("read_memory", {"address": 501575921664, "size": 16})
        print(f"服务器响应: {response}")
        
        # response = client.send_command("allocate_memory", {"size": 0x10, "permissions": 0x7})
        # print(f"服务器响应: {response}")
        
        # result = response.split("|")
        # if len(result) == 2 and result[0] == "success":
        #     allocated_address = int(result[1], 16)
        #     print(f"分配的内存地址: 0x{allocated_address:x}")
        # else:
        #     print("allocate_memory 失败")
        
        # response = client.send_command("read_memory", {"address": allocated_address, "size": 16})
        # print(f"服务器响应: {response}")
        
        # response = client.send_command("write_memory", {"address": allocated_address, "data": [0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99]})
        # print(f"服务器响应: {response}")
        
        # response = client.send_command("read_memory", {"address": allocated_address, "size": 16})
        # print(f"服务器响应: {response}")
        
        # response = client.send_command("deallocate_memory", {"address": allocated_address})
        # print(f"服务器响应: {response}")
        
        response = client.send_command("detach")
        print(f"服务器响应: {response}")
        
        
    finally:
        client.disconnect()

if __name__ == "__main__":
    main()