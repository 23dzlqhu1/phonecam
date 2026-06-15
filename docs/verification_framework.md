# PhoneCam 优化计划验收框架 v2 — 验证结果

## API 完整性验证（第 3 层）— 官方 API Diff 交叉验证

| API | 子代理声明 | 官方证据 | 实际结论 |
|-----|-----------|---------|---------|
| `KEY_LATENCY` ("latency", 编码器延迟) | "需要 API 30" ❌ | 不在 API 28→29 diff，也不在 29→30 diff → ≤ API 28 已存在 | **minSdk=24 可直接使用，无需守卫** |
| `KEY_LOW_LATENCY` ("low-latency", 解码器) | 子代理完全未提及 | API 29→30 diff 新增 | 解码器用，编码器不用 |
| `KEY_MAX_B_FRAMES` | "需要 API 29" ✅ | API 28→29 diff 新增 | **需要 `@RequiresApi(29)` 守卫** |
| `MediaCodec.setCallback(Callback, Handler)` | "API 23+" | 待查 | 待验证 |
| `CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES` | "API 21+" | 待查 | 待验证 |

## 子代理幻觉总结

五轮子代理审查中，所有子代理一致声称 `KEY_LATENCY` 需要 API 30。
**真相：`KEY_LATENCY` 在 API 28 之前就已存在，minSdk=24 可直接使用。**
子代理混淆了两个不同的常量名（`KEY_LATENCY` vs `KEY_LOW_LATENCY`）。

## 对计划的影响

计划中 Phase 2.2 的修正：
- ~~`Build.VERSION.SDK_INT >= Build.VERSION_CODES.R` → `KEY_LATENCY = 1`~~  ← 不需要守卫
- `KEY_LATENCY = 1` → 直接使用（minSdk=24 安全）✅
- `Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q` → `KEY_MAX_B_FRAMES = 0` → 保持不变 ✅
