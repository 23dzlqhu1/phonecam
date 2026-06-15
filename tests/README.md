# 测试中间件

> 🧪 这里是**独立于手机端和电脑端**的测试工具。
> 用来模拟、调试、验证 PCP 协议和端到端数据流。

---

## 目录结构

```
tests/
├── README.md              # 本文件
└── mock_phone/            # 模拟 Android 手机端
    └── mock_phone_server.py    # 用 Python 模拟手机发送视频
```

---

## 用途

| 文件夹 | 用途 | 何时用 |
|--------|------|--------|
| `mock_phone/` | 用 PC 模拟手机发送视频 | 开发电脑端时不需要真手机 |

---

## 快速开始

### 开发电脑端时没手机

```bash
# 启动 mock 手机
python tests/mock_phone/mock_phone_server.py

# 另一个终端，启动真的电脑端
python desktop/phonecam.py --connect 127.0.0.1:9999 --preview
```

---

## 开发规范

- 每个 mock 脚本独立可运行
- 在脚本顶部写明：用途、使用方法、依赖

---

**最后更新**：2026-06-10 (Pruned legacy test files)
