# AnDbg
一个 Android so 的调试器......

# 可能会用到的 adb 命令
```bash
# 转发端口
adb forward <电脑本地端口> <手机设备端口>
adb forward <tcp:本地端口> <tcp:手机端口>

# 查找 pid
adb shell pidof <包名>

# 查询当前前台运行 APP 的主 Activity 与包名
adb shell dumpsys window | grep -E "mCurrentFocus|mFocusedApp"
```