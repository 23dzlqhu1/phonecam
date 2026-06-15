# 提示词：实现新功能

> 当用户要求新功能时，使用此模板。

---

## 第 1 步：澄清需求（关键！）

如果用户需求模糊，先用 `AskUserQuestion` 问清楚。

**问什么**：
- 解决什么问题？
- 成功标准是什么（怎么算做完）？
- 有没有偏好方案？

## 第 2 步：拆任务

把功能拆成 3-7 个**可独立验证**的小任务，每个任务不超过 1 天工作量。

示例（功能：手机端切换前后置摄像头）：

1. 写 UI 按钮
2. 调用 Camera2 API 切换
3. 通知电脑端（通过协议）
4. 写单元测试
5. 手动验证 + 录屏

## 第 3 步：写测试

TDD：先写测试 → 看测试失败 → 写实现 → 看测试通过

```python
# tests/test_switch_camera.py
def test_switch_to_front_camera():
    server = StreamServer()
    assert server.current_camera == "back"
    server.switch_to_front()
    assert server.current_camera == "front"
```

## 第 4 步：实现

**每完成一个任务**：
1. 解释做了什么（不要直接甩代码）
2. 标注文件：`📂 文件名：xxx | 📝 操作：xxx`
3. 写中文注释
4. 让用户手动验证

## 第 5 步：完成整个功能

- [ ] 所有子任务完成
- [ ] 单元测试通过
- [ ] 手动验证清单全部勾选
- [ ] 更新 `.ai/context.md` 进度
- [ ] 更新 `README.md` 用户文档
- [ ] git commit + push

---

## 关键原则

- **小步快跑**：一次只做一个任务
- **可验证**：每个任务都有明确的"做完了"标准
- **可回滚**：每个 commit 都可以 `git revert` 而不破坏系统
