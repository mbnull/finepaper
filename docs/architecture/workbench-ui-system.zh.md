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
| 当前上下文 | Package Library、Inspector、Domain Manager | 主工具栏的文字 Panels 菜单打开并聚焦，View 菜单只控制响应式显示偏好 |
| 任务结果 | Diagnostics、Activity、Generation Output | 默认收起；校验、生成或错误出现时展开 |
| 低频任务 | Domain Configuration、Design Extensions、Performance、Problem Report | 作为中央工作区切换，不与画布同时挤占空间 |

没有设计时，画布显示唯一的下一步：有可运行 Package 时创建或打开设计；
没有 Package 时安装或打开已有设计。禁用按钮必须同时给出原因。

操作标签采用文字优先：主工具栏和 Panels 导航必须直接显示动作名称，不依赖
图标或 Tooltip 才能理解。图形标记只用于端口、Domain 色块、展开状态等空间或
数据语义。

面板命令分为两种不可混用的语义：View > Panels 的 checkable action 表示用户希望
该面板在布局空间允许时保持可见；窗口变窄造成的自动收起不得改写这个偏好。
`Go to …` 命令及 `Ctrl+B`、`Ctrl+Shift+B`、`Ctrl+Shift+D`、`Ctrl+J` 是非
checkable 的任务导航：退出 Canvas Focus、显示并抬起相应 Dock，再把键盘焦点送到
当前可继续操作的控件。面板重建后焦点目标必须即时求值，不能依赖 `findChild()`
对象名或缓存内部控件指针。被自动收起的 Dock 若仍持有焦点，应将焦点修复到当前
中央工作区。

Inspector 可以按当前状态显示文字任务入口，例如 `Edit Domain assignments` 和
`Review diagnostics`。这些入口与快捷键复用同一导航路径：前者切换到 Domain
Manager 的 assignment 页，后者切换到 Diagnostics；自动出现的结果仍不得主动抢
走画布焦点。

设计名、Endpoint ID、Package label、路径等外部文本必须按纯文本展示；确需富文本
的 Inspector 摘要先逐项 HTML escape。动态文案应一次性完成占位符替换或直接拼接，
不得在插入外部文本后继续链式 `QString::arg()`，因为合法的 `%2` 等内容不能被再次
解释为格式占位符。

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

## Inspector 草稿契约

工作台采用文字优先的交互语言：工具栏、面板入口、状态与恢复动作使用简短动词或
短语，不为已有文字重复添加装饰性图标。图标只适合空间关系或标准平台语义，且不能
单独承担状态、危险程度或下一步操作的表达。

NoC 参数、Endpoint 参数和 Package 定义的 Element Configuration 都区分
“持久 Design 值”与“表单中的未应用草稿”。选择其他节点、切换 Property Set、
刷新 Inspector 或重建画布时，必须保留完整编辑状态；`1e`、`-` 等暂时无效的
输入也属于草稿，不能只比较其 JSON 投影。

- 草稿按 design session 隔离；即使重新打开同一文件、Design ID 相同，也不得
  恢复上一个 session 的草稿。
- 权威 Design 值或 Package schema 改变后，旧草稿保留为只读冲突，并用文字说明
  原因；只有显式 Discard 才能清除。
- Save、Validate、Generate、New、Open、Reload、Install 和 Resize 必须汇总列出
  所有未应用 Inspector 草稿。Cancel 保留草稿，操作失败也保留；只有 Apply、
  显式 Discard 或实际开始/成功的后续操作才能消费相应草稿。
- Type、Migration 等会重建依赖参数字段的切换，若字段已修改，先提供文字化的
  “Discard Parameter Edits and Switch”与 Cancel 路径。

状态不能只靠颜色：Inspector 同时显示 `Unapplied`、`Draft conflict`、
`Discard Unapplied Changes` 等可读文案。
Domain assignment 与 Domain Configuration 草稿遵守同一工作台契约：存在任一
未应用草稿时窗口必须显示 modified 状态，并启用 Save 以进入统一处理流程。

## 结果归属与过期状态

Validation 和 Generation 启动时捕获 design session、语义 revision、Package
catalog revision 与唯一 run ID。完成回调只有在完整票据仍匹配当前工作台时才能
更新 DRC、Problem Report、Artifacts、结果 Tab、状态栏或焦点；旧任务只记录到
Activity Log，不得覆盖新结果或切走主画布。

结果页始终用文字标明以下状态之一：

- `Running`：正在处理的 Design 名称与 revision；
- `Current result` / `Current artifacts`：结果属于当前 revision；
- `Out of date`：Design 已发生持久语义修改，或 Package catalog 已更新；旧内容
  可以继续查看，但交付前必须重新运行；
- `No diagnostics` / `No RTL`：新 session 尚无结果。

Apply 参数、Endpoint/Domain/Element 修改和 Mesh 变化增加语义 revision；Save、
画布平移、自由节点位置、Router 折叠和 Fit View 不增加 revision。Validation 失败
展开 Results dock，但不自动切换中央工作区，避免打断画布上下文。

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
