# kAzeware

Windows x64、C++23、Direct3D 11 / ImGui 项目，包含 Rage、Legit、视觉、换肤、移动辅助、投掷物预测、配置和翻译系统。

当前版本 **alpha-0.0.1**（2026-11-12）。开发前阅读 [AGENTS.md](AGENTS.md) 与 [HANDOFF.md](HANDOFF.md)。[历史记录](docs/HISTORY.md) 保留旧构建的验证边界。

## 本版变化

- Rage 改为左侧 Config、中间 Accuracy、右侧 Rage Aimbot / Anti-Aim。`+` 搜索并添加武器或武器组；手持武器置顶；右键确认移除覆盖项，Global 永久保留。侧栏依次显示 Global、组、武器。
- 只有 Accuracy 按武器或组覆盖，优先级为武器 → 组 → Global。Rage Aimbot 和 Anti-Aim 全武器共用 Global；旧配置的 Accuracy 继续读取，旧子配置的 Aim / AA 数值不再生效。
- Hitboxes 和 Multipoint 位于 Accuracy。**MP 齿轮常驻**，包含 `MP per hitbox` 和八个部位的比例；使用 Auto 时显示自动 MP 策略。HC 齿轮仅在 HC=Auto 时显示。
- Quick Peek 默认保留等待 DT 第二发的策略。齿轮中可开启 `Only wait for same-target DT`：只为同一目标的第二发等待；不符合时按原回撤触发条件开始回拉，并保持回撤优先级。
- Quick Peek 通过既有玩家碰撞、台阶和地面支撑计算处理斜面 / 楼梯，回撤速度考虑高差；去掉了高差超过 72u 就取消的限制。
- 菜单标志右侧与 DLL 文件版本统一显示 `alpha-0.0.1`。

## 保留能力与验证状态

- MD=0 为 Auto：DT 开启且真实就绪时采用候选敌人半血（向上取整），否则完整血量。HC=0、MP=0 均为 Auto；HC=0 不再隐式启用 NS，MP=0 不再关闭多点。自动策略提供均衡 / 可靠性 / 时机。
- Extrapolation 控制射击外推，量为 0–8 ticks；Movement extrapolation 独立控制急停、开镜和 Visualise Peek 的敌人外推。继续检验 MD、HC、穿墙和目标优先级。
- CS2 1.41.9.7 / build 14197 已核对当前地址和关键签名；参考 dump 不等于后续游戏版本兼容保证。此前 R8 专属预热优化仍保持回滚。
- alpha-0.0.1 已构建并加载至本地 `-insecure` 对局，完成配置继承 / 保存、菜单操作、回撤碰撞和有限实测。可比五 BOT 场景中，平均帧耗时从 3.816ms 到 3.908ms，P95 从 5.216ms 到 5.292ms，未观察到新增显著开销。
- **本版等待用户验收。** 当前测试不等于所有武器、网络环境或全部页面通过；AA 非 180° 服务器朝向、Visualise 和其他历史未验收项见 [HANDOFF.md](HANDOFF.md)。

## 下载与目录

[alpha-0.0.1 Release](https://github.com/KaguraWang/kAzeware/releases/tag/alpha-0.0.1) 提供 `kAzeware-alpha-0.0.1-standalone.dll`。DLL 静态链接 CRT，字体和命中音内嵌，不需配套 PDB 或外置音效。仍依赖 Windows 系统组件和当前游戏。

SHA-256：`2355f72e2ff0875fd6fa4b65fa5b5bb8e71e7bfdd9842cf570d5e091186f9d89`

```text
kAzeware.sln
kAzeware/
  kAzeware.vcxproj           DLL 工程
  external/                 第三方源码依赖
  resources/                内嵌资源
  source/main.cpp           DLL 入口、渲染、输入与卸载
  source/cs2/               游戏实体 / SDK
  source/kAzeware/           功能、菜单、配置与接口
loader/                     可选加载器工程
translations/               英文 / 简体中文 .kt1 包
dumper/output/              游戏 SDK 参考快照
tools/audit_cs2_current.py   当前地址 / 签名审计
docs/HISTORY.md             历史构建记录
README.md                   工程入口
HANDOFF.md                  当前改动、验证与限制
```

`bin/`、`obj/` 是本地构建输出；个人参数、缓存、诊断工具和测试记录不纳入源码仓库。DLL 通过 Release 分发。

## 构建与运行

本机验证环境：Visual Studio Build Tools v145、Windows SDK 10.0.26100.0、DirectX SDK June 2010。工程中保留 DirectX SDK 的默认安装目录；其他环境按实际安装位置调整。第三方源码位于 `kAzeware/external/`。

在根目录 PowerShell 执行：

```powershell
$env:CL='/FS'
& 'C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/MSBuild/Current/Bin/MSBuild.exe' kAzeware.sln /m:1 /t:Build /p:Configuration=Release /p:Platform=x64 /p:MultiProcessorCompilation=false /v:minimal /nologo
```

DLL 输出 `bin/x64/Release/kAzeware.dll`；解决方案另含可选的 `kAzeware-loader.exe`。本版仅重新构建并发布 DLL。替换已加载版本前先正常卸载；PDB 仅供开发，必须与 DLL 来自同一次构建。

`Insert` 开关菜单，`F4` 正常卸载。配置和翻译保存在 `Documents/kAzeware/Configs/`；投掷点位在 `Documents/kAzeware/GrenadeHelpers/`，日志、缓存和崩溃记录位于 `Documents/kAzeware/`。首次运行使用默认配置，翻译包需在配置页选择并应用。

地址审计：

```powershell
python tools/audit_cs2_current.py --game-dir 'D:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game'
```

审计核对二进制指纹、源码字段、关键签名和预测兜底地址；游戏更新后旧指纹应失败。`dumper/output/info.json` 记录快照刷新范围，未重建的枚举和接口表仍是参考。

## 维护

遵守根目录 `.clang-format` 与 AGENTS.md。第三方依赖、参考 dump、内嵌资源头不做机械格式重排。每次功能变化同步更新 README.md / HANDOFF.md；构建通过、有限实测、已加载待验收和用户验收通过必须分别记录。测试专用探针及入口不得进入正式源码和 DLL。

保留 [MIT 授权及上游版权](LICENSE)；第三方依赖沿用各自许可证。
