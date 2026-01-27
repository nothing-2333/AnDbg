from .utils import execute_commands


if __name__ == "__main__":
  print("运行 update.py 脚本, 更新所有文件")
  
  cmake_commands = [
    ["cmake", "-B", "build/", "-S", "."],  # 配置构建目录
    ["cmake", "--build", "build/"],        # 执行编译
    ["adb", "push", "./build/bin/AnDbg", "/data/local/tmp"] # 通过 adb 推送到手机
  ]
  
  execute_commands(cmake_commands)
  