# 历史开发与验证记录

以下为 alpha-0.0.1 整理前的交接快照。旧路径、PID、哈希、参数范围及“当前”字样只描述对应历史构建；现行状态以根目录 [HANDOFF.md](../HANDOFF.md) 为准。

---

# kAzeware 工作区交接

最后更新：2026-11-12（Asia/Shanghai）。工作区：`C:\Users\Simon\Desktop\DTImpl`。

## 1. 当前交接点

最新反馈（2026-11-12）：MD=0 仅在 DT 开启且就绪时取半血；HC=0 和 MP=0 均改为 Auto，取消 HC=0 隐式 NS 和 MP=0 关闭多点。两项分别提供均衡／可靠性／时机，且仅在各自 Auto 时显示设置齿轮。本轮 `auto-aim-v1` 已完成代码及构建，已加载客户端，完成有限本地验证，待用户验收。此前自动急停／开镜的用户验收对应 `compat-14197-v1`，不自动套用到本构建。

此前已完整回滚到 R8 优化前的「18:06 那版」。随后菜单重排及多目标/Peek 修复形成 168 字节 Settings、42 profiles 的基线。本轮保留此前修改，不恢复 R8 专属预热优化；增加外推后曾为 172 字节，本轮增加自动策略后为 184 字节，仍为 42 profiles。旧配置按字段读取，缺失的新字段使用默认值；外部工具不得继续假定 168 或 172 字节步长。

当前客户端为 PID 4692，保持 `-insecure`、2560×1600，连接本机独立 Mirage 服务器 PID 5772 / `127.0.0.1:27016`。旧客户端已退出，本轮重新启动并保持后台窗口，原前台最终未变；启动时恢复过一次游戏请求的前台。沿用无限热身、5 名 CT BOT、0 名友方 BOT、任意位置免费购买、死亡不掉枪、关闭 noclip 的训练对局。客户端和服务器各使用发送 12ms＋接收 13ms 测试延迟，PrintWindow 实际 HUD 为 53ms。开火及性能测试使用 SSG 08；最终参数恢复和测试工具退出情况见第 3 节。

模糊排查：PID 4060 的视频配置及实际 PrintWindow 图像均为 2560×1600，FSR 配置为 0；HUD/Panorama 开关为 true。隐藏游戏菜单和正常卸载产品 DLL 后仍然模糊。干净重启后画面恢复，随后重新设置五敌方 BOT 训练条件并加载同一 DLL，注入前后画面均已核对清晰。前轮外部诊断曾误入原生截图接口，且最小化加载发生 Present 等待异常；尚未单独证明哪个持久渲染状态造成模糊，不将恢复描述为产品代码修复。新启动保持正常后台窗口，不在初始化期间最小化；不使用游戏原生截图接口，仅用 PrintWindow。

地址适配轮的历史启动记录：当时游戏更新至 1.41.9.7/14197，更早的射击和性能采样来自 1.41.9.6。旧命令工具因地址校验失败自行退出；本轮在工作区外重新核对当前原生命令服务，完成训练设置后卸载。未把工具地址改动写入产品源码。旧 PID 4060 恢复窗口可见性时曾短暂激活，随后最小化并恢复前台；该历史轮 PID 14320 保持正常后台窗口，启动后核对原前台未变。本次确认对局、训练条件、DLL 初始化和清晰画面，不代表新版游戏的完整功能兼容性回归。

工作区根目录未发现 `.git`。旧 README 引用的 `HANDOFF.txt`、`DELIVERY.json` 和 `HANDOFF_AGENT_CHANGES_2026-11-01.txt` 当前不存在，已移除失效入口；本文件是现行交接入口。

## 2. 工作约束

- 先读 [AGENTS.md](AGENTS.md) 和 [README.md](README.md)。遵守最小改动原则，保留现有架构、用户参数和工作区修改。
- 不使用 computer-use 技能，不让游戏主动抢前台；确需看画面时用 PrintWindow 后台截图。
- 训练对局按最新用户要求使用无限热身、5 名敌方 BOT、0 名友方 BOT；每次启动执行 `mp_death_drop_gun 0`、`mp_buy_anywhere 1`、`mp_warmup_items_nocost true`。开火测试关闭 noclip，使用真实物理状态。设置数量时不得叠加自动补足和手动添加，避免重复生成。
- 新增或修复功能需要可比场景的帧耗时回归，不能通过降低 MD、HC 或既有判定要求掩盖性能问题。
- 正式源码及 DLL 不保留测试专用采样、探针、计数、钩子、入口或引用；应移除后重新编译。独立诊断工具可留在交付目录之外。
- 未收到验收反馈前，不将“已注入待验收”改为“用户验收通过”。历史用户确认不等于最新构建的完整回归。
- 后续项目变化时自行同步维护 README 和本文件；及时更新与用户最新要求冲突的历史操作说明。

## 3. 最新产物

| 项目 | 记录 |
| --- | --- |
| DLL | `bin/x64/Release/kAzeware.dll` |
| 配套符号 | `bin/x64/Release/kAzeware.pdb` |
| 构建 | Release x64，2026-11-12；本地标签 `auto-aim-v1`，不是产品版本号；DLL 工程构建通过，loader 未重建 |
| DLL SHA-256 | `ae6b7d3c7d678f48020e34b44ed5a11c9743982d5f69379947d57db5503a1d1c` |
| PDB SHA-256 | `6fc8e1cf6d34202c6bb15da7254ef459c2917cb385bf01ef5b84742f4ab6d8fd` |
| 验证记录 | 当前地址审计、Auto MD/HC/MP 规则边界、42 profiles 的 Auto 数值和三策略保存/加载通过；约 53ms 的 SSG 08 提交记录和同场景帧耗时回归完成，详见下节 |
| 加载 | 客户端 PID 4692，游戏 1.41.9.7，正式路径加载，`g_initPhase == 2`；已恢复 `__before_auto_aim_20261112`（Global HC52 / MP50），菜单关闭，独立测试模块已全部退出；2560×1600 后台窗口保持运行 |
| 验收 | Auto MD/HC/MP 代码完成、构建通过、有限本地验证完成、已注入待用户验收；不视为所有武器或全部功能的完整回归 |

