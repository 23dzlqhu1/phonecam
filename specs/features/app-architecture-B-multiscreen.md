# PhoneCam App 多屏架构 (B 阶段)

> 🗺️ **本文档作用**：PhoneCam App 的**多屏架构** + **导航流**。
> viewfinder 单屏设计见 [`viewfinder-screen-A-layout.md`](viewfinder-screen-A-layout.md)。
> viewfinder 视觉风格见 [`viewfinder-screen-C-theme.md`](viewfinder-screen-C-theme.md)。
>
> **当前状态**：✅ **Phase X + Y 已实施**（2026-06-08，v0.2.5-mvp2-phaseY），详见 [`.ai/decisions.md`](../../../.ai/decisions.md) ADR-007 / ADR-008。
> OnboardingActivity 暂未实施（MVP-4 阶段再做），MainActivity 直接作启动器入口。

---

## 1. 选型记录

| 决定 | 选项 | 原因 |
|------|------|------|
| 多屏范围 | 5 屏全选 (设置/连接/关于/引导/调试) + viewfinder = 6 屏 | 用户确认 |
| 导航模式 | 多 Activity + 返回栈 | 零依赖，MainActivity 不被污染 |
| 资源策略 | 全新覆盖 (A 方案) | 设计规范 100% 命中，避免历史包袱 |

---

## 2. 全局屏幕地图

```
                    ┌────────────────────┐
                    │  启动器 (Launcher)  │
                    └────────┬───────────┘
                             │
                  ┌──────────▼──────────┐
                  │  OnboardingActivity │  ← 首次启动时
                  │   (3 步引导)         │     弹完即销毁
                  └──────────┬──────────┘
                             │ (Done 按钮)
                             │
                  ┌──────────▼──────────┐
                  │  MainActivity       │  ← 主屏 (viewfinder)
                  │   - TextureView     │     系统返回 = 退出 App
                  │   - 4 层布局         │
                  └──┬───┬───┬──────┬───┘
       ⚙ 齿轮 │  ●/○ 状态 │ 长按调试 │ │ 进入
              ▼          ▼          ▼
   ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
   │Settings      │ │Connect       │ │Debug         │
   │Activity      │ │Activity      │ │Activity      │
   │ (列表)        │ │ (QR + 输IP)  │ │ (Tab: 日志)  │
   └──────┬───────┘ └──────┬───────┘ └──────────────┘
          │ 关于           │ (返回)
          ▼
   ┌──────────────┐
   │About         │
   │Activity      │
   │ (滚动列表)    │
   └──────────────┘
```

---

## 3. 各屏职责 (一页一职责)

| # | Activity | 职责 | 入口 | 出口 |
|---|----------|------|------|------|
| 0 | **OnboardingActivity** | 首次启动 3 步教学 | 启动器 + SharedPreferences `first_run=false` | 完成后 → MainActivity, finish 自己 |
| 1 | **MainActivity** | viewfinder 4 层 (主屏) | 启动器 / 引导完成 | 系统返回 = 退出 App |
| 2 | **SettingsActivity** | 配置 (相机/推流/连接/调试) | viewfinder ⚙ | 系统返回 → MainActivity |
| 3 | **ConnectActivity** | PC 配对 (扫码/输IP/查看) | viewfinder ●/○ | 系统返回 → MainActivity |
| 4 | **AboutActivity** | 版本/许可证/联系 | Settings → "关于" 行 | 系统返回 → SettingsActivity |
| 5 | **DebugActivity** | 日志/PCP 包/连接质量 | viewfinder 长按调试区 | 系统返回 → MainActivity |

> ⚠️ **不在本次范围** (B 阶段列出但设计延后)：
> - 设置 → 相机/推流/连接/调试 的**子项详情页**（先全用单层列表，复杂时再拆二级）
> - 调试页内部的 Tab 切换（先只做 Logcat 一个 Tab）

---

## 4. 各屏布局速览

### 4.0 OnboardingActivity (首次启动)

```
┌─────────────────────────────────────┐
│  [跳过]  ← 右上角文字按钮              │
│                                     │
│         📷 大图标 (96dp)              │
│                                     │
│     "把手机变成电脑的高清摄像头"        │
│                                     │
│  ●────○────○   第 1 步 / 共 3 步     │
│                                     │
│   "1. 授予摄像头权限"                 │
│   "需要硬件访问, 我们不会录像"         │
│                                     │
│                                     │
│         [ 下一步 → ]                 │
└─────────────────────────────────────┘
```

