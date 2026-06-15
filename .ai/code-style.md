# 代码规范

> 💻 AI 写代码时请遵守这些规范。

---

## 通用原则

1. **可读性 > 简洁性**：新手也能看懂的代码 > 高手炫技的代码
2. **一次只做一件事**：每个函数、每个模块只负责一个职责
3. **错误早暴露**：不要吞异常，让问题尽早被发现
4. **测试驱动**：重要功能先写测试再写实现

---

## Python（电脑端）

### 命名
- 文件名：下划线命名法 `connection_manager.py`
- 类名：大驼峰 `ConnectionManager`
- 函数/变量：小写下划线 `start_server()`
- 常量：全大写 `MAX_PORT = 65535`
- 私有方法：前缀下划线 `_internal_helper()`

### 文件结构（每个 .py 文件）
```python
"""模块简短说明（一句话）"""
# 1. 标准库导入
import os
import sys

# 2. 第三方库导入
import cv2
import numpy as np

# 3. 本项目模块导入
from .receiver import VideoReceiver

# 4. 常量定义
DEFAULT_PORT = 8080

# 5. 类和函数
class PhoneCam: ...
```

### 中文注释规则
- **模块顶部**：3-5 行说明模块用途
- **类**：每个类 2-3 行说明
- **公共方法**：必须有 docstring
- **复杂逻辑**：行内注释解释"为什么"而不是"是什么"
- **TODO**：用 `# TODO: 描述` 标记待办

### 类型注解
```python
def start_server(port: int = 8080) -> bool:
    """启动服务
    
    Args:
        port: 监听端口
    Returns:
        是否启动成功
    """
    ...
```

---

## Kotlin（手机端 `phone_native/`）

> 📌 **当前手机端为 Kotlin 原生**（2026-06-08 路线重置 ADR-006）。旧 Dart/Flutter 工程 `phone/` 已 git rm 2026-06-09。
>
> 旧 Dart 风格保留在下文以作历史参考（新人 onboarding 切勿参考）。

### 命名
- 文件名：大驼峰 `MainActivity.kt` / 工具类大驼峰 `H264Encoder.kt`
- 类名：大驼峰 `CameraController`
- 变量/方法：小驼峰 `startPreview()` / `cameraW`
- 私有方法/变量：前缀下划线 `_internalState`（Kotlin 习惯用 `private val`/`private fun` 而非下划线）
- 常量：大写下划线 `TAG = "MainActivity"`
- 包名：小写 `com.phonecam.nativeapp`

### 导入顺序
```kotlin
// 1. Android SDK
import android.media.MediaCodec
import android.opengl.EGL14

// 2. Kotlin 标准库
import kotlin.concurrent.thread

// 3. 第三方（当前项目用得少）
// import com.example.thirdparty.X

// 4. 本项目相对路径
import com.phonecam.nativeapp.H264Encoder
```

### 类注释（KDoc 风格）
```kotlin
/**
 * Camera2 控制器，封装预览/拍照/ImageReader 监听。
 *
 * @param context Android Context
 * @param handler 相机后台线程 Handler（ImageReader 回调在此线程）
 * @see EglRenderer
 */
class CameraController(
    private val context: Context,
    private val handler: Handler
) {
    ...
}
```

### 风格要点
- **优先 `val` 而非 `var`**：不可变更安全
- **数据类用 `data class`**：`data class PcpHeader(val magic: Int, val version: Byte, val type: Byte, val length: Int)`
- **空安全**：用 `?` + `?:` 兜底，避免 `!!`
- **Lambda 简洁**：`{ it.width }` 而非 `{ img -> img.width }`（单参数时）
- **日志用 `InAppLogStore.i(TAG, msg)`**：UI 可见，便于现场排查（见 [.ai/gotchas.md G-014](../.ai/gotchas.md)）

---

## Dart/Flutter（手机端，已废）

> ⚠️ **历史章节**：MVP-0/1 时期旧手机端用 Flutter + Dart。2026-06-08 路线重置（ADR-006）后冻结，2026-06-09 `git rm -r phone/` 彻底删除。
>
> 新人请直接读上文 **Kotlin（手机端）** 章节。

### 命名（历史参考，仅供查 git 老代码）
- 文件名：小写下划线 `stream_server.dart`
- 类名：大驼峰 `StreamServer`
- 变量/方法：小驼峰 `startServer()`
- 私有方法/变量：前缀下划线 `_internalState`

### 导入顺序
```dart
// 1. Dart 标准库
import 'dart:async';
import 'dart:io';

// 2. Flutter 包
import 'package:flutter/material.dart';

// 3. 第三方包
import 'package:camera/camera.dart';

// 4. 本项目相对路径
import 'h264_encoder.dart';
```

---

## 提交规范（Conventional Commits）

### 格式
```
<type>(<scope>): <subject>

<body>

<footer>
```

### type 类型
| type | 用途 |
|------|------|
| feat | 新功能 |
| fix | 修 bug |
| docs | 文档变更 |
| style | 格式调整（不影响代码）|
| refactor | 重构（既不是新功能也不是修 bug）|
| test | 添加/修改测试 |
| chore | 构建/工具链变更 |

### 示例
```
feat(phone): 实现 H.264 关键帧请求回调

- 添加 onKeyframeRequest 回调
- 电脑端可以请求 IDR 帧以快速重连

Closes #12
```

---

## Git 工作流

- `master` 分支：稳定可发布版本
- `feature/xxx` 分支：新功能
- `fix/xxx` 分支：修 bug
- 合并前必须本地测试通过
- 每次 commit 尽量小（一个 commit 一个逻辑改动）

---

## 性能与安全

- 不要在主线程做阻塞操作（视频帧处理必须异步）
- 敏感信息（密钥、Token）一律用环境变量，不写进代码
- 公开 API 必须有错误处理
- 长时间运行的循环必须能优雅退出（监听信号）

---

**最后更新**：2026-06-07
