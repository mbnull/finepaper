# Finepaper 工作台 UI 系统

本文记录 NoC 创建工作台的稳定交互契约。具体颜色、间距和字体由
`src/ui/theme` 的系统调色板衍生 token 决定，业务页面不再自行维护色值。

## 设计上下文

- 平台：Linux 桌面 Qt Widgets，原生 Wayland 为发布前必测路径。
- 参考窗口：1480×920；最低常用逻辑窗口：1280×720。
- 缩放：至少覆盖 1.0 和 1.5；字体跟随系统设置，不固定字体族。
- 输入：鼠标与键盘同等可用。画布是主内容，选择和 Inspector 是次级内容，
  全局参数与诊断是按需内容。
- 主题：亮色、暗色和高对比系统调色板；Domain 不能只用颜色表达。

## 信息与操作层级

| 层级 | 内容 | 行为 |
|---|---|---|
| 主任务 | NoC Editor 画布 | 始终获得最大可用空间；默认 Pan，可切换 Select |
| 当前上下文 | Package Library、Inspector、Domain Manager | 主工具栏的文字 Panels 菜单打开并聚焦，View 菜单控制显隐 |
| 任务结果 | Diagnostics、Activity、Generation Output | 默认收起；校验、生成或错误出现时展开 |
| 低频任务 | Domain Configuration、Design Extensions、Performance、Problem Report | 作为中央工作区切换，不与画布同时挤占空间 |

没有设计时，画布显示唯一的下一步：有可运行 Package 时创建或打开设计；
没有 Package 时安装或打开已有设计。禁用按钮必须同时给出原因。

操作标签采用文字优先：主工具栏和 Panels 导航必须直接显示动作名称，不依赖
图标或 Tooltip 才能理解。图形标记只用于端口、Domain 色块、展开状态等空间或
数据语义。

## 视觉 token

所有常用 Widgets 使用以下语义，不依赖业务对象名：

- Surface：`surface`、`surfaceRaised`、`surfaceSunken`、`canvas`
- Boundary：`outline`、`outlineStrong`、两级 canvas grid
- Content：`text`、`mutedText`
- Interaction：`accent`、`accentHover`、`accentPressed`、`accentSubtle`
- Status：`success`、`warning`、`error`
- Component role：`primary`、`quiet`、`danger`、`card`、`title`、
  `subtitle`、`muted`、`canvasMode`

正文/背景和强调按钮前景/背景的对比度不得低于 4.5:1。新建或现代化后的
工作台布局使用 4、8、12、16、24、32 的语义间距序列；尚未迁移的旧页面逐步
收敛到相同 token。普通控件高度 36，紧凑控件高度 32；圆角为 6 或 10。

## 画布交互

| 操作 | 鼠标 | 键盘/替代路径 | 反馈 |
|---|---|---|---|
| 平移 | Pan 模式拖空白区 | `H` 进入 Pan | 手形光标，视口连续移动 |
| 框选 | Select 模式拖空白区；Pan 模式按 Shift 拖动 | `V` 进入 Select | 选择框和 Inspector 同步 |
| 创建 Endpoint | 从 Library 拖至画布或 Router | 过滤类型、选择 Router、Enter/显式 Add | 成功后选中新 Endpoint；失败显示具体原因 |
| 连接 Endpoint | 从 Endpoint attachment port 拖至 Router port/主体 | Inspector/Library 的显式动作作为替代路径 | 合法目标高亮；拒绝时保留原因 |
| 断开连接 | 连接线菜单 Disconnect | `Tab`/`Shift+Tab` 选择连接，`Delete` 断开；`Menu`/`Shift+F10` 打开菜单 | Endpoint 保留在自由位置 |
| 删除 Endpoint | Endpoint 菜单 Delete | `Tab`/`Shift+Tab` 选择 Endpoint，`Delete` 删除；`Menu`/`Shift+F10` 打开菜单 | 明确确认；Cancel 为默认 |
| 规则化布局 | Design 菜单 | `R` | 说明将清除自定义位置并确认 |
| 适应窗口 | 主工具栏 Fit | `F` | 不改变存储位置 |

Endpoint 的自由位置属于工作区状态，不得因为断连、重新选择或保存而强制
吸附 Router。断开的 Endpoint 是可恢复的编辑草稿，不属于持久 Design；重新连接
或删除以前，保存必须被阻止并明确说明原因。Router 与 Router 链路来自 Mesh，
不能作为任意图节点增删。

## Domain 表达

Domain layer 由 Package schema 和当前 design 数据驱动。每个画布标识必须同时
包含颜色以及纹理、线型或文本中的至少一种，确保灰度和常见色觉差异下仍能
区分。图例显示当前 layer、实例名称和对应标识；无有效 layer 时隐藏。

## 动效和危险操作

- 拖放高亮可使用短淡入淡出和脉冲，但必须遵守 Reduce Motion 设置；减少动画
  时使用静态边框/填充。
- 面板导航不使用动画作为唯一状态反馈。
- 删除、清除自定义布局等不可逆工作区操作先说明影响范围，Cancel 是默认和
  Escape 动作。后续引入 `QUndoStack` 后，确认框可按可撤销程度重新评估。

## 响应式与验证矩阵

| 逻辑窗口 | DPI | Palette | 必验状态 |
|---|---:|---|---|
| 1280×720 | 1.0、1.5 | Light、Dark | 空状态、Mesh、Router、Endpoint |
| 1480×920 | 1.0、1.5 | Light、Dark | 空状态、Mesh、Router、Endpoint、Domain |

每个组合至少验证：中央画布仍可操作、Inspector 无横向滚动、属性文字不重叠、
关键工具栏动作可见、焦点可辨识、窗口不被内容反向撑大。截图作为 CI artifact；
像素级 golden 只在 Qt、字体和平台固定时启用。

原生 Wayland 还必须覆盖普通窗口与最大化窗口，避免提交与 compositor 配置尺寸
不一致的 buffer。
