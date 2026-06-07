# 测试中间件

> 🧪 这里是**独立于手机端和电脑端**的测试工具。
> 用来模拟、调试、验证 PCP 协议和端到端数据流。

---

## 目录结构

```
tests/
├── README.md              # 本文件
├── mock_phone/            # 模拟 Android 手机端
│   └── mock_phone_server.py    # 用 Python 模拟手机发送视频
├── mock_pc/               # 模拟 Windows 电脑端
│   └── mock_pc_client.py       # 用 Python 模拟电脑接收视频
├── tools/                 # 性能/网络测试工具
│   ├── latency_tester.py
│   └── bandwidth_checker.py
└── legacy/                # 历史测试代码
    ├── test_vcam.py       # 早期虚拟摄像头验证脚本
    └── test_vcam2.py
```

---

## 用途

| 文件夹 | 用途 | 何时用 |
|--------|------|--------|
| `mock_phone/` | 用 PC 模拟手机发送视频 | 开发电脑端时不需要真手机 |
| `mock_pc/` | 用 PC 模拟电脑接收视频 | 开发手机端时不需要真电脑 |
| `tools/` | 性能测试、延迟测试、带宽测试 | 优化协议时 |
| `legacy/` | 历史代码，仅参考 | 维护时参考 |

---

## 快速开始

### 场景 1：开发电脑端时没手机

```bash
# 启动 mock 手机
python tests/mock_phone/mock_phone_server.py

# 另一个终端，启动真的电脑端
python desktop/phonecam.py --connect 127.0.0.1:9999 --preview
```

### 场景 2：测试协议延迟

```bash
python tests/tools/latency_tester.py
# 输出：平均延迟 X ms，最大 Y ms
```

---

## 开发规范

- 测试代码用 pytest 风格（虽然不一定需要 pytest 库）
- 每个 mock 脚本独立可运行
- 在脚本顶部写明：用途、使用方法、依赖

---

**最后更新**：2026-06-07
