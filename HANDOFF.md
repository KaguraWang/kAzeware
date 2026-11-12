# kAzeware 当前交接

更新：2026-11-12（Asia/Shanghai）。工作区 `C:\Users\Simon\Desktop\DTImpl`。现行版本 **alpha-0.0.1**；旧构建记录移至 [docs/HISTORY.md](docs/HISTORY.md)，其中的“当前状态”不适用于本版。

## 交付状态

| 项目 | 实际状态 |
| --- | --- |
| 代码 / 构建 | 本轮 Peek 策略、斜面回撤、Accuracy 配置及菜单完成；Release x64 DLL 构建通过 |
| 正式 DLL | `bin/x64/Release/kAzeware.dll`，11,938,816 bytes |
| 桌面交付 | `C:\Users\Simon\Desktop\kAzeware-alpha-0.0.1-standalone.dll`，与正式 DLL 逐字节一致 |
| DLL SHA-256 | `2355f72e2ff0875fd6fa4b65fa5b5bb8e71e7bfdd9842cf570d5e091186f9d89` |
| PDB SHA-256 | `64c480d06c254324094e398342e131737c6afc6550936cdd54955c74728f760e`，仅本地保留 |
| 产品版本 | 菜单、PE FileVersion / ProductVersion 均为 `alpha-0.0.1`，数字版本 0.0.1.0 |
| 发布位置 | [KaguraWang/kAzeware](https://github.com/KaguraWang/kAzeware)，标签 / 预发布 `alpha-0.0.1`；源码在 main，DLL 为 Release 附件 |
| 加载 | 客户端 PID 4692，正式路径加载，`g_initPhase == 2`；游戏保持运行，菜单关闭 |
| 参数 / 清理 | 已恢复本轮前 `__before_alpha_final_20261112`；严格 Peek 等待选项为关闭；移动 / 攻击指令已释放，noclip 关闭；客户端及服务器无 DTDiagnostics 模块残留 |
| 验收 | **有限本地验证完成、已加载待用户验收**；本版尚无用户确认，不沿用历史构建验收 |

Release DLL 使用 `/MT`；依赖表只有 WINMM、KERNEL32、USER32、SHELL32、ole32、IMM32、D3DCOMPILER_47。字体与命中音内嵌。Release 链接只记录 PDB 文件名；此项只改变打包设置，LTCG 重链接产物并非逐字节相同。未发布 PDB、加载器或测试辅助 DLL。

## 本轮实现与入口

以下源码路径相对 `kAzeware/source/kAzeware/`。

### Quick Peek 的 DT 等待策略

- `config/config.h`、`config/configmanager.h`、`keybinds/keybinds.cpp`：新增 `movement.peek_dt_same_target`，默认 false，支持保存与绑定。旧参数缺失时保持此前策略。
- `menu/menu.cpp`：Misc / Movement / Quick Peek 齿轮中新增 `Only wait for same-target DT`。未开启时仍按原等待窗口处理 DT。
- `features/ragebot/shots.cpp/.h`：记录本轮首发的致死目标和非致死后续目标；严格等待仅认可仍存活、非免疫且与本命令已准备目标一致的同一目标。查询不增加扫描、穿墙或 HC 采样。
- `features/ragebot/rage.cpp/.h`：`IsPreparedTarget` 复用当前命令的真实准备结果。
- `features/movement/peek_assist.cpp`：严格策略拒绝等待后，回撤期间持续保持优先级，避免下一命令被 Rage 急停抢回移动。仍尊重 Quick Peek 原有 On shot / On release 触发、用户接管、归位和关闭开关；该选项不会自行启用 On shot。

### 斜面 / 楼梯回撤

- `features/ragebot/motion.cpp/.h` 的 `CanAdvanceOnGround` 复用已有 MoveHull 的滑动、台阶、地面支撑和实际蹲姿碰撞体，使用独立新查询周期。
- `features/movement/peek_assist.cpp` 保留便宜的直线检查，仅受阻且在地面时进入台阶计算；移除 72u 高差取消条件，近终点减速使用三维剩余距离。
- 不提供完整地图寻路，也不通过 noclip、瞬移或降低开火条件绕过物理障碍。

### Rage 配置与菜单

- `features/ragebot/menu.cpp`：左 Config、中 Accuracy、右 Aim / AA。侧栏只显示 Global 和已添加覆盖；Global / 组 / 武器之间用细线分隔。新增弹窗有搜索与有界滚动列表，手持武器置顶；右键覆盖项弹出 Remove / Cancel 确认。
- Hitboxes、Multipoint 属于 Accuracy。MP 齿轮**常驻**，包含逐部位开关和八个比例；主比例 Auto 或有效逐部位比例 Auto 时提供自动 MP 策略。HC 齿轮仅 HC=Auto 时显示。
- `features/ragebot/settings.h` 的 `CopyAccuracy` 明确 Accuracy 字段；`profiles.cpp/.h` 组合 Global Aim / AA 与武器 → 组 → Global 的 Accuracy。
- 新增覆盖只复制最近父级的 Accuracy。移除时清理覆盖、初始化状态及其 Accuracy 绑定；重加从父级重新初始化，不能移除 Global。
- 保存只写 Global 全设置和启用覆盖项的 Accuracy；旧子配置中的共享 Aim / AA 值不再使用。`keybinds.cpp` 将旧子配置的共享绑定迁移到 Global；显式 Global 绑定优先。实际按键行为未做全键位覆盖。
- `version.h`、`menu/menu_ui.cpp`、`kAzeware.rc` 和工程 Release 链接设置：统一菜单、文件属性及发布文件名的 `alpha-0.0.1`。

结构尺寸：Rage::Settings 仍为 **184 bytes / 42 profiles**；MovementConfig 从 64 变为 **68 bytes**，新选项偏移 +64。外部工具必须核对当前类型与 PDB，不复用旧 RVA / 步长。运行态充能和扩散快照不纳入 Accuracy 保存。

## 实际验证

本轮记录均在工作区外 `C:\Users\Simon\Desktop\DTDiagnostics\rage-config-20261112\`；Peek 算法边界另在同级 `peek-policy-20261112\`。这些工具和记录不进入源码仓库 / Release。

| 检查 | 结果与边界 |
| --- | --- |
| 构建 | 最终 `build-final.log` 成功；本轮未重建 loader |
| 当前地址 | `address-audit.txt` 为 PASS，针对已安装 CS2 1.41.9.7 / build 14197 |
| 配置实现 | 提取真实 profiles.cpp / Settings 的编译检查通过：继承、增加 / 删除 / 重加、旧配置、稀疏保存、Global 共享作用域、运行态不复制。引擎及绑定注册以必要桩替代 |
| 原生配置读写 | `serialized-scope.json`：活动覆盖 8 / 13 / 38，只写 Accuracy；Global 保留共享项，已移除 SMG 不再保存；参数已恢复 |
| 实际菜单 | PrintWindow 确认最终布局、alpha-0.0.1 标志、手动 MP 齿轮 / 逐部位弹窗、手持 SSG 08 置顶及移除确认；此前还实际点击添加 / 移除 SMG 并核对状态 |
| Peek 策略 | 提取实际回撤 / DT 查询逻辑测试通过：默认行为、同目标 / 分目标 / 死亡 / 切换、严格回撤持续优先、接管、过期、关闭、受阻。原生观测记录 5 个 pending 命令保持同目标等待；原生分目标强制回撤窗口未采到有效 pending，不能据此声称该实战分支通过 |
| 物理边界 | 提取真实 MoveHull / CanAdvance 测试通过：平地、斜面、台阶、墙、低顶、无地面、查询失败、step=0、空中、蹲姿及查询周期 |
| 实际回撤 | noclip 关闭，真实 CT 高差路线最低约比起点低 54u，回撤后误差约 0.25u；不等于所有楼梯 / 障碍路线通过 |
| 可比性能 | 同落地站位、SSG 08、五 BOT、菜单关闭的前后采样：基线 1571 帧均值 3.816ms / P95 5.216ms；本版核心 1535 帧均值 3.908ms / P95 5.292ms。没有减少 MD / HC / 穿墙要求，未观察到新增显著开销 |
| 最终 UI 构建补验 | 最终打包构建再次初始化通过，1175 帧均值 5.103ms / P95 9.798ms；此时已恢复用户参数且目标状态不同，仅为运行采样，不与上述固定场景作性能提升 / 回退比较。数据见 release-performance.json |
| 交付清理 | 本轮测试辅助源码 / 钩子仅存在工程外；正式源码和 DLL 检查无本轮测试标记；最终 DLL / PDB 与构建输出一致，桌面 DLL 同哈希。客户端及服务器测试模块全部退出，见 final-audit.json |

训练环境沿用无限热身、5 名敌方 CT BOT、0 名友方 BOT、任意位置免费购买、死亡不掉武器。执行 `mp_death_drop_gun 0`、`mp_buy_anywhere 1`、`mp_warmup_items_nocost true`；开火测试关闭 noclip。客户端 / 服务器各发送 12ms + 接收 13ms，HUD 约 52–53ms。窗口保持 2560×1600 后台运行；不使用 computer-use，不主动抢前台。

## 保留项与未完成验收

- 普通自动急停 / 自动开镜：用户在 `compat-14197-v1` 明确确认通过；该确认不自动覆盖 alpha-0.0.1。
- Auto MD / HC / MP 与 Extrapolation：保留先前实现和有限验证。MD 半血仅 DT 开启且真实就绪；HC0 / MP0 为 Auto，NS 独立。射击外推量 0–8 ticks，移动外推独立开关。
- R8：保持全部专属优化之前的回滚选择；本轮没有重新合入被撤回的预热 / 急停实验。
- AA：用户要求暂缓。silent aim + hideshots 开火时，非 180°、尤其 ±90° 的服务器 / 他人朝向未通过。
- Visualise Aimbot：墙后提示、移动 / 视角变化闪烁仍缺最终验收；仅 Rage、左右最多 50u、只扫当前本体、不扫描回溯。DT 致死后路径第二目标需求保留。
- Duck Peek 摄像头、FS / 绑键、单独自动刀 DT widget、燃烧瓶 widget 的历史问题缺最终完整验收证据，不能用本轮菜单 / 回撤检查代替。
- 当前不声称零空枪、逐类武器、全部 NS / FNS、全网络条件或所有菜单页面通过。
- 通用启动 / 卸载日志保留；其与临时采样探针不同。翻译目录的历史污染与缺少提取脚本仍记录于历史文档，未在本轮扩大修改范围。

## 仓库与后续规则

工作区现在有 Git 仓库，origin 为 `https://github.com/KaguraWang/kAzeware.git`，保留原 main 的 README 历史后导入源码。`bin/`、`obj/`、个人 `.codex/`、PDB、DLL 和 Python 缓存不提交；临时根目录采样产物移到外部诊断目录。代码原目录结构保留，历史交接归档到 docs/。

每次变更同步更新 README / HANDOFF，重建时更新产物路径、版本、哈希和加载状态。临时测试入口必须从正式源码移除后重新编译，不能只关闭。诊断模块必须串行使用，产品卸载前先确认它们退出。以后按当前用户优先级继续；交付后待命，不自行开展历史待办。游戏本轮保持运行等待用户验收。
