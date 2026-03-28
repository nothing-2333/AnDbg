# AnDbg
一个 Android so 的调试器......

## build
```bash

```

## 可能会用到的 adb 命令
```bash
# 转发端口
adb forward <tcp:本地端口> <tcp:手机端口>

# 查找 pid
adb shell pidof <包名>

# 查询当前前台运行 APP 的主 Activity 与包名
adb shell dumpsys window | grep -E "mCurrentFocus|mFocusedApp"
```

## 文件名命名规则
- 以 `Control` 结尾的类不允许保存私有对象, 只提供方法.