本轮证据根目录：`C:\Users\Simon\Desktop\DTDiagnostics\auto-aim-20261112\`。构建日志 `build.log`，策略与布局检查 `policy_test.cpp/.exe`，配置往返 `config-roundtrip.json`，地址审计 `address-audit.txt`，射击核对 `shot-results.json`，帧耗时 `performance-final.json`，正式产物与加载清理 `final-audit.json`。`before/` 保存本轮前源码及产物；测试工具均在工作区外。此前 `auto-md-20261112/` 证据属于 Auto MD 首版。

`multitarget-20261112/final-audit.json`、`startup-v2-20261112.json`、`compat-14197-20261112/final-audit.json` 和 `extrapolation-20261112/final-audit.json` 均为历史构建/加载记录，不代表当前产物哈希。当前产物及加载核对以 `auto-aim-20261112/final-audit.json` 为准，后续用户验收反馈以本文记录为准。

早前启动核对 `startup-20261112.json` 对应 00:46 的旧 18:06 DLL，不代表当前加载状态。旧 18:06 原始产物仍保存在 `source-format/baseline/`（DLL 哈希 `8950560243dd12bdc7ae8636cd25c15cc7c4a5023c81bc81e8124cac6be5ed8c`）。

此前回滚核对：`C:\Users\Simon\Desktop\DTDiagnostics\r8-peek\rollback-final-audit.json`，记录当时源码、DLL/PDB 哈希及加载和清理状态。原有 `peek-regression/final-clean-audit.json` 只作为历史检查；原版内建诊断接口仍列于第 6 节，不将精确回滚或本次启动描述为完成这些遗留清理。

## 4. 最近修复及源码入口

### Auto MD 就绪条件、Auto HC / MP（auto-aim-v1，2026-11-12）

- MD=0：仅 `double_tap && double_tap_ready` 取 ceil(HP/2)，否则完整 HP。真实开火使用本条命令的原生充能校验；第一发后保留同一轮第二发资格，第二发消耗或超时后恢复完整 HP。`Submission` 保存该发的就绪快照，重放不再被当前 widget 状态改变；不是把上一条 UI Ready 当作当前开火许可。
- HC=0：正常 HC 自动计算，NS 仅由独立 `no_spread` 控制，FNS 保留独立开关。三策略的可靠性目标为均衡 90%、可靠性 98%、时机 70%；根据 HP / 当前候选伤害计算必要命中次数并换算每发所需概率，随后受恢复精度下的几何可达上限约束。几何上限不包含穿墙收益，实际当前散布的样本仍逐一验证 MD 和穿墙。Force Shoot 的既有最大精度旁路保持不变。
- MP=0：不再表示禁用。使用原生 HC 样本的策略分位计算散布锥，结合距离与胶囊半径求允许的最大横向偏移，上限保持原有 95% 半径；无法留出余量时使用中心。仍在原有中心＋四方向点位中按伤害、HC、部位、穿墙和目标优先级筛选。逐 hitbox 的 0 同样表示 Auto。
- 上述是给定候选点、扩散模型和策略目标下的启发式选择，不宣称穷举全局最优；策略目标也不是服务器实测命中率保证，不预测敌人的未来操作。
- HC、MP 的设置齿轮分别只在主滑条为 Auto 时显示，包含各自三策略；手动数值隐藏各自齿轮。HC samples、逐 hitbox 设置留在对应弹窗。HC/MP 的绑键数值 0 同样显示 Auto，策略支持配置保存与绑定，默认均衡；旧 HC=0 配置改用 Auto，显式 NS 开关保持，旧 MP=0 改用 Auto。
- 性能修复：内部“只扫中心”的调度不再占用用户 MP=0；新增 `ScanAcceptance::lastPoint` 保留中心优先轮询。恢复精度相同时复用 HC 几何计算；扩散样本缓存保留当前／恢复精度两组，避免每个候选重建随机样本。没有减少 HC 样本数量或绕过伤害、穿墙验证。
- Settings 为 184 字节／42 profiles：运行态 ready 位于 +167，两个持久策略分别位于 +172 / +176，运行态散布锥位于 +180。运行态字段不保存、不绑键；外部工具已按真实头文件和符号大小核对布局。

源码入口（相对 `kAzeware/source/kAzeware/`）：

| 文件 | 改动 |
| --- | --- |
| `features/ragebot/settings.h`、`auto_aim.h`（新增） | Auto 语义、就绪快照、策略与计算规则 |
| `features/ragebot/menu.cpp`、`profiles.cpp`、`keybinds/keybinds.cpp` | 条件齿轮、独立 NS、标签、保存及绑定 |
| `features/ragebot/shots.cpp/.h`、`rage.cpp` | 真实就绪、Auto HC、扩散分位、提交／重放快照 |
| `features/ragebot/target.cpp/.h`、`history.cpp`、`motion.cpp` | 共用 Auto MP、中心轮询、回溯及预测接入 |
| `features/legitbot/accuracy.cpp` | 原生 HC 样本的两组缓存；不改变样本生成公式 |
| 根目录 `README.md`、`HANDOFF.md` | 当前能力、产物、验证及限制 |

实际验证：

- Release x64 DLL 构建通过；当前游戏地址审计 PASS；真实头文件测试覆盖 HP 1–200、DT 开关／就绪组合及取整、固定 MD/HP+、HC0 独立 NS、MP0 Auto、策略顺序／必要命中次数／可达上限、距离和扩散边界、184 字节布局。
- 42 profiles 的 MD/HC/MP=0 和各自三策略保存、改值、加载后逐项一致；运行态字段恢复默认且不随配置保存。后台菜单截图确认 Auto 下两齿轮显示、手动数值下两齿轮隐藏。
- 约 53ms 的 SSG 08 测试收到 42 条 ApplyHistory 记录、10 条不同命令：HC=0 且 NS=false；全部记录的 MD 符合其就绪快照，候选伤害满足 MD，同命令重放就绪状态一致。观察到就绪半血（100→50）和未就绪完整血量（80→80、86→86）。记录含重放，不作为独立服务器子弹数、零空枪或所有武器覆盖证明。
- 最终构建站定 ABBA（HC52/MP75—Auto/Auto—Auto/Auto—HC52/MP75）均值 3.478 / 3.444 / 3.473 / 3.457ms，P95 4.581 / 4.602 / 4.631 / 4.661ms，5 名敌方 BOT、SSG 08、同一本地落地位置、MD52/DT/autowall/force body/全部部位保持一致。未观察到新增显著开销，不将窗口波动表述为性能提升。初版 P95 约 12ms 的额外采样开销已修复，弃用记录保留 `performance-before-opt.json`。
- 左右移动指令窗口 ABBA 均值 3.847 / 3.722 / 3.665 / 3.639ms，P95 6.105 / 5.869 / 5.949 / 6.023ms，同一参数及五 BOT 场景未观察到 Auto 新增显著开销。移动受墙体和自动急停影响，并非每帧保持匀速；位置从 (-1127.988,-2442.812) 移至 (-1040.254,-2266.348)，结束后恢复原站位，取消全部移动指令。见 `performance-moving.json`、`ab-perf-moving.csv`。
- 测试专用模块全部置于工程外，不进入源码或正式 DLL；源码／DLL 本轮测试标记核对通过，正式 DLL/PDB 与构建输出一致。客户端及服务器均无测试模块残留，已恢复 `__before_auto_aim_20261112`，游戏继续运行。当前等待用户验收，未完成全部武器、NS/FNS 或全部网络情况的实战回归。

### Auto MD 首版（auto-md-v1 历史构建，2026-11-12）

以下为首版验证，DT 仅按开关判断的行为已由上节的真实就绪判定替代。


- MD 滑条及伤害绑键的范围为 0–120；0 显示 `Auto`。Auto 按每个候选敌人的当前 HP 动态求值，不把推导值写回 profile，也不在多个敌人之间共享一个固定 HP 门槛。
- `Settings::Minimum(health)` 在 Auto 下返回完整 HP，或在配置的 `double_tap` 开关开启时返回向上取整的半 HP，例如 100→50、99→50、9→5。按开关判断，不依赖 DT 是否充能完成。已有固定 1–100、HP+1–20 规则不变。
- 现有实时扫描、回溯、HC/种子伤害复核、急停和 Visualise 使用该共用入口。回溯选择缓存增加 DT 开关作为配置依赖，切换后重新扫描；候选 HP 仍按原流程读取/复核。
- 六个源码入口：`features/ragebot/settings.h`、`menu.cpp`、`profiles.cpp`、`history.cpp`，以及 `keybinds/keybinds.cpp`、`menu/menu_ui.h`（均相对 `kAzeware/source/kAzeware/`）。通用伤害标签仅在 Rage 调用处开启 Auto 标签，Legit 的 0 显示保持原样。Settings 仍为 172 字节、42 profiles。
- 实际验证：Release x64 构建通过；真实 Settings 头文件测试覆盖 HP 1–200、DT 开/关及奇数取整，固定 MD 1–100、HP+1–20、0 保存/归一化和标签；42 profiles 的 Auto=0 配置保存/重新加载通过，菜单后台截图确认显示 Auto。
- 约 53ms 的本地对局中，AK-47 的 DT-on 窗口记录 28 条 Auto 提交，100 HP→50 MD；DT-off 窗口记录 33 条，100 HP→100 MD，候选伤害均满足门槛。记录含重放，不当作独立子弹数或服务器伤害验证；其他血量的规则由上述单元检查覆盖，未做逐武器实战回归。
- 枪械 ABBA（固定50—Auto—Auto—固定50，DT 开启）平均帧耗时为 4.10 / 3.93 / 3.87 / 3.90ms，P95 为 5.71 / 5.21 / 5.12 / 5.12ms。5 名敌方 BOT、同一本地位置且落地；未观察到新增显著开销，不把波动解释为性能提升。初次持刀窗口已弃用，见 `ab-perf-knife.csv`；正式数据为 `performance.json`、`perf-gun-before.json`、`perf-gun-after.json`。
- 本轮测试代码均位于工作区外，正式源码未加入探针、采样或测试开关；测试辅助模块已退出，最终 DLL/PDB 与构建输出一致。当前为已注入待用户验收。

### Extrapolation 与外推量滑条（extrapolation-v4 历史构建，2026-11-12）

- 已有急停/开镜通过 `ObserveEnemy`、`AdvanceEnemy` 模拟加减速、重力、碰撞、台阶和地面支撑。本轮复用该实现，为实际射击及 Visualise Peek 接入预测，未另写一套线性位移算法。
- `Extrapolation` 控制射击外推，默认关闭；齿轮内 `Movement extrapolation` 独立控制急停、开镜和 Visualise Peek 的敌人外推，默认开启。关闭移动外推后仍模拟本地玩家运动，敌人保持当前姿态。Visualise 不扫描回溯记录，标记按选中的目标/hitbox 映射回当前本体。
- `Extrapolation amount` 控制射击外推上限，0–8 ticks，默认 8；64 tick 下每 tick 为 15.625ms。只向实际开火时间推进必要的整 tick，最多达到滑条设定值；0 禁用射击外推，不关闭独立的移动外推。时间差超过 8 ticks 的长时间失联不由该短时预测补全。
- 预测后的胶囊、MD/HC/穿墙判断及提交时间保持一致；提交时若目标收到新状态、死亡、外推量已被降低或时间不再有效，则拒绝旧预测。历史记录继续保存真实采集姿态，不混入合成预测。
- 静止且落地的敌人直接复用不变的位置；移动目标复用同命令的急停预测路径，新增缓存不跨命令保留碰撞或伤害结论。没有减少既有命中条件或采样要求。

源码入口（相对根目录）：`kAzeware/source/kAzeware/features/ragebot/` 下的 `settings.h`、`profiles.cpp`、`menu.cpp`（配置/绑定/菜单），`target.h/.cpp`（姿态及扫描），`motion.h/.cpp`（共用预测/缓存），`shots.cpp`、`history.cpp`（开火时钟和提交校验），`visualise.cpp`（标记定位）。Settings 由 168 增至 172 字节，`extrapolation_ticks` 位于 +168；JSON 按字段兼容旧配置，缺失时默认 8。

实际验证与限制：

- Release x64 最终构建通过；当前地址审计通过。工作区外从实际源码提取函数测试了静止、匀速、重力、碰撞、落地、追踪失败、缓存复用/清空/共享、状态变化失效，以及外推时钟上下界、开关关闭、死亡目标、滑条 0/1/2/8 和越界归一化。
- 42 profiles 的两开关保存/加载通过；旧配置加载到 172 字节布局时外推量默认 8；设为 2、保存、改为 0、重新加载后恢复为 2。
- 本地独立服务器及客户端各使用发送 12ms、接收 13ms 的网络测试延迟，实际 RTT 为 51–55ms。场上保持 5 名敌方 BOT、0 名友方 BOT，使用 SSG 08、真实落地状态。v3 HC 测试记录到 27 条外推提交及服务器伤害/击杀；v4 的 2-tick 滑条窗口记录到 10 条提交，其中 9 条外推均恰好推进 2 ticks。提交记录含命令重放，不能当作独立子弹数或零空枪证明。
- v3 同场景 ABBA（关—开—开—关）测试中自动开火平均帧耗时为 4.31 / 4.35 / 4.21 / 4.44ms；Visualise 为 1.17 / 1.35 / 1.25 / 1.40ms。该窗口包含 BOT 死亡/重生，临时高血量设置未生效，不宣称每帧目标状态完全相同。
- v4 恢复用户参数后再次做 ABBA，位置前后相同；平均帧耗时为 9.57 / 10.48 / 9.83 / 9.94ms，P95 为 17.14 / 16.86 / 16.82 / 17.39ms；开启相对关闭的平均差约 0.4ms，P95 未增加。本窗口 Visualise 关闭，不与 v3 不同参数/场景的绝对帧耗时比较，亦不宣称外推零开销。见 `ab-perf-v4.json`、`v4-perf-before.json`、`v4-perf-after.json`。
- 中间一次测试因服务器网络队列溢出断开；未单独确定根因。该窗口已弃用，恢复对局后重新采样，不将重连描述为代码修复。
- 未完成所有武器、全部网络状况或所有 NS/FNS 空枪原因的系统回归。外推估计未来位置，不能预知敌人后续输入及骨骼动画变化；当前状态是已注入待用户验收。

### CS2 build 14197 地址适配（compat-14197-v1 历史构建，2026-11-12）

下表路径相对工作区根目录。没有改动 `motion.cpp`、`shot_plan.h`、Rage 参数或菜单布局。

| 文件 | 改动及原因 |
| --- | --- |
| `kAzeware/source/kAzeware/offsets/offsets.h` | 更新 client/engine 全局及兜底地址；例如本地玩家 `0x2553D38`、实体系统 `0x2709010`、输入对象 `0x2569E30`、预测对象 `0x2553C40`、时钟槽 `0x2221AB8`。射击计数兜底改为 `0x1EE8`，Schema 优先策略保留。 |
| `kAzeware/source/kAzeware/features/legitbot/accuracy.cpp` | 原扩散签名固定读取旧 `m_weaponMode +0x1A00`，实际已移到 `+0x1A40`；旧 DLL 缓存的 `getSpread` 实测为 null，会令 `ReadAccuracy` 无效。改为唯一匹配的字段位移通配签名，并在核对工具中将捕获位移与当前 Schema 比较。 |
| `kAzeware/source/kAzeware/features/prediction/prediction.cpp/h` | 预测/时钟兜底引用同一份中央地址，移除两份过时常量；更新 ProcessMovement、ModernJumpCheck、CheckJumpButton 签名。ProcessMovement 同时与原有调用点交叉核对；没有新增 RunCommand 或跳跃调用。 |
| `kAzeware/source/kAzeware/features/bones/bones.cpp` | 更新 CalculateWorldSpaceBones 主签名的栈大小字节至当前版本，保留原有备用路径。 |
| `kAzeware/source/cs2/entity/C_CSWeaponBase/C_CSWeaponBase.h` | 最大弹匣容量绑定的类型名修正为实际 `CBasePlayerWeaponVData`，修复真实调用者读取不到容量的问题；移除无调用者、当前 Schema 中不存在的 `m_flBulletSpeed` 声明。 |
| `tools/audit_cs2_current.py` | 增加参考快照二进制指纹门槛、原生精度/预测/骨骼签名、源码字段名覆盖和预测地址共享检查。旧脚本会让源码和同一份旧表互相比对后误报 PASS；现在参考指纹过期会失败。增加 `--dump-dir` 便于检查不同快照及故障用例。 |
| `dumper/output/` | 刷新 global/button 地址及 client/server 字段偏移的 JSON/HPP/CS/RS/Zig 表示；`info.json` 写入 build 14197、核对过的二进制 SHA 和刷新范围。枚举元数据、其他生成内容和接口参考表不是本轮完整重新生成，不能据新 build 标签宣称全部静态 SDK 已复验。 |

实际核对与边界：

- 从当前进程读取 Schema，client 有 542 个类、3276 个字段。对源码 205 个字段名检查通过，其中原生 identity 指针按 `+0x10` 单独处理。当前工程所有实际字段绑定均可在新快照中找到。
- 当前地址检查 PASS：39 项必需签名均唯一；原有输入备用签名仍有两个匹配，但首选签名唯一。源码另扫描 189 个字面量调用点：169 个唯一、16 个多匹配、4 个旧备用签名不匹配；这四个属于 tier0/client 材质备选、天气备选及世界渲染备选，相应首选/替代路径命中，不将扫描结果描述为“189 个都唯一”。
- 故障用例仅将参考快照中的 engine 指纹改错，其余地址/签名保留正确，检查返回失败，验证不会再只因旧表内部一致而报通过。
- 新全局地址只读核对了 build 14197、真人 HP/队伍/位置、输入对象与指针槽一致、网络对象、2560×1600 窗口；最终枚举为一名 T 真人和五名 CT BOT。
- 新 DLL 的 `getSpread` 指向当前 `client+0x8087D0`。实测 SSG 08、模式 1，原生返回 `0.0002300000051`，与当前 Schema/VData 读取值完全一致；这属于绑定/数值核对，不是开火命中验收。
- 两个约 6 秒自然交战窗口：旧版补齐同一个原生 GetSpread 指针后作为有效 HC 基线，1117 帧平均 5.369 ms、P95 9.550 ms；新版 1579 帧平均 3.797 ms、P95 6.908 ms。没有改 MD/HC 或开火判定以降低消耗。但站位/视角不同，不能据此宣称严格性能提升比例，也没有完成固定路线的移动急停回归。旧版临时内存绑定随其正常卸载结束，正式源码中没有测试入口或采样代码。
- 正式源码及 DLL 已搜索本轮探针标记，无残留；DLL/PDB 与构建输出逐字节一致；外部帧采样、配置转移及只读核对均结束，无辅助模块残留。正式 DLL 中的外部构建 PDB 路径不是测试依赖。

用户验收补记（2026-11-12）：用户先确认“实测急停功能已正常。之前的偏晚是未更新导致”，随后确认“自动开镜也正常”。因此关闭三图中急停偏晚、最终停点越过开火窗口以及自动开镜偏晚的待修项，记录为 `compat-14197-v1` 两项用户验收通过；地址更新后未另行修改运动规划算法。此次验收记录沿用原产物路径、标签、哈希和加载记录，未为文档更新执行新的构建、注入或帧耗时测试。

参考方法：[cs2-dumper 全局地址扫描](https://github.com/a2x/cs2-dumper/blob/main/src/analysis/offsets.rs)、[按键链表布局](https://github.com/a2x/cs2-dumper/blob/main/src/source2/client/input.rs)。实际数值来自本机二进制和当前进程，不直接采用网上输出表。

### 多目标扫描、急停与 DT Peek（multitarget-v2 历史构建，2026-11-12）

以下为此前构建的功能证据，不自动套用到当前 `auto-aim-v1`。

| 文件（相对 `kAzeware/source/kAzeware/features/`） | 改动及原因 |
| --- | --- |
| `ragebot/target.cpp/h` | 非最高伤害优先模式使用首个完整通过 MD/开火验收的点，避免继续追逐额外伤害；最高伤害优先保留完整比较。最近准星优先在穿透前应用已有最优角度边界，生命值/距离优先提前排除无法胜出的目标；原有目标优先级比较仍执行。 |
| `ragebot/motion.cpp/h` | 当前及未来运动查询分为中心、边缘两轮，各有独立游标；中心先轮询各敌人，边缘预算用尽后下次仍先检查新中心机会，再续扫边缘。只保存遍历位置，不跨命令保存伤害或 HC 结果；所有原多点保留。`MovementReady` 改用 `CommandDisplacement` 的命令末速度，与随后精度验收使用同一状态，避免等待下一命令的玩家速度。 |
| `ragebot/shots.cpp/h` | 接入命令末速度门槛；提供基于既有 `firstCommand/untilCommand` 的 `DoubleTapPending` 查询，无新计时器或配置字段。 |
| `movement/peek_assist.cpp` | 保留首发/松键产生的回拉意图；DT 第二发仍待处理，或当前 Rage 已准备制动时，在写方向、速度及障碍 trace 前让出该命令。解除后沿用原回拉状态继续；用户反向接管和关闭功能的取消行为保留。 |

完整穿透、MD、HC、弹道与最终眼位/精度复核均保留，未截短正式穿透射线。工作区外截短射线实验在 99,165 次对照中有 1,588 次结果不一致，已拒绝采用，不能当作正式优化或通过记录。正式源码没有加入这些实验、采样或观测入口。

实际验证：

- 抽取当前 `ScanPose`、`ScanCandidate`、`MovementReady` 及运动准备查询函数体独立编译执行：26 项断言通过，覆盖 MD 拒绝、HC 失败后继续 MP、最高伤害优先、角度剪枝、Force BAIM、命令末速度边界、中心/边缘互补及预算续扫。调度检查确认后排目标中心优先于前排被挡目标的边缘，边缘续扫期间新出现的中心机会仍能被检查。此类检查使用受控依赖，不替代真实物理碰撞验证。
- 子代理抽取当前 Peek/DT 函数体编译执行：31 项断言通过，覆盖首发后等待、完成与过期、已有回拉和 Rage 制动冲突、松键、反向接管、关闭与重置。
- v2 原生 12 秒 DT/Peek 观测为 PASS：768 条 Peek、753 条 Quick Peek 开启记录、2 次开火提交、0 次移动仲裁违规。首发命令 `486843`、第二发 `486857`，同一目标 `17728353`；14 条待处理 Peek 命令返回 `-1` 且未改方向，第二发完成后 `486858` 恢复回拉。中间未开火的 12 次提交属于 HC 未通过，未用强行放宽条件换取双发。
- v2 穿墙重复验证：从 `(-1800,-1420,-167.96875)` 脚踏实地向左移动，约 0.225 秒、位移约 12.95u 后首发，命令 `580261/580275`；服务器记录同一护甲目标胃部 54+54 并穿墙击杀。原生观察同时保存生产路径的 seed、精度和预测射线，重建一致，实际 seed 射线伤害约 54.73/54.69，与服务器整数伤害相符。该次双发相隔 14 个命令，不能描述为稳定两命令内完成。
- 另一次 `486857` 第二发服务器只有 41 伤害，计划约 54.67，目标残余 4 HP；没有采到该发 seed、精度和完整姿态。旧监测文件未覆盖本次进程，HitLog 完整数据已被处理删除，无法回放归因。保留为未解释样本，不能直接归为扩散，也不能用后次 54+54 将其标记通过。
- 旧 v1 近距离历史观测曾得到 `226132/226134` 同目标双发、`226135` 回拉及服务器 93+93；v1 的采样不自动算作 v2 通过。旧 v1/v2 观测工具的按钮列偏移有误，这两列不能用于证据；上述移动方向、返回值、DT 状态和新 seed 工具不依赖它们，外部工具源码已修正，旧 CSV 原样保留。
- 实测使用实际 SSG 08（定义 40）、DT、Force BAIM、52 MD、75 MP、75 HC（256 samples）、全部部位与 Autowall；有效参数为完全急停 `stop_mode=0`、`stop_early=0`。开火窗口确认正常移动类型 2、脚踏实地、noclip 关闭。测试后恢复参数并卸载全部外部观测、性能及命令转发模块。

相同起点、视角和约 10 秒左右移动脚本，五名敌人：

| 场景 / 指标 | 菜单重排后旧版 | 本轮最终 v2 |
| --- | ---: | ---: |
| 遮挡场景平均帧耗时 | 4.134 ms | 3.932 ms |
| 遮挡场景帧耗时 P95 | 7.803 ms | 7.794 ms |
| 开火场景平均帧耗时 | 3.543 ms | 3.579 ms |
| 开火场景帧耗时 P95 | 5.157 ms | 5.298 ms |

遮挡场景运动准备函数平均耗时旧版 2.567 ms、v2 1.743 ms；开火场景约 0.278 ms。未出现新的显著帧耗时开销。v1 历史一敌人开火场景平均 3.184 ms、P95 4.238 ms；不能作为 v2 一敌人回归。新旧版制动会改变实际路径，目标复活、免疫及碰撞也会影响开火窗口；因此这是回退检查，不能据此声称精确的耗时提升比例。v1 的初次、复测和全部原始窗口均保留；热身重置后拿成手枪的窗口已排除，不作为 SSG 通过证据。

未覆盖：远程服务器延迟、移动敌人从墙后出现的系统性逐 tick 开火时机对照、NS/FNS/R8 及其他枪械的本轮实战。当前有本地约 54 伤害墙边双发成功样本，但没有完整覆盖所有移动、姿态与穿透路径；第二发 41 的样本仍未归因。不能据有限本地测试宣称已达到所有商业产品的表现或零空枪。

训练场最终保留无限热身、五名 CT 敌方 BOT、无友方 BOT、任意地点免费购买及死亡不掉武器；测试还使用 `mp_free_armor 2`、`sv_infinite_ammo 1`、`bot_stop 1`、`bot_dont_shoot 1`，`mp_respawn_immunitytime` 已设回 0，诊断日志关闭。游戏保留运行待用户验收。

### 基础射击修复（历史保留）

以下路径相对于 `kAzeware/source/kAzeware/features/`。`build-v10` 相对 `build-v9` 仅增加命中日志部位判断的复用修正。

| 文件 | 改动及原因 |
| --- | --- |
| `ragebot/motion.cpp`、`motion.h` | 根据最终命令预测位移和速度，供射击起点和精度计算使用。本地运动参数使用当前武器最大速度；SSG 08 曾误用通用速度 260，而服务器实际使用 230，造成空中减速和精度预测偏差。敌方预测既有默认行为保持不变。 |
| `legitbot/accuracy.cpp`、`accuracy.h` | 精度计算可接收预测的开火速度，运动项和空中项使用对应状态。 |
| `ragebot/angles.cpp`、`angles.h`、`rage.cpp` | 修正充能射击起点；压缩符合条件的自动生成地面移动序列，避免密集输入挤掉原生射击位置历史。 |
| `ragebot/shots.cpp` | 处理起跳、落地和蹲起切换；提交角度后复核眼位和精度，必要时重算开火解；第二次提交后再校验，状态仍改变时不发送该次自动开火。 |
| `ragebot/target.cpp` | 命中部位选择与实测服务器规则对齐：各伤害组内取最近交点，再按组优先级选择；较近胸腹会排除后方头部。修复“手臂挡在胸前就判成手臂”的错误，扫描、HC、NS/FNS 共用判断。 |
| `hitlog/hitlog.cpp` | 日志射线判断复用同一部位选择规则，避免旧算法把正常结果误判为计算错误。日志仍基于保存姿态，不因此获得服务器真实姿态保证。 |

调用入口：`hooks/hooks.cpp` → `ragebot/rage.cpp` 的准备、移动后处理与执行 → `shots.cpp` 的选目标和开火。历史姿态位于 `ragebot/history.cpp`，运动预测位于 `ragebot/motion.cpp`。

## 5. R8 优化前的历史功能验证

以下为保留的功能基线证据。本次回滚未重跑射击或性能采样，不把这些历史结果作为本次用户验收。

- Release x64 构建成功。
- 提取实际部位交点代码进行独立 C++ 检查，覆盖组优先级、胸腹挡头、同组最近点、相同距离、射程和 inset 边界；日志复用检查通过。
- 旧样本中 195 次服务器命中，按新规则回放得到的 hitbox 全部与原生命中记录一致。
- `gpt-6.1-sol / max` 子代理完成射击链路检查，以及站定按住空格、移动按住空格两组 SSG 08 测试。严格按窗口配对，共 42 发 HC：24 发命中、18 发空枪；42 发名义部位均符合计划，24 次实际命中部位均符合服务器实际扩散射线预测。9 次实际部位偏离原计划也可由扩散解释。
- 上述 18 次空枪的实际扩散射线均未穿过保存目标姿态，该批未复现非扩散计算错误。另有 2 发同时伤害其他目标及原目标，原目标均受 292 伤害，不属于漏掉原目标。
- 移动跳枪精度最大差约 `0.0000205`，旧版约 `0.016` 的大偏差未再出现；水平速度最大差约 `0.0000169 u/s`。站定跳枪最大眼位差约 `0.00562 u`。
- 最终版蹲起/侧移补测配对 4 发 SSG 08 HC，全部命中，名义和实际部位核对一致，未见超过 `0.0001` 的精度差异。

### 性能记录

同站位、同左右移动脚本的约 10 秒比较，Visualise 关闭：

| 指标 | v7 | v9 |
| --- | ---: | ---: |
| 平均帧耗时 | 5.724 ms | 5.723 ms |
| 帧耗时 P95 | 10.282 ms | 10.968 ms |
| 帧耗时最大值 | 14.369 ms | 14.951 ms |

该比较未复现此前约 200 FPS 降至 20 FPS 的问题。场景存在累积落地物，不是干净的通用性能基准。v10 另采到平均 `5.488 ms`、P95 `9.524 ms`，但交接时玩家已换武器并改变场景，不能据此宣称相对 v9 性能提升。

### 验证限制

- 最新版仍待用户验收；有限样本不保证所有武器、地图和网络状态下零异常。
- 本轮实战覆盖 HC，NS/FNS 只检查了共用链路，未做本轮实战验证。
- 保存姿态、服务器骨骼快照与最终原生命中 collider 不能无条件等同；“穿过保存姿态”本身不构成服务器必然命中的证据。
- 蹲起补测仅 4 发；部分地面运动预测速度与事后状态仍有差异，该批精度和射击结果一致，不能声称所有斜坡、碰撞和状态切换都已精确验证。

## 6. 其他功能和待办

| 事项 | 交接状态 |
| --- | --- |
| R8 / 自动急停 | 被撤回的 R8 专属预热改动未重新合入；共用命令末速度门槛来自此前多目标修复。本轮只更新地址/字段绑定，R8 未做本轮实战回归，不恢复旧预热优化。 |
| 普通自动急停 | 2026-11-12 用户实测确认 `compat-14197-v1` 正常，之前偏晚由未更新导致，关闭此前偏晚待修项。本轮将已有敌人外推置于独立开关控制，默认开启。 |
| 自动开镜时机 | 2026-11-12 用户追加确认 `compat-14197-v1` 自动开镜正常，关闭此前开镜偏晚待修项。本轮与急停共用移动外推开关。 |
| Auto MD / HC / MP | 当前 `auto-aim-v1`：MD0 仅 DT 开启且就绪取半血，HC0/MP0 为 Auto，各自仅 Auto 显示三策略齿轮。已注入待用户验收，实际验证和限制见第 4 节。 |
| Extrapolation | 射击外推、独立移动外推开关及 0–8 tick 射击外推量滑条于 `extrapolation-v4` 接入，当前继续保留；历史验证与限制见第 4 节。 |
| DT / Auto Peek | 枪械 DT 限 Rage、排除 R8，带充能 widget。历史 v2 同一目标第二发等待期间不回拉、完成后回拉的原生观测通过，另采到穿墙 54+54 击杀；这些记录不替代当前地址版的逐类射击复验。 |
| 菜单 | 保留单页/弹卡结构重排及 42 profiles；Auto 策略使当前 Settings 为 184 字节。Auto/手动两种菜单截图和参数往返已核对，未重跑其余页面的完整验收。 |
| 自动刀 | 用户此前表示逻辑可接受；保留护甲差异判断。单独自动刀 DT widget 闪烁曾被报告，不能用枪械 DT 的测试代替其最新验收。 |
| 投掷物 | 用户此前确认空中轨迹和烟范围正常，燃烧范围经过多轮修正。燃烧瓶落地后 widget 闪烁、燃尽才开始计时曾委派修复，当前缺少最终验收证据。 |
| Visualise Aimbot | 移动/转视角闪烁、墙后提示仍待完成验收。需求：仅 Rage；左右各最多 50u；仅当前目标本体、不用回溯；按真实 MD/HC/穿墙和选目标逻辑判断；DT 首发可致死时继续寻找路径上的下一目标。 |
| AA | 用户已要求暂缓。非 180°，尤其 ±90° 和 manual left/right，在 silent aim + hideshots 开火时服务器及其他玩家所见朝向保持不变，尚未通过。客户端模型表现不能代替服务器验收。 |
| Duck Peek Assist | 触发时摄像头不应随下蹲改变高度；用户报告改善后仍有移动闪烁，缺少最终通过记录。 |
| FS / 绑键 | 有过委派及“Press a key...”不响应的反馈；缺少本轮独立验收记录，不标记通过。 |

继续工作时先遵循用户当前优先级。不要因为本表存在待办，就在等待验收期间自动开始其他功能。

### 本次完整回滚（2026-11-11）

- 源码恢复覆盖 `ragebot/shots.cpp`、`shots.h`、`rage.cpp`、`motion.cpp`、`motion.h`、`shot_plan.h`，以及本轮一度改动的 `main.cpp`、`hooks.cpp/h`、`visuals.cpp`、`console.cpp/h`。最终全部 248 个项目 C/C++ 文件的 SHA-256 与 `source-format/token-audit.json` 一致；未选择性重加代码。
- `bin/x64/Release/kAzeware.dll` 与配套 PDB 恢复自第 3 节原始产物，已经卸载中间版并加载恢复版。参数快照 `__r8_before_selective_rollback_20261111` 已恢复，结果为 `load result=1`；不可再用更早快照覆盖当前参数。
- 客户端 PID 27464、专用服务器 PID 19480 保持运行。回滚后核对：服务器为 1 名真人、5 个 CT BOT，`mp_warmup_pausetimer=1`、`mp_death_drop_gun=0`、`mp_buy_anywhere=1`、`mp_warmup_items_nocost=true`；用户为 T 方。约 50 ms 的当前网络环境未再调整。
- 客户端命令转发和服务器命令转发辅助 DLL 均正常退出，枚举确认两进程中不再加载 `DTDiagnostics` 目录下的模块。停止诊断日志，删除本轮临时游戏配置 `DTDiagnostics_r8_peek.cfg`；独立诊断记录仍保存在工作区外。
- 本次复用原始产物，未重新编译、未执行新的射击或帧耗时测试。回滚期间的 R8 实验构建和测试不属于当前交付的通过记录。精确恢复同时撤销了本轮尝试的内建诊断接口清理，相关原版遗留仍待单独处理。

### 本轮排版整理（2026-11-11）

- 范围：`kAzeware/source/` 和 `loader/` 下 248 个 C/C++ 文件；保留第三方依赖、参考 dump，以及 `weapon_icon_atlas.hpp`、`undefeated_font.hpp`、`icons.h`、`world_particle_assets.hpp` 四个内嵌资源数据头原样。
- 新增根目录 `.clang-format`，统一缩进、换行、函数和控制流布局，关闭 include 排序、字符串拆分及自动增加括号；不改声明、表达式、常量、条件、调用或执行顺序。
- 限定名的实际代码硬换行已合并，46 处被甩到下一行的语句结束分号已归位；19 处含多个 `//` 的粘连注释行已拆开（URL 保持完整）。`hooks.h` 仍有一处原有空声明 `;`，它不是拆散的语句结束符，故未删除。三处 `constexpr std::` 残片位于旧注释中，不是活动代码，未猜测补全。
- 为 `rage.cpp`、`shots.cpp`、`motion.cpp`、`target.cpp`、`visualise.cpp` 和 `config.h` 的命令阶段、DT 状态、运动预测、扫描缓存及渲染快照补充了说明注释；原业务注释均保留。10 处命名空间结尾注释按实际限定名补全 `Velocity::` 前缀。
- 248 文件逐一核对：非注释代码标记序列与逻辑预处理指令均与备份一致；格式器二次检查通过。Release x64 DLL 和 loader 验证构建通过。
- 同编译路径比较整理前后 DLL：`.text`、`.data`、`.pdata`、`.fptable`、`.rsrc`、`.reloc` 逐字节一致；`.rdata` 仅 4 字节构建调试元数据不同，运行数据无差异。没有新增或修复运行功能，因此未启动游戏帧耗时采样，也没有将历史性能数据当作本轮实测结果。
- 验证产物、原文件备份及检查记录均位于 `C:\Users\Simon\Desktop\DTDiagnostics\source-format\`：`before/`、`files.json`、`token-audit.json`、`machine-code-audit.json`、`rdata-audit.json`、`baseline-dll-build.log`、`formatted-dll-build.log`、`formatted-loader-build.log`。初次解决方案构建因外部中间目录参数未展开而链接失败；改为按工程指定明确目录后构建通过。未改工程构建设置。
- 排版整理当时未替换正式 DLL；本次按用户指定的 18:06 交付点，将该轮外部验证产物恢复至正式路径并加载，当前产物状态以第 3 节为准。原有诊断接口未在当前源码中清理。

### 工程性遗留（功能代码未处理）

历史工程项及当前状态。菜单整理聊天已经移除 ISO 和 AD 临时诊断代码；本轮核对没有恢复它们，也没有向正式源码添加测试采样。通用启动/卸载日志接口与独立测试探针分开记录。

| 事项 | 核对结论 | 建议处理 |
| --- | --- | --- |
| ISO / AD 临时诊断 | 菜单整理已移除 ISO 开关、分支及 `debug/debug.cpp/h`；本轮源码搜索未发现 ISO、ADLog 或外部测试 DLL 入口 | 保持移除状态 |
| 通用初始化/卸载日志 | 仍有 `Con::BootBanner/Tag/Stats/ScopedTimer` 及 DEBUG 日志接口，服务初始化和卸载排错；不是本轮临时采样钩子 | 保留现有通用接口，不将其描述为已删除 |
| 源码畸形排版 | 本轮已整理，范围和核对结果见上节；旧注释中疑似截断的文字未猜测恢复 | 后续沿用 `.clang-format`；不要把注释残片误当作代码补回 |
| 翻译目录污染 | `config/translation_catalog.h` 混入 AOB 签名串、C++ 导出符号及成批 `Enemy /` 重复前缀键；注释所指生成脚本位于被 `.gitignore` 排除的 `_tools/`，工作区内不存在 | 修正提取脚本过滤规则、纳入版本库后重新生成目录 |

澄清上一轮梳理的一处误判：`hooks.h` 中 `GlobalLightUpdate`、`UpdateLightObject`、`GeneratePrimitives`、`DrawAggregateSceneObjectArray` 等声明并非未安装，安装走 `addHook()` 辅助函数（`hooks.cpp:654/665`）及 `forEachHook` 遍历（`hooks.cpp:1478` 起）；`AntiTamper` 在 `hooks.cpp:810` 安装，`815` 行有 insecure 检查。此项不列为遗留。

当前产物以第 3 节为准，为保留菜单重排、多目标/Peek、Extrapolation 并加入 Auto 参数的 `auto-aim-v1`，保持「已注入待用户验收」。其他待办遵循用户后续优先级，不在交付后自动扩展任务。

## 7. 独立证据和诊断注意事项

证据根目录：`C:\Users\Simon\Desktop\DTDiagnostics`。这些文件不属于正式交付依赖，迁移工作区时可能不可用。

| 文件（相对证据根目录） | 用途 |
| --- | --- |
| `auto-md-20261112/build.log`、`final-audit.json`、`source.diff` | Auto MD 最终构建、DLL/PDB 哈希、加载/清理状态及六文件差异。 |
| `auto-md-20261112/policy_test.cpp`、`auto-shots-dt-on.csv`、`auto-shots-dt-off.csv`、`performance.json` | 共用规则边界测试、实际阈值提交记录及枪械 ABBA 性能检查。提交记录含重放。 |
| `auto-md-20261112/auto-menu.png`、`auto-scene.png` | Auto 标签后台截图及已弃用的持刀测试场景。 |
| `extrapolation-20261112/build-v4.log`、`final-audit.json`、`source.diff` | 本轮最终构建、产物哈希、加载/清理状态和功能源码差异。 |
| `extrapolation-20261112/test_forecast.py`、`test_time.py`、`shots.csv`、`server-console.txt` | 外部源码提取测试、外推时钟/滑条提交记录及服务器事件。记录含重放，不表示独立射击数量或零空枪。 |
| `extrapolation-20261112/ab-perf-v4.json`、`v4-perf-before.json`、`v4-perf-after.json`、`slider.png` | 最终构建 ABBA 回归、相同本地位置核对及菜单入口后台截图。 |
| `compat-14197-20261112/build-addresses.log`、`final-audit.json`、`source-changes.json` | 本轮地址更新构建、六个 C/C++ 文件差异、最终 DLL/PDB、实际加载及清理核对。 |
| `compat-14197-20261112/live-schema.json`、`schema-delta.json`、`resolved-globals.json`、`resolved-buttons.json` | 本机当前进程字段与地址提取，不是第三方静态表直接替换。 |
| `compat-14197-20261112/audit-after-addresses.txt`、`audit-fingerprint-negative.txt`、`source-patterns-after.txt` | 当前构建核对、错误二进制指纹应失败的用例及全部字面量模式的扫描范围/限制。 |
| `compat-14197-20261112/native-spread-check.json`、`global-runtime-check.json` | 当前原生扩散返回与 Schema 数据相符；新全局对象与版本/位置/窗口的只读核对。 |
| `compat-14197-20261112/performance-comparison.json`、`frames-*.csv`、`loaded.png` | 自然交战采样及新版加载后 PrintWindow 截图；站位不同，不能作为严格性能提升证明。 |
| `multitarget-20261112/build-v2.log`、`final-audit.json`、`source-changes.json` | 历史 v2 构建、七个功能文件差异及当时加载/清理核对。 |
| `multitarget-20261112/performance-comparison.json`、`perf-*.csv/.json` | 历史 v2 五敌人回归与旧版/v1 窗口；一敌人仅为 v1 历史采样，比较限制见第 4 节。 |
| `multitarget-20261112/scan-test-result.txt`、`segment-audit.txt` | 26 项实际函数体/调度断言；截短路径实验失败记录（未采用）。 |
| `dt-peek-return-20261112/check.ps1`、`observe-v2-summary.json`、`observe-v2.txt` | Peek 的 31 项断言及 v2 原生移动仲裁 PASS、安全卸载记录；CSV 按钮两列无效，不用于通过判断。 |
| `dt-peek-return-20261112/seed-v2.csv`、`seed-v2.txt`、`486857-data-audit.json` | v2 成功双发的生产 seed/精度和射线复算；旧 41 伤害样本无法还原的证据。 |
| `multitarget-20261112/wall-live-v2.json`、`wall-server-v2.txt`、`wall-seed-live-v2.json`、`wall-seed-server-v2.txt` | 两次真实落地墙边开火的移动位置、时机和服务器伤害；分别为 54+41 未归因与 54+54 击杀。 |
| `multitarget-20261112/server-status-final.txt`、`final.png` | 最终训练参数/人数及 v2 清理后 PrintWindow 后台截图。 |
| `multitarget-20261112/server-dt-peek-evidence.txt`、`dt-peek-return-20261112/observe-v1-summary.json` | v1 近距离双发的历史观测及服务器日志，不自动套用到 v2。 |
| `weapon-monitor/five-minute-window.json`、`five-minute-audit.json` | 较早 v5 五分钟采样，不是最终版通过报告。 |
| `weapon-monitor/v7-hitbox-audit.json` | 255 发旧版配对记录及 195 次原生命中回放。 |
| `weapon-monitor/jump-motion-replay.json` | 25 个旧空中命令的速度上限差异回放。 |
| `weapon-monitor/jump-v9-window.json`、`jump-v9-audit.json` | v9 原地跳枪窗口和逐发核对。 |
| `weapon-monitor/jump-moving-v9-window.json`、`jump-moving-v9-audit.json` | v9 移动跳枪窗口和逐发核对。 |
| `weapon-monitor/crouch-v10-window.json`、`crouch-v10-audit.json` | v10 蹲起/侧移补测。 |
| `peek-regression/perf-final-comparison.json` | 性能记录，比较限制见上文。 |
| `peek-regression/final-v10.png` | 交接时 PrintWindow 截图，不是当前实时画面。 |
| `peek-regression/final-clean-audit.json` | 源码引用及 DLL 标记核对。 |
| `r8-peek/rollback-source-audit.json`、`rollback-final-audit.json` | 本次 248 文件精确回滚、DLL/PDB 校验值与实际加载和辅助模块清理结果。 |
| `r8-peek/rollback-server-status.txt` | 回滚后实际服务器人数、BOT 数量与训练参数。 |
| `r8-peek/rollback-1806.png` | 回滚后的 PrintWindow 截图，仅记录当时画面。 |
| `startup-20261112.json`、`startup-20261112.png` | 2026-11-12 重新启动并加载当前 DLL 的状态核对及主菜单后台截图，不是功能验收报告。 |
| `startup-v2-20261112.json`、`startup-v2-20261112-console.txt`、`startup-v2-20261112-launch.json` | JSON 为重启后的最新 PID 14320 状态；console 含两轮设置，launch 是 14:05 旧 PID 4060 启动记录。游戏 1.41.9.7、现有 v2 DLL、五敌方 BOT 训练和辅助模块清理；不包含射击或帧耗时回归。 |
| `resolution-check-20261112.json`、`resolution-relaunch-20261112.json`、`resolution-training-status-20261112.txt`、`resolution-*.png` | 模糊反馈后核对 2560×1600、隐藏菜单/卸载对照、PID 14320 重启及清晰画面、五 BOT 训练和重注入记录；不代表新功能构建或完整游戏版本兼容性回归。 |

本轮 `auto-aim-20261112/final-audit.json` 对应 PID 4692、`auto-aim-v1`；记录正式哈希、184 字节布局、初始化、配置恢复和两端测试模块清理。测试窗口与截图均为后台操作；图片中的 Auto 数值是检查用临时配置，最终已恢复原参数。

诊断工具曾因叠加 Present 钩子、卸载后恢复到已释放跳板而导致游戏退出。重新使用时必须串行：监测、性能采样、眼位探针和配置转移工具不能同时挂接；卸载产品 DLL 前先停止并确认辅助工具退出。不能复用旧 PID、旧 DLL 基址或旧构建 RVA；每次核对当前进程、构建和符号。旧命令转发工具中硬编码的诊断入口也不是通用接口。

最新换版已恢复 `__before_auto_aim_20261112`。`__auto_aim_zero_20261112` 和 `__auto_md_zero_test_20261112` 是全 profile Auto/DT 测试配置，不可覆盖用户参数。`__before_auto_md_20261112` 为前轮快照。旧 `__before_extrapolation_slider_20261112`、其他 extrapolation、compat、multitarget、R8/shotreview 快照仅作历史备份；未来操作前重新保存届时参数。

## 8. 后续维护方式

每轮项目变化结束前主动更新 README 和本文件：记录改动原因与源码入口、实际检查结果及未覆盖范围、剩余问题和用户最新约束。明确区分代码完成、构建通过、实测通过、已注入待验收和用户验收通过。

重建或交付 DLL 时更新构建标签、路径、哈希和加载状态；没有重建时不改变二进制交付记录。只做文档变更时检查链接和记录一致性，不因此编译或操作游戏。
