# Viewfinder 屏视觉风格 (C 阶段)

> 🎨 **本文档作用**：PhoneCam viewfinder 屏的**视觉风格**规范。
> 布局 (A 阶段) 见 [`viewfinder-screen-A-layout.md`](viewfinder-screen-A-layout.md)。
> 多屏架构 (B 阶段) 见 [`app-architecture-B-multiscreen.md`](app-architecture-B-multiscreen.md)。
> **当前状态**：C 阶段，待用户确认。

---

## 1. 选型记录

| 决定 | 选项 | 原因 |
|------|------|------|
| 资源策略 | A. 全新覆盖 colors/themes/dimens | 设计规范 100% 命中，避免历史包袱 |
| 深色 / 浅色 | 强制深色 | 避免相机画面反光 / 保持品牌一致 |
| 主色调 | 电光青 (Electric Cyan) #00E5FF | 像示波器波形 / 5G 信号灯，**科技 + 视频信号**双重隐喻 |
| 主字体 | Roboto Flex (sans-serif) | 现代、可变字体、清晰 |
| 数字字体 | JetBrains Mono | 等宽、有"测量仪器感"、0/O 一眼分得开 |

---

## 2. 设计哲学 (7 条)

1. **像示波器，不像 Excel** —— 调试行是技术人员的"心电图"，要看得舒服
2. **像录音棚设备，不像消费电子** —— 暗色 + 单一强调色，模仿 RØDE / Blackmagic 调性
3. **像航拍遥控器，不像电视遥控器** —— 物理感 (圆角、阴影)，但又数字化 (脉冲、glow)
4. **距离也能读** —— 用户可能手机架在三脚架上 2 米外看，按钮要大、状态要亮
5. **数字要"有性格"** —— 0 和 O 必须一眼分得开 (用 JetBrains Mono)
6. **颜色要"会说话"** —— 灰=空闲 / 青=工作 / 绿=成功 / 橙=警告 / 红=危险
7. **永远深色** —— 强制 dark theme，不给浅色选项

---

## 3. 调色板 (10 色)

| 名称 | 颜色 | 用途 |
|------|------|------|
| `bg_oled` | `#0A0A0F` | App 背景 (OLED 黑, 非纯黑) |
| `surface_1` | `#1A1A22` | 卡片 / 按钮背景 |
| `surface_glass` | `rgba(10,10,15,0.6)` | Layer B 玻璃感 (可加 blur) |
| `hairline` | `rgba(255,255,255,0.08)` | 1px 边线 |
| `text_primary` | `#E8E8F0` | 主要文字 |
| `text_secondary` | `rgba(255,255,255,0.6)` | 调试 / 时间 |
| `text_disabled` | `rgba(255,255,255,0.38)` | 占位 / 禁用 |
| `brand_cyan` | `#00E5FF` | 推流指示 / 主按钮 / 高亮 |
| `led_green` | `#00E676` | PC 已连接 / 推流成功 |
| `warn_amber` | `#FFB300` | 推流中断 / 重连中 |
| `danger_red` | `#FF3D5A` | 停止按钮 / 错误 |

---

## 4. 字体

| 用途 | 字体 | 字号 | 字重 | 颜色 |
|------|------|------|------|------|
| Layer B 状态文字 | Roboto Flex | 14sp | 500 | text_primary |
| Layer B 调试数字 | **JetBrains Mono** | 13sp | 500 | brand_cyan |
| Layer C 按钮文字 | Roboto Flex | 20sp | 600 | 白 |
| Layer D 状态 | JetBrains Mono | 12sp | 400 | text_secondary |
| 错误文字 | Roboto Flex | 15sp | 500 | text_primary |

> 💡 JetBrains Mono 需要打包 .ttf 到 `app/src/main/assets/fonts/`。MVP 阶段先用 Android 系统 mono (`typeface="monospace"`) 占位。

---

## 5. 圆角 / 间距 / 动效

| 元素 | 圆角 | 间距 |
|------|------|------|
| Layer C 推流按钮 | 16dp (pill 形) | 上下 16dp 内边距 |
| Layer B 状态行 | 0 (通栏) | 左右 16dp 内边距 |
| 状态圆点 | 圆形 | - |
| 错误占位图标 | 0 | 居中 |

| 元素 | 动效 | 时长 / 缓动 |
|------|------|------------|
| 推流按钮按下 | scale 1.0 → 0.95 | 200ms ease-out |
| 推流按钮释放 | scale 0.95 → 1.0 | 150ms ease-in |
| 推流指示点 | opacity 0.3 ↔ 1.0 | 1.0s ease-in-out (循环) |
| Layer B 状态切换 | 颜色 cross-fade | 300ms |
| 错误占位淡入 | opacity 0 → 1 | 250ms ease-out |

---

## 6. 4 层配色应用

### Layer A — 相机画面
- 背景：#0A0A0F (letterbox 填这个色)
- 异常占位：
  - 背景：rgba(10,10,15,0.95) 蒙在 TextureView 上
  - 图标：64dp, 警告用 #FFB300，权限用 #E8E8F0
  - 文字：15sp 500, #E8E8F0
  - "重新申请" 按钮：实心 #00E5FF, 文字 #0A0A0F

### Layer B — 状态 + 设置行
- 背景：rgba(10,10,15,0.6) + backdropBlur(20dp)
- 高度：80dp
- 顶/底 1px hairline
- 左侧状态点：8dp 圆形, 发光
  - 空闲：#E8E8F0 30% alpha
  - 已连接：#00E676 + 8dp 外发光
  - 重连中：#FFB300 + 1Hz 闪烁
- 中部调试：JetBrains Mono 13sp, #00E5FF
- 右侧图标：⚙ 🔄 24dp outline, #E8E8F0 80% alpha, 按下变 100%

