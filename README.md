# Chronicon Skill Damage Meter

给 **Chronicon（英雄旧忆）** 做的游戏内伤害统计插件：像 WoW 的 Skada / Details! 一样，**每个技能单独一行**，列出伤害、占比、DPS、暴击率，并能点开看命中次数、最大单次和按目标拆分。

> ⚠️ **先说清楚一件事**：Chronicon 是 GameMaker 游戏，**没有 WoW 那种官方 AddOn / Lua 插件系统**，不能丢一个文件夹到 `Interface\AddOns` 里就生效。本项目是一个基于 [YYToolkit / Aurie](https://github.com/AurieFramework/YYToolkit) 的**原生 DLL mod**，通过 Hook 游戏内部函数工作。这是目前社区唯一被验证可行的路线（[atty303/chronicon-dps](https://github.com/atty303/chronicon-dps) 也是这么做的）。

打开 `preview.html` 可以先看到界面和交互效果（单文件，无需联网，数据是 `tests/verify_logic.py` 跑出来的真实聚合结果）。

---

## 1. 功能

| | |
|---|---|
| 按技能分列 | 每一行 = 一个技能 ID，显示总伤害、占比、DPS、暴击率 |
| 条形填充 | 按占比横向填充，技能颜色稳定（按 ID 哈希），一眼看出主力输出 |
| 三种视图 | 当前战斗 / 全部累计（Overall）/ 上一场战斗 |
| 四种排序 | 伤害 / DPS / 命中数 / 暴击率，点表头或按 F8 切换 |
| 展开详情 | 点任意一行，底部显示命中数、平均/最大单次、DPS、按怪物拆分 |
| 战斗分段 | 脱战 4 秒自动结算并归档；F5/F6/F7 控制显隐、重置、视图 |
| 窗口操作 | 拖标题栏移动、滚轮翻页、右上角关闭；位置自动保存 |
| 导出 | F9 导出 `damage_log.csv` + 未知技能 ID 清单；可配置每场战斗自动导出 |
| 名称映射 | `skill_names.txt` 自定义技能名；运行时还会尝试自动学习 |

统计**不依赖技能名**：即使全部显示 `Skill #12`，分组依然是精确的。名称只影响显示。

---

## 2. 原理（为什么能精确到"哪个技能"）

游戏内部有一个 GML 脚本：

```
gml_Script_actorDamage(dmg, crit, element, trigger, attacker, skill)
                        [0]  [1]   [2]      [3]      [4]       [5]
```

- `Self` = 被打的对象，`Args[4]` = 攻击者，**`Args[5]` 就是技能 ID**。
- 插件 Hook 这个函数，先读目标的 `hp`，调用原函数，再读一次 `hp`，用 **HP 实际减少量**作为有效伤害 —— 这样护盾、减伤、溢出伤害都不会被算错（训练假人的 HP 是异常值，会自动回退到 `dmg` 参数）。
- 判断"是不是玩家打的"：把 `Args[4]` 经 GML `real()` 转成实例 ID，和每帧刷新的玩家实例 ID 比较；`Self` 是 `obj_player` 的直接跳过（那是玩家挨打）。
- 另一处 Hook `gml_Script_GUIdraw`，在原 HUD 绘制**之后**画窗口，保证浮在最上层；坐标系按 960×540 虚拟 HUD 缩放，适配任意分辨率。

对比社区已有的 DPS meter：它们 Hook 的是 `gml_Script_floatNumCreate`（伤害飘字），拿不到技能信息，而且必须开启游戏的「显示伤害数字」设置。本项目用 `actorDamage`，**不依赖飘字开关**，也天然带技能 ID。

---

## 3. 安装（先把 DLL 编出来）

### ⚠️ 先看这个：交付包里没有 `ChroniconSkillMeter.dll`

这个项目是**源码**，仓库里只有 `.cpp` / `.hpp` 和 VS 工程，**不含任何 `.dll`**。第 3.2 节里 `Add Mods` 要选的那个文件，必须先编译才会出现——不是你漏找了某个文件夹。

三个容易混在一起的「位置」：

| | 路径 | 谁产生 / 谁使用 |
|---|---|---|
| **编译产物** | `bin\<x64 或 Win32>\Release\ChroniconSkillMeter.dll`（相对项目根目录） | MSBuild 生成 |
| **一键脚本输出**（最好找） | `dist\ChroniconSkillMeter.dll` | 跑 `build.bat` 后自动复制一份到这里 |
| **安装目标** | `<Chronicon 游戏目录>\mods\aurie\ChroniconSkillMeter.dll` | 见 3.4；路线 A 的 `Add Mods` 会自动放对 |

**拿到 DLL 的三条路：**

1. **本地编译（推荐）**：装 [VS 2022 Community](https://visualstudio.microsoft.com/zh-hans/vs/community/)（勾选「使用 C++ 的桌面开发」+ Windows 10/11 SDK），然后双击 **`build.bat`**（默认 x64；32 位游戏用 `build.bat Win32`）。脚本会自动检查依赖、定位 MSBuild、编译，最后把 DLL 复制到 `dist\` 并打印完整路径。
2. **云端编译（本机装不了 VS 时）**：把项目推到自己的 GitHub 仓库，`.github/workflows/build.yml` 会在 `windows-2022` runner 上编译。进 **Actions → Build DLL → Run workflow**，选 `x64` / `Win32`，跑完在 **Artifacts** 里下载 `ChroniconSkillMeter.dll`。本地不需要装任何开发工具。
3. **让别人代编**：把整个项目目录发给有 VS2022 的人，跑一次 `build.bat`，把 `dist\` 里的 DLL 发回来即可。

> 生成这个项目的机器**没有安装任何 C++ 编译器 / Windows SDK**，因此无法在这里预先编译好 DLL。但 `extern/` 下 4 个依赖头文件**已经预先拉好**，你在自己机器上装完 VS 可以直接离线编译。`fetch_deps.ps1` 还加了 `curl.exe` + GitHub contents API 双回退并显式设置 TLS 1.2——`raw.githubusercontent.com` 连不上、或 Windows PowerShell 5.1 的 .NET 网络栈被 403 拦截时会自动换通道（两条通道均已实测）。

### 3.0 先排掉一个坑：`Auri Manager` ≠ `AurieManager`

搜 "Aurie Manager" 很容易装成 **Auri Manager**（少一个 `e`）——那是一款管理音视频/网络设备的软件，界面是 `Devices` / `Credentials` / `System`，按钮是 `Add Device`、`Manage Device Credentials`，列表列是 `IP` / `Serial` / `Firmware`。**它没有 `Add Game`，和游戏 mod 没有任何关系**，直接关掉。

要找的是 GitHub 项目 `AurieFramework/Aurie` 发布的 **`AurieManager.exe`**（有 `e`）。

### 3.1 为什么照着旧教程会找不到 `Add Game`

`Add Game` 是 **Aurie v1.x 的 `AurieManager.exe`** 才有的按钮。社区教程（如 [chronicon-dps](https://github.com/atty303/chronicon-dps)）把下载链接写成了 `.../releases/latest`，但 Aurie 的 latest 现在已经是 **v2.0.2**：

| 版本 | 管理器 | 安装方式 |
|---|---|---|
| v1.1.0 / v1.2.0 / **v1.2.1** | `AurieManager.exe` ✅ 有 `Add Game` | `AurieLoader` + 注册表代理 |
| v2.0.0a / v2.0.0b / **v2.0.2** | ❌ 已移除 | `AuriePatcher.exe` 原地给 exe 打补丁 |

v2 的 release notes 里明确写了 `Removed AurieLoader`、`Added support for in-place patching of the target executable instead of relying on registry hacks`。所以从 "latest" 下载的人，既找不到 `Add Game`，也找不到 `AurieManager.exe`。

### 3.2 路线 A（推荐）：用 v1.2.1 的 AurieManager，就有 `Add Game`

**推荐理由**：本项目的 YYToolkit / Aurie 头文件与 v1.x API 对齐（与 chronicon-dps 完全一致）。v2 改动了模块入口（新增 `ModuleEntrypoint`）、回调校验和 hook 默认行为，本项目尚未针对 v2 适配验证。

1. 打开 [Aurie v1.2.1 release](https://github.com/AurieFramework/Aurie/releases/tag/v1.2.1)，下载：
   - [`AurieManager.exe`](https://github.com/AurieFramework/Aurie/releases/download/v1.2.1/AurieManager.exe)
   - 同页的 `AurieCore.dll`、`AurieLoader.exe`（备用）
   - ⚠️ **不要**点 `releases/latest`，那会是 v2.0.2
2. 运行 `AurieManager.exe`。
3. 点游戏下拉框旁边的 **`Add Game`** → 选择你的 `Chronicon.exe`。
4. 点右下角 **`Install Aurie`**。
5. 点右侧 **`Add Mods`** → 选择 `ChroniconSkillMeter.dll`。
6. 启动游戏，按 **F5** 显示 / 隐藏窗口。

### 3.3 路线 B（进阶）：当前 v2.x

v2 没有管理器 UI，是给 `Chronicon.exe` **原地打补丁**。⚠️ **先复制一份 `Chronicon.exe` 备份**，patcher 会直接改写这个文件。

- 图形化：下载 [`AurieInstaller.exe`（v2.0.0b）](https://github.com/AurieFramework/Aurie/releases/download/v2.0.0b/AurieInstaller.exe)，运行后点 `Select your game executable...` 选 `Chronicon.exe`，它会自动下载组件、建目录、调用 patcher。注意 **v2.0.2 里没有 `AurieInstaller.exe`**，它只在 v2.0.0b 发过。
- 命令行 / 交互模式：下载 [`AuriePatcher.exe`（v2.0.2）](https://github.com/AurieFramework/Aurie/releases/download/v2.0.2/AuriePatcher.exe)。**不带参数双击**即可进入交互模式，依次弹出三个窗口：
  1. `Select game executable...` → 选 `Chronicon.exe`
  2. `Select native DLL...` → 选 `mods\native\AurieCore.dll`
  3. `Action` → 点 **Install**（另一点是 Uninstall）

  等价命令行：`AuriePatcher.exe "Chronicon.exe" "mods\native\AurieCore.dll" install`
- 组件下载：[`AurieCore.dll`(x64)](https://github.com/AurieFramework/Aurie/releases/download/v2.0.2/AurieCore.dll) / [`AurieCore-x86.dll`(x86)](https://github.com/AurieFramework/Aurie/releases/download/v2.0.2/AurieCore-x86.dll)，`YYToolkit.dll` 需从 [YYToolkit releases](https://github.com/AurieFramework/YYToolkit/releases) 单独取。

> v2 仍需按下节的 `mods\aurie\` 目录放本插件；且因为 API 变动，若编译/加载报错需按 v2 头文件适配 `Gml.cpp` 与 `ModuleMain.cpp` 的入口导出。

### 3.4 DLL 到底放哪个目录（关键）

Aurie v2 的目录结构（对照官方 `AurieInstaller` 与 `AurieMain.cpp`）：

```
Chronicon/
├─ Chronicon.exe                    ← v2 的 patcher 会原地改写，务必备份
└─ mods/
   ├─ native/   AurieCore.dll       ← 由 patcher 指定，普通 LoadLibrary
   ├─ aurie/    YYToolkit.dll
   │            ChroniconSkillMeter.dll   ★ 放这里
   ├─ bin/      AuriePatcher.exe
   └─ managed/  （AurieSharp 用，本项目不需要）
```

依据 `Aurie/source/AurieMain.cpp` 里 `ArProcessAttach` 的两段加载逻辑：

```cpp
fs::path native_path = game_folder / "mods" / "native";  // LoadLibraryW 直接载入
game_folder          = game_folder / "mods" / "aurie";   // MdpMapFolder -> ModuleInitialize
```

本项目导出的是 `ModuleInitialize` 并调用 `MmCreateHook` / `ObGetInterface` 等 Aurie API，属于 **Aurie 模块**，必须放在 **`mods\aurie\`**。放进 `mods\native\` 只会被 `LoadLibrary` 载入，`ModuleInitialize` 永远不会被调用——表现为「游戏正常，但没窗口也没数据」。路线 A 的 `Add Mods` 会帮你放到正确位置。

### 3.5 架构必须一致（另一个高频坑）

DLL 位数与 `Chronicon.exe` 不一致会静默加载失败；`AuriePatcher` 还会直接报 `Image architecture is not supported`。先查游戏是 32 位还是 64 位：

```powershell
$p = "C:\Program Files (x86)\Steam\steamapps\common\Chronicon\Chronicon.exe"
$b = [IO.File]::ReadAllBytes($p)
$pe = [BitConverter]::ToInt32($b, 0x3C)
$machine = [BitConverter]::ToUInt16($b, $pe + 4)
switch ($machine) { 0x8664 { "64-bit -> Release|x64 + AurieCore.dll" } 0x14C { "32-bit -> Release|Win32 + AurieCore-x86.dll" } default { "unknown 0x{0:X}" -f $machine } }
```

或者：启动游戏后在任务管理器 → 详细信息，进程名后带 `(32 位)` 即为 x86。参考项目 chronicon-dps 同时提供 Win32 / x64 两套配置，本项目 `.sln` 也已补上 `Win32` / `x64` 两套，按上表选对应配置编译即可。

> 游戏版本：本项目依据 Chronicon 1.5x 的 GML 脚本名编写。若游戏大版本更新导致脚本改名，窗口能出来但没有数据，请看第 8 节排查。

---

## 4. 从源码编译

需要 **Visual Studio 2022（桌面 C++ 工作负载，MSVC v143）+ Windows 10 SDK**，**C++20**；平台 `x64` 或 `Win32` 按第 3.5 节查到的游戏架构选，**必须一致**。

**最省事：双击 `build.bat`**，它把下面所有步骤一次做完，并把 DLL 复制到固定的 `dist\` 目录：

```bat
cd ChroniconSkillMeter
build.bat                :: 一键：查依赖 → 找 MSBuild → 编译 → 复制到 dist\
build.bat Win32          :: 32 位游戏用这个（默认 x64）
```

产物：**`dist\ChroniconSkillMeter.dll`**（MSBuild 原始产物在 `bin\<平台>\Release\ChroniconSkillMeter.dll`）。没有 VS 的话改用第 3 节的 GitHub Actions 云端编译。

手动编译的等价步骤：

```bat
cd ChroniconSkillMeter
fetch_deps.bat                      :: 拉取 YYToolkit / Aurie 头文件到 extern\
:: 若 GitHub 不通：  powershell -NoProfile -ExecutionPolicy Bypass -File .\fetch_deps.ps1 -Mirror "https://ghproxy.net/"
:: 或手动把 atty303/chronicon-dps 仓库里的 ChroniconDPS\include 整个目录复制到 extern\
start ChroniconSkillMeter.sln       :: 选择 Release | x64 或 Release | Win32，生成
```

`extern/` 需要最终长这样（布局与已验证的 chronicon-dps 一致）：

```
extern\YYToolkit\Shared.hpp
extern\YYToolkit\Shared.cpp
extern\Aurie\shared.hpp
extern\FunctionWrapper\FunctionWrapper.hpp
```

把 DLL 放进 **`mods\aurie\`**（见第 3.4 节）后，同目录会生成 `skillmeter.ini`、`unknown_skills.txt`、`damage_log.csv`。

---

## 5. 快捷键

| 键 | 作用 |
|---|---|
| **F5** | 显示 / 隐藏统计窗口 |
| **F6** | 清空所有数据（当前战斗 + 历史 + 滑动窗口） |
| **F7** | 循环切换视图：当前战斗 → 全部累计 → 上一场 |
| **F8** | 循环切换排序：伤害 → DPS → 命中数 → 暴击率 |
| **F9** | 导出 CSV + 写出未命名技能 ID |
| 鼠标 | 拖标题栏移动；滚轮翻页；点行展开详情；点 `[CURRENT]` 切视图；点 `x` 关闭 |

按键是 Windows VK 码，可在 `skillmeter.ini` 里改（`F5=116 … F9=120`）。

---

## 6. 配置（`skillmeter.ini`）

首次运行会自动生成带注释的默认文件。常用项：

| 项 | 默认 | 说明 |
|---|---|---|
| `x / y / width / height` | 18 / 96 / 420 / 336 | 窗口位置尺寸（虚拟 960×540 坐标） |
| `rowHeight / maxRows` | 22 / 10 | 行高、最大行数（实际行数还受窗口高度限制） |
| `fightGapMs` | 4000 | 多久没伤害算脱战，结束当前战斗并归档 |
| `windowMs` | 5000 | 「LAST 5s」滑动窗口长度 |
| `maxFights` | 30 | 最多保留多少场历史 |
| `countMinions` | 0 | 开启后把非玩家来源的伤害归到 `Minions / Other` |
| `exportCsv` | 0 | 开启后每场战斗结束自动覆盖写 `damage_log.csv` |
| `learnSkillNames` | 1 | 是否尝试运行时自动学习技能名 |
| `view / sort` | 0 / 0 | 启动时的视图与排序 |

窗口拖动结束后会自动写回 ini（会覆盖掉手写注释，属正常行为）。

---

## 7. 技能名：唯一的已知短板

游戏没有公开「技能 ID → 名称」的查询接口，所以插件提供三层方案：

1. **自动学习**（默认开）：尝试 Hook `skillGetName` 一类脚本，并探测可能的全局名称数组。找到就用，找不到完全无副作用。**尽力而为，不保证成功。**
2. **`skill_names.txt`**（推荐）：照着 `config/skill_names.example.txt` 的格式写 `12=Frost Nova`。文件里的名字优先级最高。
3. **`Skill #N` 兜底**：即使没有名字，分组、占比、DPS 全部照常精确统计。

插件会把所有见过的未命名 ID 定期写进 `unknown_skills.txt`（约每 30 秒，或按 F9 立即写）。**建立映射的实操办法**：只放一个技能打一下，看哪一行数字在涨，就把那个 ID 填上。

> 游戏 HUD 字体是 `font_small_GUI_latin`，**不支持中文**。技能名请用英文/拼音，否则窗口里会显示成方框。README 和 ini 注释是中文没问题，因为它们不进游戏画面。

同时脚本约定：`-1` = 普攻 / 被动 / 装备触发等无技能伤害，`-2` = 召唤物等其他来源。

---

## 8. 统计口径与已知限制

- **DPS** = 该技能总伤害 ÷ 战斗活跃时长（第一下到最后一下，最少按 1 秒计）；`LAST 5s` 是固定 5 秒滑动窗口，两种口径不同，不要直接对比。
- **占比** = 技能伤害 ÷ 当前视图总伤害。
- **Overall** 会把所有历史战斗 + 当前战斗合并，活跃时长取各场之和，避免挂机时间拉低 DPS。
- **只统计玩家造成的伤害**；玩家**受到**的伤害不计入（另有 death recap 需求可自行扩展 `HookActorDamage` 的 `toPlayer` 分支）。
- 需要游戏里存在 `gml_Script_actorDamage` 与 `gml_Script_GUIdraw` 这两个脚本；版本更新改名会导致采集或绘制失效。
- 这是一个**注入式 mod**，仅用于单机/离线统计。联机或官方排行榜场景请勿使用；Aurie 本身也可能被反作弊视为第三方注入。
- **窗口根本不出现**，按这个顺序排查：① DLL 是否在 `mods\aurie\` 而不是 `mods\native\`（放错则 `ModuleInitialize` 不被调用，见 3.4）；② DLL 与游戏的 x86/x64 是否一致（见 3.5）；③ Aurie 日志控制台里是否打印了 `[ArProcessAttach] Loaded ...` / 模块初始化失败 status；④ 用的是 v1.2.1 还是 v2（v2 需按 3.3 适配入口）；⑤ `skill_names.txt` 是否误用中文导致绘制异常。
- 若窗口出来了但没数据：先看 Aurie 控制台的 `[SkillMeter]` 报错；再确认 `unknown_skills.txt` 是否生成；最后检查 `countMinions` 与玩家实例识别（切地图/重开角色后玩家实例 ID 会变，插件每 20 帧刷新一次）。

---

## 9. 验证情况

- `tests/verify_logic.py`：可直接运行的**统计逻辑等价实现**，断言与 `tests/meter_test.cpp` 完全一致（聚合、占比、暴击、分段、滑动窗口、排序、格式化）。本机执行结果：全部通过。
- `tests/meter_test.cpp`：C++ 单元测试，在有 MSVC/MinGW 的机器上可编译运行：
  ```
  cl /std:c++20 /EHsc /I ChroniconSkillMeter\source tests\meter_test.cpp ChroniconSkillMeter\source\Meter.cpp /Fe:meter_test.exe
  ```
- **本机没有 C++ 编译器**，因此 DLL 未在此环境实际编译/注入验证；所有引擎侧调用均对照两个已公开的可用 mod 源码（`atty303/chronicon-dps`、`falorfrozen-cmd/ChroniconPlus`）核对过函数名与参数顺序。首次编译若有报错，通常是 `extern/` 头文件版本差异，按报错调整 `Gml.cpp` 即可。
- 新增的 `Win32` 配置只是把 `x64` 的编译/链接选项复制了一份并切换平台（对照 chronicon-dps 的 vcxproj），同样**未经实机编译**；若 32 位下 YYToolkit 头文件里的指针宽度断言报错，按报错调整 `Gml.hpp` 的 `static_assert` 即可。
- 安装章节（第 3 节）已按 Aurie `v1.2.1` / `v2.0.2` 的实际 release 资产与 `AurieMain.cpp`、`AurieInstaller` 源码逐条核对，但这些流程同样**未在本机实机执行**。
- `extern/` 的 4 个依赖头文件已通过 GitHub contents API **实际下载并解码成功**（`Shared.hpp` 73619 B、`Shared.cpp` 7585 B、`Aurie/shared.hpp` 16118 B、`FunctionWrapper.hpp` 2114 B）。`fetch_deps.ps1` 的三条通道都实测过：raw 直连成功、`curl.exe` + API 回退成功、PowerShell 5.1 直连 API 会 403（已用前两条绕开）。
- `build.bat` **实机跑到「Locating MSBuild」**：依赖检查通过，并正确报出本机缺 `vswhere.exe` / VS2022；因无编译器，后半段编译与 `dist\` 复制未实机验证。`.github/workflows/build.yml` 经 `pyyaml` 解析校验（6 个步骤、触发器和 shell 均正确），但**未在 GitHub Actions 上实跑**。
- 结论：**DLL 必须由你在有 VS2022 的机器上、或用 GitHub Actions 编出来**；本仓库只提供源码与工程。

---

## 10. 文件清单

```
ChroniconSkillMeter/
├─ README.md                     本文件
├─ preview.html                  界面与交互预览（单文件，浏览器打开）
├─ build.bat                     一键编译 → dist\ChroniconSkillMeter.dll
├─ fetch_deps.bat / .ps1         拉取 YYToolkit / Aurie 头文件（含 API 回退）
├─ .github/workflows/build.yml   云端编译 DLL，产出 Actions Artifact
├─ dist\                         编译后生成，DLL 成品固定放这里
├─ extern\                       YYToolkit / Aurie 头文件（本副本已预先拉好）
├─ ChroniconSkillMeter.sln       VS 2022 解决方案（x64 + Win32）
├─ config/skill_names.example.txt
├─ tests/
│  ├─ meter_test.cpp             C++ 单元测试
│  └─ verify_logic.py            可直接跑的逻辑验证 + 示例数据
└─ ChroniconSkillMeter/
   ├─ ChroniconSkillMeter.vcxproj
   └─ source/
      ├─ Meter.hpp/.cpp          统计核心（纯 C++，零游戏依赖）
      ├─ Config.hpp/.cpp         配置读写
      ├─ Gml.hpp/.cpp            YYToolkit 封装（引擎调用、输入、绘制）
      ├─ SkillNames.hpp/.cpp     技能 ID → 名称
      ├─ Overlay.hpp/.cpp        Skada 风格窗口
      └─ ModuleMain.cpp          入口 + 两个 Hook
```

---

## 11. 参考

- [YYToolkit / Aurie Framework](https://github.com/AurieFramework/YYToolkit) — GameMaker 游戏的 mod 框架
- [atty303/chronicon-dps](https://github.com/atty303/chronicon-dps) — 验证可用的 Chronicon DPS meter，本项目沿用其 Hook 与头文件；其 README 的 `Add Game` 流程对应 Aurie **v1.x**
- [falorfrozen-cmd/ChroniconPlus](https://github.com/falorfrozen-cmd/ChroniconPlus) — 提供了 `actorDamage` 六参数签名与玩家实例识别的参考
- [Aurie v1.2.1 release](https://github.com/AurieFramework/Aurie/releases/tag/v1.2.1) — 最后一个带 `AurieManager.exe` / `Add Game` 的版本
- [Aurie v2.0.2 release](https://github.com/AurieFramework/Aurie/releases/tag/v2.0.2) — 当前的 `AuriePatcher.exe` 方案
- [Aurie Installer / Patcher 源码](https://github.com/AurieFramework/Aurie/tree/master/AurieInstaller) — `mods\aurie` / `mods\native` 目录结构与 patch 流程的依据
