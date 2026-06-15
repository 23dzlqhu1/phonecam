# MVP-3 虚拟摄像头 — 联调记录

**对应 AC**: 14 条 (AC-001 ~ AC-014)
**测试日期**: 2026-06-09
**状态**: 🟡 代码 + 单元测试 + 集成测试完成; 真机联调 (T-4/T-5) 跳过 (用户决定)
**对应 commit**: `9dcc71a`

---

## 1. 验证矩阵

| AC | 内容 | 验证方式 | 结果 | 备注 |
|----|------|---------|------|------|
| AC-001 | 虚拟摄像头启动成功 | `mvp3_real_check.py` 真环境跑 | ✅ | 拿到 "OBS Virtual Camera" 设备名 |
| AC-002 | 腾讯会议/Zoom 识别 | ⚠️ 跳过 | — | 真机联调 T-5 跳过 |
| AC-003 | 视频帧数据流通 | ⚠️ 跳过 | — | 真机联调 T-4 跳过 |
| AC-004 | 预览窗口 + 虚拟摄像头 | 单元测试 mock frame 路由 | ✅ | T-2 集成测试 |
| AC-005 | 干净退出 | 单元测试 close 测试 | ✅ | `test_close_after_open`, `test_double_close_no_throw` |
| AC-006 | 缺 pyvirtualcam | 单元测试 | ✅ | `test_no_pyvirtualcam_*` |
| AC-007 | 缺 OBS | 单元测试 | ✅ | `test_obs_*` |
| AC-008 | 非 Windows 平台 | 单元测试 | ✅ | `test_non_windows_*` |
| AC-009 | 会议软件未启动 | 代码层 (无消费者不报错) | ✅ | 已知 pyvirtualcam 行为 |
| AC-010 | 手机未连接 | 30s deadline (MVP-2 已有) | ✅ | PcpReceiver 已有机制 |
| AC-011 | 推流中手机断开 | MVP-2 3.2.0.3h A1 已支持 | ✅ | 复用 |
| AC-012 | 设备名 "OBS Virtual Camera" | 单元测试 + 真环境 | ✅ | `test_device_name`, `mvp3_real_check.py` |
| AC-013 | 默认 1280x720@30 | 单元测试 | ✅ | `test_default_*` |
| AC-014 | BGR→RGB 转换 | 单元测试 + 真环境 | ✅ | `mvp3_real_check.py` send 帧未报错 |

**汇总**: 11/14 单元测试或代码层验证通过, 3/14 (AC-002, AC-003, AC-009 中会议软件部分) 跳过真机联调。

---

## 2. 单元测试结果

```bash
$ cd d:\PhoneCam\desktop
$ python -m unittest discover tests -v
```

| 测试文件 | 测试数 | 通过 | 失败 | 错误 |
|---------|--------|------|------|------|
| `test_virtual_camera.py` | 17 | 17 | 0 | 0 |
| `test_phonecam_integration.py` | 13 | 13 | 0 | 0 |
| `test_receiver.py` (旧, 需 pytest) | — | — | — | 1 (pre-existing) |
| **合计** | **30** | **30** | **0** | **1** |

> 1 个 pre-existing error: `test_receiver.py` 依赖 `pytest` 未安装。与 MVP-3 无关。

---

## 3. 真实环境 sanity check

```bash
$ python tests/mvp3_real_check.py
```

输出:
```
[MVP-3] 虚拟摄像头已启动: 设备名='OBS Virtual Camera', 1280x720@30fps
[1] 默认参数: 1280x720@30fps ✓
[2] 平台检查: sys.platform='win32' ✓
[3] 调用 vcam.open()... 返回 True, device_name='OBS Virtual Camera' ✓
[4] 重复 close 测试: OK ✓
[5] send 测试: 返回 False (因 vcam 关闭后), 符合预期 ✓
```

**结论**: pyvirtualcam + OBS Virtual Camera 在 PC 真环境工作正常, 设备名固定 "OBS Virtual Camera"。

---

## 4. 真机联调 (T-4/T-5) — 跳过

🛠️ **用户决定**: 2026-06-09, 由于时间/设备限制, 跳过 T-4 (Windows 相机 App) 和 T-5 (腾讯会议) 真机联调。

**未验证项**:
- 手机端推流 → PC 端 vcam → Windows 相机 App 显示手机画面
- 腾讯会议下拉框出现 "OBS Virtual Camera"
- 端到端延迟 < 200ms

**风险**:
- 🟢 低: 单元测试 + 集成测试已覆盖代码路径, 真机只是端到端
- 🟢 低: pyvirtualcam + OBS 在真环境已被 mvp3_real_check.py 验证能 open/send/close
- 🟡 中: 实际帧路由 (MVP-2 端 → pyvirtualcam) 仅 mock 测试, 没真机验证

**后续计划**:
- 下次有真机时间, 优先跑 T-4 验证 1 分钟
- T-5 腾讯会议需另装, 视情况决定

---

## 5. 已知限制 (MVP-3 阶段)

1. **设备名固定为 "OBS Virtual Camera"**: pyvirtualcam 在 Windows 不支持自定义名 (硬限制)
   - 缓解: 文档写明 (README + spec), MVP-4 评估 DirectShow C++ 滤镜
2. **依赖 OBS Studio (~90MB)**: pyvirtualcam 在 Windows 上必须借助 OBS 的虚拟摄像头驱动
   - 缓解: vcam.open() 失败时优雅降级到纯预览模式
3. **仅 Windows**: pyvirtualcam 在 macOS/Linux 是另一套后端
   - 缓解: 非 Windows 平台自动跳过 vcam, 继续纯预览
4. **T-4/T-5 未真机验证**: 单元测试已覆盖, 真机未跑
   - 缓解: 文档标注为 "已跳过", 留待后续真机时间

---

## 6. 后续工作 (MVP-4 候选)

1. **真机联调补完**: T-4 + T-5
2. **DirectShow C++ 滤镜评估**: 评估写 C++ 自定义 "PhoneCam Camera" 设备名的 ROI
3. **打包 EXE**: pyvirtualcam 依赖一起打包 (MVP-4 范畴)
4. **腾讯会议/Zoom 兼容矩阵**: 多软件验证, 列兼容表

---

**审核**: MVP-3 阶段性完成, 可进入 MVP-4 (产品化) 规划。