- 3 张卡片，水平滑动 ViewPager 或单 Activity 切换 Fragment
- MVP 阶段**先做单 Activity + 3 个状态切换**（不引 ViewPager 依赖）
- "跳过" 在第 1 步可见，第 2/3 步隐藏

### 4.1 MainActivity (viewfinder 主屏)
- 详见 [`viewfinder-screen-A-layout.md`](viewfinder-screen-A-layout.md) + [`viewfinder-screen-C-theme.md`](viewfinder-screen-C-theme.md)
- 不在本设计稿重复

### 4.2 SettingsActivity (设置)

```
┌─────────────────────────────────────┐
│  ← 设置                              │ ← ActionBar 标题 + 返回箭头
├─────────────────────────────────────┤
│  📷 相机                              │
│    默认摄像头 ........... [后置 ▾]    │ ← 弹出底部选择 (后/前)
│    分辨率 ............... [720p ▾]    │ ← 弹 (480p/720p/1080p)
│    目标帧率 ............. [30 fps ▾] │
├─────────────────────────────────────┤
│  📡 推流                              │
│    码率 ................. [2 Mbps ▾] │
│    编码 .................. [H.264 ▾] │
├─────────────────────────────────────┤
│  🔗 连接                              │
│    传输方式 ............ [自动 ▾]     │ ← 弹 (USB 优先 / WiFi 优先 / 自动)
│    PC 发现 .............. [● 开]      │ ← Switch
├─────────────────────────────────────┤
│  🛠 调试                              │
│    显示调试信息 .......... [● 开]     │
├─────────────────────────────────────┤
│  ℹ️  关于                       >    │ ← 点击 → AboutActivity
├─────────────────────────────────────┤
│         PhoneCam v0.2.3              │ ← 底部版本号
└─────────────────────────────────────┘
```

- 使用 `ListView` 或 `LinearLayout` + 多组 `SettingRowView` 自定义控件
- 每行 = 左侧图标 + 中间标题 + 右侧当前值/开关
- 点击行 → 弹出 `BottomSheetDialog` 或 `AlertDialog` 选择

### 4.3 ConnectActivity (PC 连接)

```
┌─────────────────────────────────────┐
│  ← 连接 PC                           │
├─────────────────────────────────────┤
│  当前状态                              │
│    ┌─────────────────────────────┐  │
│    │   ●  未连接                   │  │ ← 大状态指示
│    │   等待 PC 端 PhoneCam 启动    │  │
│    └─────────────────────────────┘  │
│                                     │
│  ┌─ 方式 A: 扫码 ─────────────────┐ │
│  │                                 │ │
│  │   [QR 码  240×240]              │ │ ← PC 端显示二维码
│  │                                 │ │    手机扫码自动填 IP
│  │   192.168.1.10:7878             │ │
│  └─────────────────────────────────┘ │
│                                     │
│  ┌─ 方式 B: 手动输入 ──────────────┐ │
│  │   IP: [________]                │ │
│  │   端口: [7878]                  │ │
│  │   [    连接    ]                │ │
│  └─────────────────────────────────┘ │
│                                     │
│  已连接设备                            │
│    (空 - 尚无 PC 连接)                │
└─────────────────────────────────────┘
```

- 已连接后：顶部状态变绿 "● 已连接"，下方显示设备信息
- 两种方式二选一即可，不强制同时填

### 4.4 AboutActivity (关于)

```
┌─────────────────────────────────────┐
│  ← 关于                              │
├─────────────────────────────────────┤
│                                     │
│           📷  (大图标 96dp)          │
│         PhoneCam                    │
│         v0.2.3-mvp2-batch3-topfit  │
│                                     │
│  ─────────────────────────────────  │
│  作者     23dzlqhu1                  │
│  仓库     github.com/23dzlqhu1/...   │
│  许可     MIT                        │
│  ─────────────────────────────────  │
│  📜  开源许可证                  >    │ ← 子页 (本次不做)
│  💬  问题反馈                  >    │ ← 跳浏览器到 GitHub Issues
│  📖  使用文档                  >    │ ← 跳浏览器到 docs/
│  ─────────────────────────────────  │
│  © 2026 PhoneCam Contributors        │
└─────────────────────────────────────┘
```

- 纯展示页
- 点击外部链接用 `Intent.ACTION_VIEW`

### 4.5 DebugActivity (调试 / 铁粉用)

