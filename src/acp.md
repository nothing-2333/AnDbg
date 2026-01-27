# AnDbg Communication Protocol

## 消息整体格式
```
8 字节长度 + 命令|参数
```

## 命令|参数
```json
{
  "lanuch": 
  [
    {
      "path": "string",
      "args": "array",
      "env": "array | null"
    },
    {
      "package_name": "string",
      "main_activity": "string",
    },
    {
      "android_target": "string(= package_name + main_activity)",
    }
  ],
  "attach": 
  [
    {
      "pid": "number",
    },
    {
      "package_name": "string",
    },
  ],

  "run": {},
  "detach": {},
  
  "step_into": 
  {
    "tid": "number | null",
  },
  "step_over":
  {
    "tid": "number | null",
  },
}

```