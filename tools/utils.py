import subprocess
import sys


def execute_commands(commands: list[list[str]]):
  
  for cmd in commands:
    try:
      # 执行命令, 输出实时打印到终端, 统一编码为 utf-8
      subprocess.run(
        cmd,
        check=True,          # 命令返回非 0 值时抛出 CalledProcessError
        stdout=sys.stdout,   # 标准输出重定向到终端
        stderr=sys.stderr,   # 标准错误重定向到终端
        encoding="utf-8"     # 统一字符编码, 兼容多系统
      )
      print(f"✅ 命令执行成功: {' '.join(cmd)}")
    except Exception as e:
      # 捕获其他未知异常
      print(f"❌ 执行命令 {' '.join(cmd)} 时发生未知错误: {str(e)}")
      sys.exit(1)
  