```
┌─────────────────────────────────────┐
│  ← 调试                              │
├─────────────────────────────────────┤
│  [日志]  [PCP]  [质量]               │ ← Tab (本次只实现 "日志")
├─────────────────────────────────────┤
│  19:18:01.234  I  camera opened    │
│  19:18:01.456  I  preview started  │
│  19:18:02.123  D  frame seq=42     │
│  19:18:02.345  I  bitrate=2.1Mbps  │
│  19:18:03.000  W  socket ping=80ms │
│  ...                                 │
│                                     │
├─────────────────────────────────────┤
│  [清空]  [分享]  [暂停自动滚动]      │
└─────────────────────────────────────┘
```

- 用 `RecyclerView` + 自适应行高 (长行折叠)
- 自动滚动到底部 (暂停时停)
- 顶 Tab 本次**只实现"日志"**，"PCP" / "质量" 显示"敬请期待"

---

## 5. AndroidManifest 声明

```xml
<application ...>
    <!-- 启动器入口: OnboardingActivity (启动后根据 first_run 跳转) -->
    <activity
        android:name=".OnboardingActivity"
        android:exported="true"
        android:theme="@style/AppTheme.Onboarding">
        <intent-filter>
            <action android:name="android.intent.action.MAIN" />
            <category android:name="android.intent.category.LAUNCHER" />
        </intent-filter>
    </activity>

    <!-- 主屏: 引导完成后跳转 -->
    <activity
        android:name=".MainActivity"
        android:exported="false"
        android:theme="@style/AppTheme.NoActionBar" />

    <!-- 设置 -->
    <activity
        android:name=".SettingsActivity"
        android:exported="false"
        android:theme="@style/AppTheme.WithActionBar"
        android:parentActivityName=".MainActivity" />

    <!-- 连接 PC -->
    <activity
        android:name=".ConnectActivity"
        android:exported="false"
        android:theme="@style/AppTheme.WithActionBar"
        android:parentActivityName=".MainActivity" />

    <!-- 关于 -->
    <activity
        android:name=".AboutActivity"
        android:exported="false"
        android:theme="@style/AppTheme.WithActionBar"
        android:parentActivityName=".SettingsActivity" />

    <!-- 调试 -->
    <activity
        android:name=".DebugActivity"
        android:exported="false"
        android:theme="@style/AppTheme.NoActionBar"
        android:parentActivityName=".MainActivity" />
</application>
```

### 5.1 主题变体

| 主题 | 父主题 | 用途 |
|------|--------|------|
| `AppTheme` | `Theme.Material3.Dark.NoActionBar` | 基础 |
| `AppTheme.Onboarding` | `AppTheme` + 引导页专用色 (可能更亮的品牌色背景) | 首次启动 |
| `AppTheme.NoActionBar` | `AppTheme` + `windowNoTitle=true` | MainActivity / DebugActivity |
| `AppTheme.WithActionBar` | `AppTheme` + Material ActionBar (深色) | Settings / Connect / About |

> ⚠️ 当前 `themes.xml` 还没分变体，B 阶段实现时一起建。

---

## 6. 导航流 AC (App 级别)

### 6.1 启动

**AC-APP-001** — 首次启动走引导
```
Given SharedPreferences "first_run" 标志为 false (默认) 或不存在
When 用户点击桌面图标
Then OnboardingActivity 启动, 显示第 1 步教学卡
And 标题栏右上角显示 "跳过" 按钮
```

**AC-APP-002** — 引导完成跳主屏
```
Given 用户在 OnboardingActivity 第 3 步
When 用户点击 "开始使用" 按钮
Then 写入 SharedPreferences "first_run" = true
And startActivity(MainActivity)
And finish() 销毁 OnboardingActivity
And 系统返回键无法回到引导 (已销毁)
```

**AC-APP-003** — 非首次启动直接进主屏
```
Given SharedPreferences "first_run" = true
When 用户点击桌面图标
Then OnboardingActivity 立即 startActivity(MainActivity) + finish()
And 视觉上看不到引导 (闪屏级别)
```

### 6.2 跳转

**AC-APP-004** — 齿轮进设置
```
Given 用户在 MainActivity
When 用户点击 Layer B 右侧 ⚙
Then startActivity(SettingsActivity)
And MainActivity 不销毁 (按返回能直接回)
And ActionBar 标题显示 "设置"
```

**AC-APP-005** — 状态点进连接
```
Given 用户在 MainActivity
When 用户点击 Layer B 左侧 ●/○ 区域
Then startActivity(ConnectActivity)
And MainActivity 不销毁
And 顶部状态区显示 "● 未连接" 或 "● 已连接 192.168.1.10"
```

