# RTS UnitPanel Architecture

## HUD 边界

小地图、UnitDetailPanel、RTSAvatar 和命令面板是独立的 HUD 块。UnitPanel 内部的编队条独立监听选择与控制组更新，不参与选择详情路由。

## 三个面板叠放

```text
UnitPanelFrame
└─ UnitPanelContentRoot
   ├─ UnitPanelHeaderBounds（固定编队条高度）
   │  └─ UnitFormationList
   └─ UnitPanelRouteBounds（固定 8 列 × 3 行完整槽位）
      └─ UnitPanelRoutes（Overlay）
         ├─ SingleUnitPanel：一个单位的详情
         ├─ IconContainer：一些单位，每个单位占一格
         └─ SummaryIconContainer：一群单位，每类图标与数量各占一格
```

三个面板覆盖同一块区域，按 `FRTSSelectionView.Mode` 互斥显示。单选采用垂直结构：顶部居中单位名称，中间为图标、属性与武器／护甲，底部居中显示编制和类别。右侧武器／护甲与研发区域叠放，只由正在执行的按钮的 `bIsResearch` 能力决定切换；单位类型和生产能力不参与这条判断。

| 选择状态 | SingleUnitPanel | IconContainer | SummaryIconContainer |
| --- | --- | --- | --- |
| Empty | Collapsed | Collapsed | Collapsed |
| Single | Visible | Collapsed | Collapsed |
| List | Collapsed | Visible | Collapsed |
| Summary | Collapsed | Collapsed | Visible |

无选择且无已编队单位时，整个 UnitPanel 使用 Hidden 保留占位；已存在编队时仍显示编队条。十个编队固定按 1–9、0 排列，有单位才显示，空编队 Hidden 而非 Collapsed。编队条两端对齐：按钮保持配置的固定宽度，第一个按钮左边与最后一个按钮右边对齐下方网格按钮边缘，剩余宽度均分到九个间隔。空编队仍保留完整槽位和间隔。各状态不因名称、选择数量或队列填充情况改变外壳尺寸。

## 尺寸来源

128 表示图标本体大小，不能作为含边框按钮的完整格子尺寸。UnitPanel 复用 `ControlGrid` 类默认对象的 `ButtonSize` 与 `SlotPadding`，按钮外尺寸包含其边框；槽位间距只计一次。

- `CellWidth = ButtonSize.X + SlotPadding.Left + SlotPadding.Right`
- `CellHeight = ButtonSize.Y + SlotPadding.Top + SlotPadding.Bottom`
- `ContentWidth = SelectionGridColumns * CellWidth`
- `ContentHeight = SelectionGridRows * CellHeight`
- `PanelWidth = ContentWidth + PanelPadding.Left + PanelPadding.Right`
- `PanelHeight = ContentHeight + HeaderHeight + PanelPadding.Top + PanelPadding.Bottom`

当前按钮外尺寸为 144×144，槽位四边各 4，所以一个完整槽位是 152×152；8×3 内容区是 1216×456。编队条预留 68，面板左右各 16、上下各 4，外壳为 1248×532。HUD 可以整体缩放；这些是缩放前的 UMG 布局单位。

配置允许增加行列数，最少为 8 列、3 行。模板子项数量、GridPanel 的旧 RowFill/ColumnFill、选中单位数均不决定面板尺寸。

## 格子与生产队列

列表和汇总使用独立对象池，每个格子按完整按钮外尺寸固定，再通过 GridSlot/UniformGridSlot 添加共享间距。图标本体居中，数量文字在固定格子内按需缩小，不能撑宽整列。未用格子的容器保留尺寸。

默认列表容量为 24 个单位；汇总容量为 12 类，每类占相邻两个格子。增加固定行列配置可提高容量；奇数列最后一格留空，图标与数量不跨行。

单选名称和底部编制／类别各占半行，中间恰好两行。中间左侧占两列：`UnitIdentityBounds → UnitIdentityContent（VerticalBox）`，其中头像在上，`InfoVerticalBox` 的血条、生命值和其他状态在下。删除原来把头像和生命信息横向拆开的 `UnitPortraitBounds`、`UnitStatsBounds` 及多余身份边框。头像保持比例，血条使用头像列宽，不随生命值文本长短改变；没有有效生命上限时同时隐藏血条和数值。

