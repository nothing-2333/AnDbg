# AnDbg Communication Protocol

## 消息整体格式
```
8 字节长度 + 命令|参数
```

## 命令|参数
```json
{
  "lanuch": 
  {
    "path": "string",
    "args": "array",
    "env": "array | null"
  }
}

```