**AC-APP-006** — 长按调试进调试
```
Given 用户在 MainActivity
When 用户长按 Layer B 中部调试区 ≥ 500ms
Then startActivity(DebugActivity)
And 不触发普通点击事件 (不会"不小心"进)
```

**AC-APP-007** — 设置进关于
```
Given 用户在 SettingsActivity
When 用户滚动到底部点击 "关于" 行
Then startActivity(AboutActivity)
And SettingsActivity 不销毁
```

### 6.3 返回

**AC-APP-008** — 系统返回栈
```
Given 用户从 Main → Settings → About
When 用户在 About 按系统返回
Then 回到 Settings (不是直接退出 App)
And 再按返回回到 Main
And 再按返回退出 App
```

**AC-APP-009** — MainActivity 返回 = 退出
```
Given 用户在 MainActivity
When 用户按系统返回
Then 弹出 "再按一次退出" Toast 提示 (1.5s 内再按才真退)
And 不直接 finish (避免误退)
```
> 🧠 这一条是体验细节, 防误触, 可选实现

### 6.4 跨屏状态同步

**AC-APP-010** — 设置改完同步主屏
```
Given 用户在 SettingsActivity 把 "默认摄像头" 改为前置
When 用户返回 MainActivity
And 当用户点击 Layer B 🔄 切换按钮
Then 打开的是前置摄像头 (不是后置)
And 设置被持久化到 SharedPreferences
```

**AC-APP-011** — 连接成功后状态同步
```
Given 用户在 ConnectActivity 点击 "连接" 并成功
When 用户返回 MainActivity
Then Layer B 左侧状态点变绿色, 显示 "已连接 192.168.1.10"
And 推流按钮从 "未连接" 灰色变 "可推流" 青色边
```

---

## 7. 范围 (Scope)

### B 阶段做
- ✅ 4 屏地图 (Main / Settings / Connect / About / Debug)
- ✅ 1 步启动 (MainActivity 启动时直接调 requestPermissions, 无独立引导页)
- ✅ 双击退出 (Toast 1.5s 窗口)
- ✅ Settings 单层列表 (弹窗选值, 不拆子页)
- ✅ 5 个 Activity 的清单 + Manifest 模板
- ✅ 3 套主题变体 (基础 / NoActionBar / WithActionBar)
- ✅ 各屏布局速览
- ✅ 11 条导航/状态同步 AC (含 AC-APP-009 双击退出)

### B 阶段不做
- ❌ 各屏详细控件设计 (设置项细节 / 连接页交互细节 → 后续阶段)
- ❌ 调试页 Tab 切换 (本次只做"日志"一个)
- ❌ 真实 QR 扫码 / mDNS 发现 (→ 批次 5)
- ❌ Fragment vs Activity 的子页拆分 (先用单层列表, 不够再拆)
- ❌ 多窗口 / 分屏支持
- ❌ 深色/浅色切换 (C 阶段已定: 强制深色)

---

## 8. 实现优先级建议 (进代码时按此顺序)

| 序 | 任务 | 风险 | 工作量 | 状态 |
|---|------|------|--------|------|
| 1 | C 阶段资源覆盖 (colors/themes/dimens) | 低 | 1h | ✅ Phase X-1 |
| 2 | 5 个空 Activity 壳 (只显示屏名) + Manifest 注册 | 低 | 1h | ✅ Phase X-4 |
| 3 | ~~OnboardingActivity (3 步卡片)~~ | ~~中~~ | ~~2h~~ | ⏳ **延后 MVP-4** |
| 4 | MainActivity 4 层布局落地 (应用 C 阶段配色) | 高 | 3h | ✅ Phase X-2 + X-3 |
| 5 | SettingsActivity 列表 + 弹窗 | 中 | 2h | ✅ Phase Y-1 |
| 6 | ConnectActivity QR + 输入框 (先不接 mDNS) | 中 | 2h | ✅ Phase Y-2（QR 暂未实施）|
| 7 | AboutActivity | 低 | 0.5h | ✅ Phase Y-3 |
| 8 | DebugActivity (只日志 Tab) | 中 | 1.5h | ✅ Phase Y-4 |
| 9 | 跨屏状态同步 (SharedPreferences + EventBus 或回调) | 中 | 1.5h | ✅ Phase Y-5 (SettingsStore 9+2 键) |
| 10 | 真机验证 + 截图 | 低 | 1h | ✅ Phase Y-6 (5 张 phaseY_*.png) |