中间右侧占六列、两行。上排两个当前项目，每个项目一格按钮加两格进度，合计三格；下排恰好六个预备槽。当前按钮保持 144×144 的完整外尺寸，两格进度区域扣除左右各 4 的间距后宽 296。没有预备项目时隐藏整条预备队列，当前项目在右侧区域水平、垂直居中；一个项目占三格，两个项目占六格。出现预备项目后显示完整六槽，未使用的槽位显示空框。生产和通用研发的现有入队入口均拒绝超过六个预备项目的订单。

## 按钮的移动、复制和回调

命令网格的十五个固定容器只负责位置，`CommandButtonInstances` 保存原命令按钮实例。研发按钮具有两个独立属性：`bIsResearch` 表示使用研发表现，`bRepeatableResearch` 表示可重复执行。生产按钮在公共基类声明这两项能力，不能按具体单位另外判断。

- 一次性按钮从命令卡直接移出，并将同一个控件实例挂到研发容器。原位置保留空槽。
- 可重复按钮通过 `DuplicateObject` 复制控件实例和控件树，原按钮继续留在命令卡，副本挂到研发容器。
- 研发区把按钮的 `OnClicked` 从命令执行回调切换为取消回调。图标、名称、说明、费用等仍由原按钮定义提供，提示框复用命令网格原有实现。
- 点击取消通过原有 `RTSCommandProgressController` 处理权限、退费及队列推进。收到该实例已移除的状态后，一次性按钮移回命令卡并恢复原回调；副本移出容器并释放引用，交由 UObject 回收。
- 等待项目进入当前研发位置时，移动现有的队列按钮实例，不重新构造按钮。一次性项目正常完成后消耗原按钮，可重复项目完成后释放副本。

`ResearchButtons` 仅保存按现有 `FRTSTimedCommandInstance.InstanceId` 索引的控件引用，不维护另一份计时、排队或执行状态。固定的空槽框与真实研发按钮分离，禁止通过清空按钮数据、重填另一槽位来模拟按钮移动。国策菜单刷新也保留原按钮定义，不在每次刷新时重建定义对象。

## 验证

Tab 切组属于 `URTSSelector` 的既有输入处理器，直接调用 `URTSSelectionSubsystem::CycleGroup`，不随命令卡重建绑定。处理发生在 Slate 焦点导航之前，因此命令按钮、编队按钮或小地图获得键盘焦点后仍可切组。输入范围限于同一玩家的游戏视口；编辑器窗口、文字输入框、UI-only 模式和暂停状态交回原 UI 处理。长按 Tab 只切换一次，不继续触发 UI 焦点导航。

`Winyunq.Input.SelectionTabFocus` 使用独立的实际 LocalPlayer、游戏视口、命令按钮蓝图和 Slate 键盘事件，先复现按钮把 Tab 处理为焦点导航，再验证选择器接管后的切组。覆盖分组刷新保留当前组、命令卡子按钮替换后无需再次点击即可连续切组、长按只切一次，以及文字输入、UI-only 模式和视口外焦点不会被抢走 Tab。

循环回归包含四个组，跨 32 个编辑器帧逐次发送 Tab 并检查 `Alpha → Bravo → Charlie → Delta → Alpha`，每次都刷新选择视图。相机边缘滚屏复用原 `URTSCamera::executeEdgeScrollingEvaluation` 和移动入口，认可游戏视口内 HUD 的焦点，使用实时 Slate 鼠标位置，不依赖场景视口鼠标缓存。`RTSCamera - Edge Scroll Settings / distanceFromEdgeThreshold` 是各方向触发距离占视口尺寸的比例：`0.02` 为 2%，`0` 禁用；默认仍为 `0.1`。离开游戏视口、切到其他窗口或 UI-only 模式时不滚屏。测试使用渲染后的视口几何，检查四边、触发距离修改、静止光标持续移动与实际相机根组件位移；无原生窗口的测试仅替代前台状态，焦点和鼠标捕获仍走真实 Slate。

`Winyunq.UI.UnitPanelLayout` 使用真实详情、命令卡和按钮蓝图，通过 Slate 渲染读取实际几何，覆盖头像下方血条及生命比例、完整状态信息不溢出、无生命上限、空闲生产、研发能力切换、单／双当前项目、六槽队列、排空、满格列表及长数字汇总。测试还用原有生产提供者执行取消，检查一次性按钮的对象身份、移回后的命令模式、副本的控件树独立性，以及一个当前项目加六个预备项目的生产入队上限。渲染结果保存在 `Saved/UnitPanelArchitecture`。

`Winyunq.RTSTechnology.Research.BoundedQueue` 验证一个当前项目加六个预备项目后的请求被原入队入口拒绝。
