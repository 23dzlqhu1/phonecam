# 提示词：修 Bug

> 当用户报告 bug 或系统异常时，使用此模板。

---

## 第 1 步：理解问题

**问题描述**：[用户原话]

**复现步骤**：
1. ...
2. ...
3. 出错

**期望行为**：...

**实际行为**：...

## 第 2 步：定位根因

- 读相关文件（不要先猜，看代码）
- 用类比向用户解释"为什么出错"
  - ✅ 好："就像还没打开冰箱就想拿牛奶，我们试图在数据还没到时就读取"
  - ❌ 差："这是 null pointer exception"

## 第 3 步：提出修复

```python
# 修改前
def foo():
    return data['key']  # 报错：data 可能为空

# 修改后
def foo():
    if data is None:
        return default_value
    return data['key']
```

## 第 4 步：写测试

```python
def test_foo_with_none():
    assert foo(None) == default_value
```

## 第 5 步：手动验证清单

- [ ] 步骤 1：...
- [ ] 步骤 2：...
- [ ] 期望看到：[具体现象]

## 自我纠正限制

- 修一次不成功 → **立即停止**
- 不要再试第二次，直接报告给用户
- 不要陷入无限 retry 循环

---

**修复完成后**：
1. 更新 `.ai/context.md` 的"当前进度"
2. git commit: `fix(scope): 简短描述`
3. push
