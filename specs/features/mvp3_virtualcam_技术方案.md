# MVP-3 虚拟摄像头 — 技术方案

**配套**: `specs/features/mvp3_virtualcam.md`
**作者**: Claude Code (架构师)
**日期**: 2026-06-09

---

## 1. 现有代码资产 (重要)

✅ **好消息**: 大部分骨架已存在, MVP-3 主要是**集成+补全**而非从零写。

| 文件 | 现状 | MVP-3 需要 |
|------|------|------------|
| [`desktop/virtual_camera.py`](file:///d:/PhoneCam/desktop/virtual_camera.py) | 127 行, 完整 VirtualCamera class + check_pyvirtualcam | 加 OBS 检测 / 平台检查 / 默认 1280x720@30 |
| [`desktop/phonecam.py`](file:///d:/PhoneCam/desktop/phonecam.py) | 已支持 `--virtual-cam` `--width --height --fps` | 改默认 1280x720@30 + 完善日志 |

**MVP-3 工作量评估**: < 1 天

---

## 2. 模块改动清单

### 2.1 virtual_camera.py 改造

**改 1**: `__init__` 默认参数改为 1280x720@30
```python
def __init__(self, width: int = 1280, height: int = 720, fps: int = 30):
```

**改 2**: `open()` 加 OBS 后端探测 + 平台检查
```python
def open(self) -> bool:
    # 平台检查: 仅 Windows
    if sys.platform != "win32":
        logger.warning(f"[MVP-3] 虚拟摄像头仅支持 Windows (DirectShow)。当前平台: {sys.platform}。已跳过。")
        return False

    try:
        import pyvirtualcam
    except ImportError:
        logger.error("[MVP-3] 缺少依赖: pyvirtualcam。请运行: pip install pyvirtualcam")
        return False

    try:
        # 不传 device 参数: 让 obs 后端用默认名 "OBS Virtual Camera"
        self._cam = pyvirtualcam.Camera(
            width=self.width, height=self.height, fps=self.fps,
            fmt=pyvirtualcam.PixelFormat.RGB,
            print_fps=False,  # 关闭自动 FPS 日志 (我们自己有)
        )
        self._is_open = True
        logger.info(f"[MVP-3] 虚拟摄像头已启动: 设备名={self._cam.device!r}, {self.width}x{self.height}@{self.fps}fps, 后端=obs")
        return True
    except RuntimeError as e:
        # OBS 后端不可用
        msg = str(e)
        if "obs" in msg and "OBS Virtual Camera" in msg:
            logger.warning("[MVP-3] 未检测到 OBS Virtual Camera。pyvirtualcam 后端不可用。下载: https://obsproject.com/")
        else:
            logger.warning(f"[MVP-3] 虚拟摄像头启动失败: {e}")
        return False
    except Exception as e:
        logger.warning(f"[MVP-3] 虚拟摄像头启动失败: {type(e).__name__}: {e}")
        return False
```

**改 3**: `device_name` property 用 `cam.device` (pyvirtualcam 提供的实际名, 可能是 "OBS Virtual Camera")

---

### 2.2 phonecam.py 改造

**改 1**: argparse 默认值改为 1280x720@30
```python
parser.add_argument("--width", type=int, default=1280, help="虚拟摄像头宽度 (MVP-3 默认 1280)")
parser.add_argument("--height", type=int, default=720, help="虚拟摄像头高度 (MVP-3 默认 720)")
parser.add_argument("--fps", type=int, default=30, help="虚拟摄像头帧率 (MVP-3 默认 30)")
```

**改 2**: `_run_cli()` 在连接成功后才启动 vcam, 失败时优雅降级 (AC-007)
```python
# 当前代码: vcam = VirtualCamera(...); vcam.open() -- 在 receiver.start() 之后
# 改进: 如果 vcam.open() 失败, 打印日志, 继续预览模式 (不强制退出)
```

**改 3**: AC-005 退出时, 虚拟摄像头要在 receiver.stop() 之后 close
```python
finally:
    receiver.stop()
    if vcam and vcam.is_open:
        vcam.close()  # 已有, 加日志
    cv2.destroyAllWindows()
```

---

## 3. 数据流 (Mermaid)

```mermaid
flowchart LR
    A[手机 Camera2] -->|YUV420| B[EglRenderer]
    B -->|EGL 零拷贝| C[H264Encoder]
    C -->|H.264 NALU| D[PCP 协议头]
    D -->|TCP 9998| E[PC PcpReceiver]
    E -->|H.264 NALU| F[H264Decoder]
    F -->|BGR ndarray| G{分发}
    G -->|cv2.imshow| H[OpenCV 预览窗口]
    G -->|BGR→RGB| I[pyvirtualcam]
    I -->|DirectShow| J[OBS Virtual Camera]
    J -->|DShow| K[腾讯会议/Zoom]
```

---

## 4. 异常处理矩阵

| 异常 | 行为 | 终端日志 | AC |
|------|------|---------|-----|
| pyvirtualcam 未装 | 跳过 vcam, 继续预览 | `[MVP-3] 缺少依赖: pyvirtualcam...` | AC-006 |
| OBS 未装 | 跳过 vcam, 继续预览 | `[MVP-3] 未检测到 OBS Virtual Camera...` | AC-007 |
| 非 Windows | 跳过 vcam, 继续预览 | `[MVP-3] 虚拟摄像头仅支持 Windows...` | AC-008 |
| 会议软件未启动 | 持续发送, 无报错 | (无) | AC-009 |
| 手机未连接 (30s) | 跳过 vcam, 退出 | `[MVP-3] 手机未连接, 跳过虚拟摄像头启动` | AC-010 |
| 推流中手机断开 | 冻结最后一帧, vcam 不断 | `[MVP-3] 推流中断, 虚拟摄像头持续发送最后一帧` | AC-011 |

**关键设计**: **所有异常都不阻塞纯预览模式**, 用户即使没装 OBS 也能看到手机画面。

---

## 5. AC → 实现 映射

| AC | 改动点 | 验证方法 |
|----|--------|---------|
| AC-001 | virtual_camera.py open() + 1280x720@30 默认 | 终端日志显示"已启动" |
| AC-002 | (无需代码改动, 系统层面) | 腾讯会议选 OBS Virtual Camera |
| AC-003 | phonecam.py 数据流分发 | 会议预览画面=手机画面 |
| AC-004 | phonecam.py 主循环 (已有) | 两个消费者同时工作 |
| AC-005 | phonecam.py finally (已有) | Ctrl+C 干净退出 |
| AC-006 | virtual_camera.py open() ImportError | 卸载 pyvirtualcam 跑 |
| AC-007 | virtual_camera.py open() RuntimeError | 卸载 OBS 跑 |
| AC-008 | virtual_camera.py open() 平台检查 | (MVP 不测, 单元测试覆盖) |
| AC-009 | (无需代码改动) | 启动 vcam 不开会议 |
| AC-010 | phonecam.py 30s deadline (已有) | 不连手机跑 |
| AC-011 | (无需代码改动) | 推流中拔 USB |
| AC-012 | 不传 device 参数 | 终端日志显示 "OBS Virtual Camera" |
| AC-013 | 默认 1280x720@30 | 终端日志 |
| AC-014 | send 前 BGR→RGB (已有) | 会议画面色彩正确 |

---

## 6. 测试计划 (真机联调)

### 6.1 单元测试 (mock)

```python
# tests/test_virtual_camera.py
def test_open_no_pyvirtualcam(monkeypatch):
    """AC-006: 缺 pyvirtualcam 不阻塞"""
    monkeypatch.setitem(sys.modules, 'pyvirtualcam', None)
    vcam = VirtualCamera()
    assert vcam.open() == False  # 优雅失败

def test_open_non_windows(monkeypatch):
    """AC-008: 非 Windows 跳过"""
    monkeypatch.setattr(sys, 'platform', 'darwin')
    vcam = VirtualCamera()
    assert vcam.open() == False
```

### 6.2 集成测试 (真机 + 相机 App)

| 步骤 | 期望 |
|------|------|
| 1. 启动手机推流 (MVP-2 链路) | 手机端 OK |
| 2. PC 跑 `python phonecam.py --connect 127.0.0.1:9999 --virtual-cam` | 终端 `[MVP-3] 虚拟摄像头已启动` |
| 3. Windows "相机" App (Win+K) | 能看到手机画面 |
| 4. Ctrl+C | 终端 `[MVP-3] 虚拟摄像头已关闭` |

### 6.3 端到端 (腾讯会议)

| 步骤 | 期望 |
|------|------|
| 1. PC 启 phonecam.py --virtual-cam | vcam 启动 |
| 2. 打开腾讯会议 → 设置 → 视频 | 下拉框有 "OBS Virtual Camera" |
| 3. 选 "OBS Virtual Camera" | 预览显示手机画面 |
| 4. 加入会议 | 主持人看到手机画面 |

---

## 7. 任务拆分 (下一步进入 task-planning)

| 任务 | 估时 | 依赖 |
|------|------|------|
| T-1: 改 virtual_camera.py 加 OBS/平台检查 + 默认 1280x720@30 | 0.5h | - |
| T-2: 改 phonecam.py argparse 默认值 + vcam 失败时优雅降级 | 0.5h | T-1 |
| T-3: 写单元测试 test_virtual_camera.py | 0.5h | T-1 |
| T-4: 装 OBS Studio + 真机联调 (相机 App 验证) | 1h | T-2 |
| T-5: 真机联调 (腾讯会议) | 1h | T-4 |
| T-6: 写联调记录 docs/03.3虚拟摄像头/mvp3_pyvirtualcam.md | 0.5h | T-5 |
| T-7: git commit + 更新 README | 0.5h | T-6 |
| **总计** | **4.5h** | |

---

## 8. 风险与回退

| 风险 | 应对 |
|------|------|
| OBS 没装好, 虚拟摄像头不出图 | mvp3 spec 已写明: 缺 OBS 不阻塞, 纯预览可用 |
| pyvirtualcam BGR/RGB 转换有问题 | 已有 cv2.cvtColor (验证过) |
| 腾讯会议不识别 OBS Virtual Camera | 用 "Windows 相机" App 兜底验证 (AC 仍算过, 腾讯会议兼容性属运营问题) |
| 虚拟摄像头延迟 > 200ms | 排查 BGR→RGB 转换, 必要时缩分辨率 |
| phonecam.py 加 vcam 后 OOM | vcam.send() 失败时 return False 不缓存, 已设计无 OOM |

---

**审核点**: 此技术方案是否可执行?有要调整的设计吗?
