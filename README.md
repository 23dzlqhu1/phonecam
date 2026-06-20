# PhoneCam

> 把 Android 手机变成 Windows 电脑摄像头。

## 快速入口

| 我是 | 去看 |
|------|------|
| 普通用户 | [docs/current-status.md](docs/current-status.md) + [docs/user-manual.md](docs/user-manual.md) |
| 开发者 | [docs/current-architecture.md](docs/current-architecture.md) + [docs/protocol.md](docs/protocol.md) |
| AI 助手 | [Hermes.md](Hermes.md) + [docs/current-status.md](docs/current-status.md) |

`docs/current-status.md` 是当前事实锚点。其他文档如果和它冲突，以它为准。

## 当前状态

- 手机摄像头画面可以在本机 exe 中有效显示，大部分情况下主观没有明显延迟。
- 腾讯会议中可以看到并选择 PhoneCam 摄像头。
- 腾讯会议竖屏可显示手机画面。
- 腾讯会议横屏当前会显示 `Naoko` 占位图，这是已知问题。
- 音频、1080p60、稳定低于固定毫秒值的延迟、全新用户 3 分钟流程都不作为当前已验证能力宣传。

## 项目结构

| 目录 | 用途 |
|------|------|
| `docs/` | 当前状态、使用手册、架构、协议 |
| `phone_native/` | Android Kotlin 端：Camera2、MediaCodec、PCP v2、TCP 推流 |
| `cpp/` | 当前 PC 端主线：C++/Qt/FFmpeg/DirectShow，源码在 `cpp/src/`，本机 exe 位于 `cpp/build/phonecam.exe` |
| `scripts/` | 构建/安装脚本 |

## 文档原则

旧规划、旧联调记录、旧 AI 记忆库已经删除。以后新增或修改文档时，只保留能直接描述当前事实、当前使用方式或当前源码结构的内容。

## License

[MIT License](LICENSE) © 2026