**合计 ~15.5h** = 大约 2-3 个工作日 (按每天 5-6h 有效编码)
**实际完成**: Phase X + Y = ~13.5h（Onboarding 2h 跳过未做）

---

## 9. ✅ Phase X+Y 验收清单 (2026-06-08)

### 9.1 Phase X 资源 + 4 层布局 + Activity 壳

| AC | 描述 | 状态 |
|----|------|------|
| X-AC-1 | 资源齐备：colors.xml / dimens.xml / strings.xml + 4 个 activity_*.xml + themes.xml | ✅ |
| X-AC-2 | MainActivity 4 层布局（相机 50% / 状态 8% / 推流按钮 12% / 设置条 30%）| ✅ |
| X-AC-3 | Camera2 后置摄像头预览（30.76 FPS 稳定 logcat）| ✅ |
| X-AC-4 | 4 个 Activity 通过 Intent 跳转正常（Settings / Connect / Debug / About）| ✅ |
| X-AC-5 | AndroidManifest 5 个 Activity 注册 + 主题 + 父 Activity 配置 | ✅ |

### 9.2 Phase Y 4 屏完整 + 跨屏同步

| AC | 描述 | 状态 |
|----|------|------|
| Y-AC-1 | SettingsActivity 4 分区 9 项设置（默认摄像头 / 分辨率 / 目标帧率 / 码率 / 编码 / 传输方式 / PC 发现 / 显示调试信息 / 关于跳转）| ✅ |
| Y-AC-2 | SettingsActivity AlertDialog setSingleChoiceItems 弹窗选值 + SharedPreferences 持久化 | ✅ |
| Y-AC-3 | ConnectActivity IP/Port 输入 + 模拟连接（弹 Toast）+ lastIp/lastPort 自动回填 | ✅ |
| Y-AC-4 | AboutActivity 版本号 v0.2.5-mvp2-phaseY + 仓库 / 许可证 / 跳转行 | ✅ |
| Y-AC-5 | DebugActivity 实时日志（[InAppLogStore.kt](../../../phone_native/app/src/main/java/com/phonecam/nativeapp/InAppLogStore.kt) 500 行环形缓冲）+ 清空/复制/暂停滚动按钮 | ✅ |
| Y-AC-6 | SettingsStore.kt 9+2 键封装（9 个设置项 + 2 个 lastIp/lastPort）| ✅ |
| Y-AC-7 | 跨屏状态同步（主页推流按钮读 settings 实时更新，5 屏数据同步）| ✅ |
| Y-AC-8 | 推流按钮状态机（空闲 / 推流中 / 已暂停 / 错误 4 态）| ✅ |
| Y-AC-9 | 真机验收 5 张截图（[phaseY_*.png](../../../phone_native/phaseY_main.png)）| ✅ |
| Y-AC-10 | Camera2 + 权限申请 + 30.76 FPS 稳定预览 | ✅ |

### 9.3 关键决策

详见 [`.ai/decisions.md`](../../../.ai/decisions.md)：
- **ADR-007**：多 Activity 方案（vs Fragment / Compose）— 5 屏内零依赖最简方案
- **ADR-008**：推流按钮 UI 占位（"⏳ 推流功能待后续批次"）但状态机已完整，Phase Z 填逻辑

### 9.4 新增踩坑

详见 [`.ai/gotchas.md`](../../../.ai/gotchas.md)：
- G-013 SurfaceView vs TextureView 选型
- G-014 ConstraintLayout SurfaceView 事件拦截
- G-015 AlertDialog setSingleChoiceItems 参数顺序
- G-016 ADB 自动化点击靠 uiautomator dump 拿坐标
- G-017 PowerShell GBK 控制台打印 UTF-8 字符
- G-018 Android 4 层垂直布局比例

---

## 10. 待用户确认

1. **Onboarding 3 步卡**还是直接 "✓ 接受" 一个按钮？3 步更长教育，但用户说"从竞品来"的可能不需要
2. **AC-APP-009 双击退出**要不要做？防误触好但增加复杂度
3. **SettingsActivity 内的二级页面** (例如 "码率 → 高级") 本次要不要拆？建议**先不拆**，全放单层列表
4. **Phase Z 真推流链路**（Camera2 → MediaCodec → TCP）优先级：批次 3-5 实施

确认后 → 进入**实际代码实现**，按 §8 的优先级 1→10 推进。
