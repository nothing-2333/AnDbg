from pathlib import Path
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools.utils import execute_commands

if __name__ == "__main__":
  # 测试根目录
  ROOT = Path("test/")
  
  # 初始化统计信息
  total_tests = 0
  passed_tests = 0
  failed_tests = []
  
  
  # 检查测试目录是否存在
  if not ROOT.exists():
    print(f"❌ 错误：测试目录 {ROOT}/ 不存在！")
    sys.exit(1)
  if not ROOT.is_dir():
    print(f"❌ 错误：{ROOT} 不是有效目录！")
    sys.exit(1)

  print(f"🚀 开始执行 {ROOT}/ 目录下所有以test结尾的Python测试文件\n")
  
  for test_file in ROOT.glob("**/*test.py"):
    total_tests += 1
    abs_test_file = str(test_file.absolute())
    print(f"━━━━ 执行测试文件 [{total_tests}]: {test_file.relative_to(ROOT)} ━━━━")

    # 执行测试文件，实时打印输出，捕获退出码
    execute_commands([[sys.executable, abs_test_file]])


    # 打印执行总结
    print("=" * 60)
    print(f"📊 测试执行完成 | 总计：{total_tests} | 成功：{passed_tests} | 失败：{len(failed_tests)}")
    if failed_tests:
        print(f"❌ 失败的测试文件：{', '.join(failed_tests)}")
    else:
        print("🎉 所有测试文件均执行成功！")
    print("=" * 60)

    # 脚本退出码：有失败则返回1，全成功返回0
    sys.exit(1 if failed_tests else 0)