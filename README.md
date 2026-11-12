# kAzeware

**有全部功能的 CS2 HvH 菜单**

[下载 alpha-0.0.2](https://github.com/KaguraWang/kAzeware/releases/tag/alpha-0.0.2) · [浏览源码](kAzeware/source) · [Apache 2.0](LICENSE)

![kAzeware alpha-0.0.2 实机总预览](docs/images/overview.png)

淡粉色孟菲斯拼贴界面：贴纸卡片、和纸胶带、爱心控件和滑动胶囊导航。常用设置直接铺开，细节放进齿轮弹窗；菜单与屏幕 widgets 使用统一的视觉风格。

## 功能一览

| 页面 | 内容 |
| --- | --- |
| Rage | 目标选择、伤害与命中率、多点、穿墙、DT、急停 / 开镜与 Anti-Aim |
| Legit | 武器配置侧栏、FOV、平滑、Accuracy、瞄准曲线、Triggerbot、RCS 与 Semi-rage |
| Visuals | 玩家 ESP、Chams、镜头与命中反馈、世界效果、投掷物轨迹和点位管理 |
| Skins | 枪械、刀、手套和人物模型浏览与配置 |
| Misc | 移动辅助、Peek、近战辅助、投掷控制与 HUD widgets |
| Config | 参数保存 / 加载、压缩、界面样式与翻译包 |

## Rage

左侧选择参数，中间调整 Accuracy，右侧集中管理 Rage Aimbot 和 Anti-Aim。Global、武器组、单把武器按层级组织；武器覆盖只保存 Accuracy，瞄准与 Anti-Aim 使用全局设置。

- Hitboxes 与 Multipoint 集中在 Accuracy；MP 齿轮内提供逐部位比例。
- MD、HC、MP 支持 Auto；自动 HC / MP 提供均衡、可靠性、时机三种策略。
- DT 仅用于 Rage 枪械开火，排除 R8，并通过 widget 展示充能状态。
- 射击外推与移动外推分别控制，配合伤害、命中率和穿墙路径判断。

![Rage 页面：配置侧栏、Accuracy、Aimbot 与 Anti-Aim](docs/images/rage.png)

<details>
<summary>查看配置添加与逐部位 Multipoint</summary>

![Rage — 搜索添加武器或组，手持武器置顶](docs/images/rage-config-add.png)

![Rage — MP per hitbox 与八个部位比例](docs/images/rage-multipoint.png)

</details>

## Legit

左侧配置栏支持 Global、武器组和单把武器覆盖，通过“+”搜索添加，手持武器置顶；右键确认移除。四个子页面分别管理 Aim、Accuracy、Humanization 和 Trigger / RCS，Semi-rage 已整合到 Accuracy。平滑和 RCS 的常用选项直接显示。

![Legit — Aim：FOV 与平滑](docs/images/legit.png)

<details>
<summary>查看 Accuracy、Humanization、Trigger / RCS 与配置添加</summary>

**Accuracy：目标部位、多点、优先级、瞄准预览与 Semi-rage。**

![Legit — Accuracy](docs/images/legit-accuracy.png)

**Humanization：曲线、速度变化、反应延迟与微调。**

![Legit — Humanization](docs/images/legit-humanization.png)

**Trigger / RCS：扳机条件、射击时序与后坐力控制。**

![Legit — Trigger / RCS](docs/images/legit-trigger-rcs.png)

**配置添加：搜索武器 / 组，手持武器位于列表顶部。**

![Legit — 配置添加](docs/images/legit-config-add.png)

</details>

## Visuals

本地、友方和敌方分别配置。玩家页面提供边框、血条、装备信息、状态标记、Glow 与多种 Chams 材质；本地页面集中镜头、模型和命中反馈。World 与 Nade Pred 分别管理环境效果和投掷物辅助。

![Visuals — Enemy：ESP、状态标记与 Chams](docs/images/visuals.png)

<details>
<summary>查看 Local、Friendly、World、Nade Pred</summary>

**Local：模型、镜头、显示移除与命中反馈。**

![Visuals — Local](docs/images/visuals-local.png)

**Friendly：独立的友方显示选项。**

![Visuals — Friendly](docs/images/visuals-friendly.png)

**World：天空、光照、天气、道具与环境效果。**

![Visuals — World](docs/images/visuals-world.png)

**Nade Pred：轨迹、落点与投掷点位采集 / 管理。**

![Visuals — Nade Pred](docs/images/visuals-grenades.png)

</details>

## Skins

通过图标网格浏览枪械、刀和手套，支持搜索与阵营区分；人物模型单独管理。皮肤配置包含磨损、模板和适用物品的 StatTrak 设置。

![Skins — Guns：枪械浏览](docs/images/skins.png)

<details>
<summary>查看 Knives、Gloves、Agents</summary>

**Knives。**

![Skins — Knives](docs/images/skins-knives.png)

**Gloves。**

![Skins — Gloves](docs/images/skins-gloves.png)

**Agents：双方人物模型与自定义入口。**

![Skins — Agents](docs/images/skins-agents.png)

</details>

## Misc

移动、近战、投掷控制和信息 widgets 集中管理。包含 Bhop、Autostrafe、Quick Peek、Duck Peek、刀 / 电击枪辅助，以及按键列表、Func List、炸弹信息和观战列表。Func List 汇总当前 HC / MD / MP、DT 充能与生效的跳跃辅助，可拖动并吸附屏幕边缘；DT 充满时以薄荷色提示。Quick Peek 可选择是否仅等待同一目标的 DT 第二发。

![Misc：移动、近战、投掷与 HUD](docs/images/misc.png)

<details>
<summary>查看游戏中的 Func List</summary>

![Func List：屏幕边缘显示当前 HC、MD、MP](docs/images/widgets.png)

</details>

## Config

保存和加载参数，管理压缩、DPI、圆角、默认页面及翻译包。多数功能可通过右键绑定快捷键，齿轮展开对应的细节设置。

![Config：参数、界面样式与翻译](docs/images/config.png)

## 使用与构建

- 发布包见 [Releases](https://github.com/KaguraWang/kAzeware/releases)。Windows x64 standalone DLL 静态链接 CRT，字体和命中音内嵌。
- `Insert` 开关菜单，`F4` 正常卸载。
- 参数与翻译位于 `Documents/kAzeware/Configs/`；投掷点位位于 `Documents/kAzeware/GrenadeHelpers/`。

<details>
<summary>源码结构与构建说明</summary>

项目采用 C++23、Direct3D 11 和 ImGui。

```text
kAzeware/source/     游戏 SDK、功能、菜单与配置
kAzeware/external/   第三方源码依赖
kAzeware/resources/  内嵌资源
loader/              可选加载器工程
translations/        翻译包
bin/x64/Release/     本地构建输出
```

本机验证环境为 Visual Studio Build Tools v145、Windows SDK 10.0.26100.0、DirectX SDK June 2010。工程沿用 DirectX SDK 默认安装目录，其他安装位置需对应调整。

在根目录 PowerShell 执行：

```powershell
$env:CL='/FS'
& 'C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/MSBuild/Current/Bin/MSBuild.exe' kAzeware.sln /m:1 /t:Build /p:Configuration=Release /p:Platform=x64 /p:MultiProcessorCompilation=false /v:minimal /nologo
```

DLL 输出 `bin/x64/Release/kAzeware.dll`；解决方案另含可选加载器。项目采用 [Apache License 2.0](LICENSE)；上游版权及第三方授权说明见 [NOTICE](NOTICE)，原有第三方许可证继续保留。

</details>

## 版本公告

### alpha-0.0.2 · 2026-11-13

[下载此版本](https://github.com/KaguraWang/kAzeware/releases/tag/alpha-0.0.2)

- 菜单与 widgets 全面改为淡粉色孟菲斯拼贴风格，更新卡片、控件、导航、粒子与 DT Ready 动效。
- Legit 改为可搜索的武器配置侧栏与四个子页面；Semi-rage 并入 Accuracy，覆盖参数稀疏保存，移除覆盖时清理绑定。
- 新增可拖动、贴边的 Func List；子弹火花改为带重力的弹道火星。
- 修正侧键 Toggle 抖动和输入重复计数；45ms 消抖仅作用于鼠标侧键，保留键盘快速连按。
- 统一窗口回调的安装 / 恢复方式，修正 Misc、World 在默认尺寸下的底部裁切。
- 菜单及 DLL 属性统一为 `alpha-0.0.2`，全部 16 个主、子页面与配置弹窗重新实机截图。

Release x64 构建、输入回归、压缩配置保存 / 加载和页面截图检查通过。五 BOT、约 52ms 延迟的可比本地场景未观察到新增显著帧耗时；这不代表完整实战功能已验收。本版已在本机加载，等待用户验收。

DLL SHA-256：`bd3490c50f9ff4a745b5f3bba861bd56ab7ccee4b6482492191159cd7f1fb1e4`

### 源码授权更新 · 2026-11-12

- 项目源码采用 Apache License 2.0，并保留上游和第三方版权声明。
- 全部 362 个代码文件各加入一行 Apache 2.0 提示注释。
- 本次为源码授权和注释更新，已发布 DLL 未重新打包。

### alpha-0.0.1 · 2026-11-12

[下载此版本](https://github.com/KaguraWang/kAzeware/releases/tag/alpha-0.0.1)

- Rage 改为 Config 侧栏 + Accuracy + 全局 Aimbot / Anti-Aim；支持搜索新增武器或组，手持武器置顶、右键确认移除。
- Hitboxes / Multipoint 移入 Accuracy；MP 齿轮常驻，保留逐部位比例；HC 齿轮仅 Auto 时显示。
- Quick Peek 增加“仅等待同目标 DT 第二发”的可选策略，默认沿用原等待行为；调整斜面 / 楼梯回撤的碰撞与速度处理。
- 菜单标志与 DLL 属性统一显示 `alpha-0.0.1`。
- 适配基于 CS2 1.41.9.7 / build 14197；游戏更新后需重新核对兼容性。

本版为 Alpha，已完成构建和有限本地验证，等待用户验收。非 180° AA 开火朝向、Visualise 稳定性及严格分目标回撤仍有待完整验收。

DLL SHA-256：`2355f72e2ff0875fd6fa4b65fa5b5bb8e71e7bfdd9842cf570d5e091186f9d89`

---

<p align="center">支持千早爱音谢谢喵</p>
<p align="center"><img src="docs/images/support.jpg" width="360" alt="赞赏码"></p>