### Layer C — 推流按钮
- 尺寸：宽 60% 屏 × 高 64dp, 圆角 16dp (pill)
- 默认态：背景 #1A1A22, 边 1px #00E5FF, 文字 "▶ 开始推流" #E8E8F0
- 推流中态：背景 #FF3D5A, 文字 "■ 停止推流" #FFFFFF, 阴影 0 4dp 16dp rgba(255,61,90,0.4)
- 推流中断态：背景 #FFB300, 文字 "推流中断" #0A0A0F
- 不可用态：背景 rgba(26,26,34,0.5), 边 1px hairline, 文字 "请先打开摄像头" text_disabled
- 推流指示点 (按钮上方 8dp)：6dp 圆形 #00E5FF + 外圈 16dp #00E5FF 30% alpha, 1Hz opacity 脉冲

### Layer D — 底部状态文字
- 高度：40dp
- 背景：透明
- 顶 1px hairline
- 文字：JetBrains Mono 12sp, 格式 `PHONECAM v0.2.3 · CAM0 · 00:01:23`
- 颜色：text_secondary, 时间部分用 text_primary

---

## 7. 三个"铁粉"细节

1. **推流指示点** —— 远处看手机就知道在不在推。**0.3 → 1.0 opacity 循环**。
2. **推流按钮的红色微光阴影** —— 设备"在工作"的物理感。`elevation=8, ambientShadowColor=#FF3D5A 30%`。
3. **调试数字用 mono 字体 + 电光青** —— 像示波器上的数据，是"工具"不是"App"。

---

## 8. 实现资源 (Kotlin/XML)

```xml
<!-- res/values/colors.xml -->
<resources>
    <color name="bg_oled">#0A0A0F</color>
    <color name="surface_1">#1A1A22</color>
    <color name="text_primary">#E8E8F0</color>
    <color name="text_secondary">#99FFFFFF</color>
    <color name="text_disabled">#61FFFFFF</color>
    <color name="brand_cyan">#00E5FF</color>
    <color name="led_green">#00E676</color>
    <color name="warn_amber">#FFB300</color>
    <color name="danger_red">#FF3D5A</color>
    <color name="hairline">#14FFFFFF</color>
</resources>
```

```xml
<!-- res/values/themes.xml -->
<style name="AppTheme" parent="Theme.Material3.Dark.NoActionBar">
    <item name="android:windowBackground">@color/bg_oled</item>
    <item name="android:statusBarColor">@android:color/transparent</item>
    <item name="android:navigationBarColor">@android:color/transparent</item>
    <item name="colorPrimary">@color/brand_cyan</item>
    <item name="colorOnPrimary">@color/bg_oled</item>
    <item name="colorError">@color/danger_red</item>
</style>
```

> 全部资源定义在 `colors.xml` / `themes.xml` / `dimens.xml`，**禁止在 Kotlin 里硬编码颜色**。

---

## 9. 验收 AC (视觉部分)

**AC-VF-101** — 深色主题
```
Given App 启动
When 任何时刻
Then 整个 App 背景为 #0A0A0F (非纯黑)
And 无任何浅色面板 (强制 dark theme)
```

**AC-VF-102** — 调试数字等宽字体
```
Given Layer B 调试行显示
When FPS / 分辨率 / 码率更新
Then 数字使用 JetBrains Mono (或 Android system mono)
And 数字颜色为 #00E5FF
And 数字与数字之间等宽 (像 "30" 和 "60" 占同样宽度)
```

**AC-VF-103** — 推流指示点脉冲
```
Given 用户点击开始推流
When 推流稳定运行
Then 按钮上方 8dp 处出现 6dp 圆形 #00E5FF
And 外圈 16dp 30% alpha 以 1Hz 循环 opacity 0.3 ↔ 1.0
And 停止推流后指示点消失
```

**AC-VF-104** — 按钮微动效
```
Given 推流按钮可点击
When 用户按下
Then 按钮在 200ms 内 scale 1.0 → 0.95 (ease-out)
When 用户释放
Then 按钮在 150ms 内 scale 0.95 → 1.0 (ease-in)
```

**AC-VF-105** — 状态点发光
```
Given PC 已连接
When 任何时刻
Then Layer B 左侧 8dp 圆点为 #00E676
And 圆点周围有 4dp 模糊外发光 (同样 #00E676, 30% alpha)
```

**AC-VF-106** — 状态颜色语义
```
Given App 运行
When 任何状态切换
Then 状态颜色严格遵循: 灰=空闲 / 青=工作 / 绿=成功 / 橙=警告 / 红=危险
And 不出现混用 (例如: "推流中"不用绿色而用电光青)
```

**AC-VF-107** — 错误占位样式
```
Given 权限被拒 / 摄像头被占用
When 异常状态显示
Then Layer A 中央显示 64dp 图标 + 15sp 500 文字
And 图标颜色: 权限=#E8E8F0, 占用=#FFB300
And 整体在 250ms 内淡入
```

---

## 10. 范围 (Scope)

### C 阶段做
- ✅ 调色板 (10 种颜色)
- ✅ 字体 (2 套: sans + mono)
- ✅ 圆角 / 间距 / 动效规范
- ✅ 4 层配色具体到每个元素
- ✅ 资源文件示例 (colors.xml / themes.xml)
- ✅ 7 条视觉 AC

### C 阶段不做
- ❌ 多屏导航 (→ B 阶段)
- ❌ 设置页 / 连接页的具体控件 (→ B 阶段)
- ❌ 真实动效代码实现 (本阶段只出规范)
- ❌ 启动屏 / 关于页设计 (→ B 阶段)
