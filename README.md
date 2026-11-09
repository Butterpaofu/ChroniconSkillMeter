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

> ✅ **本机当前状态（2026-11-09）**：DLL 已经通过 GitHub Actions 编好并放进游戏目录，运行时也已换成与头文件匹配的版本。**现在只需要重新启动一次游戏**，按 **F5** 看窗口。若仍然闪退，直接跳到 **3.8**。
>
> 一键重装 / 回滚固定版运行时（推荐先记住这条命令）：
> ```powershell
> cd "C:\Users\FIREBAT\WorkBuddy AI\2026-11-09-08-54-10\ChroniconSkillMeter"
> powershell -NoProfile -ExecutionPolicy Bypass -File .\install_runtime.ps1
> ```
> 当前 `mods\` 布局：`AurieLoader.exe`（Aurie v1.0.5）、`native\AurieCore.dll`（v1.0.5）、`aurie\YYToolkit.dll`（**v3.2.3**）、`aurie\ChroniconSkillMeter.dll`（本项目 x64）。旧栈完整备份在 `<游戏目录>\_ChroniconSkillMeter_backup\`。

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
2. **云端编译（本机装不了 VS 时）**：把项目推到自己的 GitHub 仓库，`push` 会自动触发 `.github/workflows/build.yml`，在 `windows-2022` runner 上编译，跑完在 **Artifacts** 里下载 `ChroniconSkillMeter.dll`。本地不需要装任何开发工具。**完整手把手步骤见 3.6**（本地仓库已初始化好，只差建仓 + 推送）；32 位游戏用 `Run workflow` 选 `Win32`。
3. **让别人代编**：把整个项目目录发给有 VS2022 的人，跑一次 `build.bat`，把 `dist\` 里的 DLL 发回来即可。

> 生成这个项目的机器**没有安装任何 C++ 编译器 / Windows SDK**，本地 `build.bat` 跑不通；DLL 是用 **GitHub Actions** 云端编译后下载到 `dist\` 的（见第 9 节）。`extern/` 下 4 个依赖头文件**已经预先拉好**，在有 VS 的机器上可以直接离线编译。`fetch_deps.ps1` 还加了 `curl.exe` + GitHub contents API 双回退并显式设置 TLS 1.2——`raw.githubusercontent.com` 连不上、或 Windows PowerShell 5.1 的 .NET 网络栈被 403 拦截时会自动换通道（两条通道均已实测）。

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

### 3.2 路线 A：用 v1.2.1 的 AurieManager，就有 `Add Game`

**为什么用它**：本项目的 YYToolkit / Aurie 头文件与 v1.x API 对齐（与 chronicon-dps 完全一致）。v2 改动了模块入口（新增 `ModuleEntrypoint`）、回调校验和 hook 默认行为，本项目尚未针对 v2 适配验证。

> 🚨 **但 `AurieManager` 只会用来建 `Add Game` / IFEO 启动项，不要用它下载的那套 YYToolkit**。v1.2.1 配套的 YYToolkit 是 **v4.0.1**，与本项目（v3 头文件）**ABI 不兼容，装上去必定闪退**。安装时请一律以 **3.8** 的固定版本组合为准，最省事的做法是直接跑 `install_runtime.ps1`。

1. 打开 [Aurie v1.2.1 release](https://github.com/AurieFramework/Aurie/releases/tag/v1.2.1)，下载：
   - [`AurieManager.exe`](https://github.com/AurieFramework/Aurie/releases/download/v1.2.1/AurieManager.exe)
   - 同页的 `AurieCore.dll`、`AurieLoader.exe`（备用）
   - ⚠️ **不要**点 `releases/latest`，那会是 v2.0.2
2. 运行 `AurieManager.exe`。
3. 点游戏下拉框旁边的 **`Add Game`** → 选择你的 `Chronicon.exe`。
4. 点右下角 **`Install Aurie`**。
5. 点右侧 **`Add Mods`** → 选择 `ChroniconSkillMeter.dll`。
6. 启动游戏，按 **F5** 显示 / 隐藏窗口。

> ⚠️ `Install Aurie` 会从 GitHub **联网下载**组件（`AurieLoader.exe`、`AurieCore.dll`、`YYToolkit.dll`）。国内网络常常下载失败：此时界面可能没报错，但注册表已经写好、文件却没落地，游戏就再也启动不了，报「启动游戏错误：游戏可执行文件缺失」。见 **3.7** 的诊断与离线补齐方法。
>
> ⚠️ 即使下载成功，它拿到的也是 **YYToolkit v4.0.1**，会让本项目闪退。请务必在装完后按 **3.8** 换成 v3.2.3。

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

> 本机实测 `Chronicon.exe` 版本 **1.4.0.0**、PE `0x8664`（x64）、62981120 B，所以应选 `Release|x64` + `AurieCore.dll`。GML 脚本名来自 chronicon-dps 所用的版本；若游戏大版本更新导致脚本改名，窗口能出来但没有数据，请看第 8 节排查。

### 3.6 云端编译：从推送到下载 DLL 的完整步骤

**已经替你做好的部分**（不用重做）：项目目录已 `git init` 并建好首个提交，分支 `main`；`.gitignore` 已排除编译产物与第三方头文件；`.github/workflows/build.yml` 已就位；`push_to_github.bat` 把推送压成一条命令。

#### 第 1 步：在 GitHub 网页建一个空仓库

1. 登录 [github.com](https://github.com)（没有账号先注册）。
2. 右上角 **`+`** → **`New repository`**。
3. `Repository name` 填例如 `ChroniconSkillMeter`；选 **Public**（Public 的 Actions 完全免费；Private 也行，有每月 2000 分钟免费额度）。
4. ⚠️ **下面三个都不要勾**：`Add a README file`、`Add .gitignore`、`Choose a license`。仓库必须**完全空**，否则推送会因历史冲突被拒。
5. 点 **`Create repository`**，在随后页面复制形如 `https://github.com/你的用户名/ChroniconSkillMeter.git` 的 HTTPS 地址。

#### 第 2 步：推送（一条命令）

> ⚠️ 最常见的报错：`'push_to_github.bat' 不是内部或外部命令`。原因只是 **cmd 当前目录不对**（工作区是两层目录，容易停在外层），或在 PowerShell 里少了 `.\`。脚本本身在 `ChroniconSkillMeter\` 里，不在外层工作区。

**最省事：用完整路径，从任意目录都能跑。** `Win + R` → 输入 `cmd` 回车，粘贴：

```bat
"C:\Users\FIREBAT\WorkBuddy AI\2026-11-09-08-54-10\ChroniconSkillMeter\push_to_github.bat" https://github.com/你的用户名/ChroniconSkillMeter.git
```

或者先切到项目目录：

```bat
cd /d "C:\Users\FIREBAT\WorkBuddy AI\2026-11-09-08-54-10\ChroniconSkillMeter"
push_to_github.bat https://github.com/你的用户名/ChroniconSkillMeter.git
```

PowerShell 里必须带 `.\` 或 `&`：

```powershell
& "C:\Users\FIREBAT\WorkBuddy AI\2026-11-09-08-54-10\ChroniconSkillMeter\push_to_github.bat" https://github.com/你的用户名/ChroniconSkillMeter.git
```

**也可以直接双击 `push_to_github.bat`**：没有参数时它会提示你粘贴仓库 URL，跑完会 `pause` 等你按键，不会一闪而过。

推送前只想自检环境（不联网、不推送）：

```bat
push_to_github.bat --check
```

自检会打印脚本绝对路径、找到的 `git.exe`、当前分支、remote、最新提交和 workflow 是否存在。

> **本机特别注意：系统 PATH 里没有 `git`**，只有 WorkBuddy 自带的便携版 Git。所以手写 `git remote add ...` 会报 `'git' 不是内部或外部命令`。脚本会自动定位 `C:\Program Files\WorkBuddyAI\resources\vendor\PortableGit\cmd\git.exe`，并临时把 `cmd` 与 `mingw64\bin` 加进 PATH —— 后者是必需的，否则 `git-credential-manager.exe` 会因找不到 `git.exe` 而崩溃，登录窗口弹不出来。**统一用脚本推送，不要手写 git 命令。**

等价的手写命令（仅在你已装好系统级 Git 时可用）：

```bat
git remote add origin https://github.com/你的用户名/ChroniconSkillMeter.git
git push -u origin main
```

首次推送会弹 **Git Credential Manager 的浏览器登录窗口**（本机已装），登录授权即可。若窗口没弹出、直接要求输入密码：去 GitHub → `Settings` → `Developer settings` → `Personal access tokens` 生成一个勾选 `repo` 的 token，**用 token 当密码**（GitHub 已不接受账号密码）。

#### 兜底：完全不用 git，也能触发 Actions

如果登录/推送始终失败，可以走纯网页路线，效果一样（`push:` 换成 `Create`/`Add file` 同样会触发 `.github/workflows/build.yml`）：

1. 把工作区根目录的 **`ChroniconSkillMeter-upload.zip`** 解压（它由 `git archive HEAD` 生成，只含仓库应提交的文件，已自动排除 `extern/` 第三方头文件、`bin/`、`obj/`、`dist/`）。
2. 在 GitHub 空仓库页点 **`uploading an existing file`**。
3. 把解压出来的**全部内容**（含隐藏的 `.github` 文件夹、`.gitignore`）一起拖进上传区。
4. 填 commit message，点 **`Commit changes`**，然后按第 3 步去 Actions 看构建。

> 注意：不要把 zip 本身拖进去（GitHub 会当成一个压缩文件上传，不会自动解压）。必须先解压再拖内容。


#### 第 3 步：等自动构建

工作流里配了 `push:` 触发，所以**推送完成就自动开始编译**，不用手动点。

1. 打开仓库页 → 顶部 **`Actions`** 标签。
2. 左侧点 **`Build DLL`**，右侧会看到一次运行（黄色圆点 = 进行中，约 2~5 分钟，首次拉镜像略久）。
3. 绿色勾 = 成功；红色叉就点进去看日志，把报错贴给我。

> **32 位游戏**：push 默认编 `x64`。需要 Win32 时在 `Build DLL` 页点右侧 **`Run workflow`** → 平台选 **`Win32`** → **`Run workflow`**，等它单独跑完。

#### 第 4 步：下载 DLL

1. 进那次成功的运行页面，往下滚到 **`Artifacts`** 区块。
2. 点 **`ChroniconSkillMeter-x64`**（或 `-Win32`）下载 zip。
3. 解压得到 **`ChroniconSkillMeter.dll`**。

#### 第 5 步：装进游戏

把 DLL 放进 `<Chronicon 游戏目录>\mods\aurie\`（见 3.4），或用 `AurieManager` 的 `Add Mods` 选中它；启动游戏按 **F5**。

#### 常见问题

| 现象 | 处理 |
|---|---|
| `'push_to_github.bat' 不是内部或外部命令` | 当前目录不对。改用第 2 步的**完整路径**运行，或先 `cd /d "...\ChroniconSkillMeter"`；PowerShell 用 `& "完整路径"` |
| `'git' 不是内部或外部命令` | 本机没有系统级 Git。必须用 `push_to_github.bat`（它会自动定位便携 Git），或改用上面的「完全不用 git」兜底方案 |
| `Failed to locate 'git.exe'` / 登录窗口不弹 | Git Credential Manager 找不到 git。脚本已自动补 PATH；若仍出现，先跑 `push_to_github.bat --check` 看 `GitRoot` 是否正确 |
| `push` 报 `rejected` / `non-fast-forward` | 建仓时勾了 README。执行 `git pull --rebase origin main` 后重新 `git push -u origin main` |
| Actions 里看不到 `Build DLL` | 确认 `.github/workflows/build.yml` 在仓库根目录的这一路径；再去 `Settings` → `Actions` → `General` → 选 `Allow all actions and reusable workflows` |
| 编译报 `C1083: Cannot open include file` | 看 `Fetch YYToolkit / Aurie headers` 那步的日志；工作流已带 raw 三次重试 + GitHub API 回退 |
| `Build` 步骤红叉、只显示 `Process completed with exit code 1.` | 现在会把 MSVC 的 `error Cxxxx` 原样发成 **Annotations**，并额外上传 **`build-log`** artifact（`msbuild.log`）。点开运行的 Annotations 或下载该 artifact 就能看到具体报错 |
| 报错 `'m_Object': is not a member of 'YYTK::CInstance'` | 旧版源码直接读了 GameMaker 内部结构。当前版本已改为通过 `variable_instance_get` / `asset_get_index` 等 GML 内建函数判断，不要回退这段改动 |
| 要两个平台的 DLL | 默认 push 出 x64，再手动 `Run workflow` 选 `Win32` 各跑一次 |
| 推送报 TLS / `schannel` 错误 | 重试；或改用 GitHub Desktop / 网页上传（注意 `.github` 是隐藏目录） |
| 游戏启动后立刻闪退，事件查看器显示 `YYToolkit.dll 4.0.1.0` + `0xc0000005` | 版本错配：本插件是 v3 头文件编译的，必须配 **YYToolkit v3.2.3 + Aurie v1.0.5**。见 **3.8**，跑 `install_runtime.ps1` 一键换掉 |
| `install_runtime.ps1` 提示「禁止运行脚本」 | PowerShell 执行策略限制，命令前加 `-ExecutionPolicy Bypass`（README 里的示例已经带了） |

### 3.7 Steam 报「启动游戏错误：游戏可执行文件缺失」

**这既不是 DLL 的问题，也不是游戏文件损坏**，而是 Aurie v1 的 **IFEO 启动劫持残留**。`Install Aurie` 会写：

```
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\Chronicon.exe
  UseFilter = 1, IsAurieInstallerKey = 1
  子键 D:_SteamLibrary_steamapps_common_Chronicon_Chronicon.exe   (路径把 \ 换成 _)
    FilterFullPath = D:\SteamLibrary\steamapps\common\Chronicon\Chronicon.exe
    Debugger       = D:\SteamLibrary\steamapps\common\Chronicon\mods\AurieLoader.exe
```

Windows 每次启动 `Chronicon.exe` 都会先执行 `Debugger` 指向的 loader。一旦 `mods\` 目录不存在（联网下载失败、手动删除、Steam 更新清理），Windows 找不到这个 exe，Steam 就报「游戏可执行文件缺失」——**哪怕 `Chronicon.exe` 本身完好无损**。

自查（PowerShell）：

```powershell
$k = "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\Chronicon.exe"
Get-ChildItem $k -Recurse | ForEach-Object {
  $i = Get-Item $_.PSPath
  $d = $i.GetValue('Debugger')
  "Debugger = $d"
  "Exists   = $(Test-Path -LiteralPath $d)"
}
```

`Debugger` 有值但 `Exists = False` 就是它。

**修复二选一：**

- **A. 只想恢复原版**：运行 `AurieManager.exe` → 选中 Chronicon → **`Uninstall Aurie`**，清掉 IFEO 即可。
- **B. 继续用 mod（离线补齐）**：按下面结构放好 4 个文件，让 `Debugger` 目标存在。`AurieLoader` 的 `MapFolder` 会注入 `mods\native\*.dll`，`AurieCore` 再加载 `mods\aurie\*.dll`。

```
Chronicon/
├─ Chronicon.exe
└─ mods/
   ├─ AurieLoader.exe              ← Aurie v1.0.5（IFEO Debugger 指向它）
   ├─ native/
   │   └─ AurieCore.dll           ← v1.0.5 的 x64 版，不是 AurieCore-x86.dll
   └─ aurie/
       ├─ YYToolkit.dll           ← YYToolkit v3.2.3 x64（699904 B）
       └─ ChroniconSkillMeter.dll ← 本项目 x64 产物
```

组件来源：`AurieLoader.exe` / `AurieCore.dll` 来自 [Aurie v1.0.5](https://github.com/AurieFramework/Aurie/releases/tag/v1.0.5)；`YYToolkit.dll` 来自 [YYToolkit v3.2.3](https://github.com/AurieFramework/YYToolkit/releases/tag/v3.2.3)；`ChroniconSkillMeter.dll` 来自 Actions Artifact 或本地 `dist\`。**别用 v1.2.1 / v4.0.1 那套**，原因见 3.8。

> ⚠️ **版本与位数必须配套**：项目头文件来自 chronicon-dps（2024-02），只对应 Aurie v1.0.x + YYToolkit v3.x。Chronicon 是 x64（PE machine `0x8664`），四个文件都要 x64。
> ⚠️ Steam 每次「验证游戏文件完整性」或游戏更新都可能抹掉这些文件；更新后若又报同样错误，重做 B 或先 A 再重新 `Install Aurie`。

### 3.8 启动后闪退：YYToolkit v4 与本插件 ABI 不兼容

**症状**：Steam 能启动游戏、黑屏 / 进主菜单前直接退出，事件查看器里是 `Application Error`：

```
出错应用程序名称: Chronicon.exe，版本: 1.4.0.0
出错模块名称:     YYToolkit.dll，版本: 4.0.1.0
异常代码:         0xc0000005
错误偏移:         0x0000000000017752
错误模块路径:     ...\mods\aurie\YYToolkit.dll
```

`0xc0000005` = Access Violation。注意**出错模块是 `YYToolkit.dll`，不是 `ChroniconSkillMeter.dll`**——但根因仍在本插件侧：插件是按 **YYToolkit v3 头文件**编译的，却去调用 **v4 运行时**的接口虚表，等于按旧槽位跳进了一个完全不相干的函数。

**证据链**（全部实测）：

| 事实 | 结论 |
|---|---|
| 本项目 `extern/YYToolkit/Shared.hpp` 的 git blob SHA 是 `16631db58725`，`Aurie/shared.hpp` 是 `15885650ecd4` | 与 `atty303/chronicon-dps` 的 `include/` 快照**逐字节一致**（2024-02） |
| chronicon-dps v1.0.0 发布于 2024-02-26，当时 Aurie 最新是 **v1.0.5**（2024-01-15）、YYToolkit 最新是 **v3.2.3**（2024-01-21） | 这套头文件就是 v3.2.3 API |
| `ModuleMain.cpp` 调 `ObGetInterface("YYTK_Main", ...)`，`Gml.cpp` 调 `iface->CallBuiltin(...)`、`AsReal()` / `AsBool()` / `AsString()` | 全是 v3 虚表布局与 v3 方法名 |
| [YYToolkit v4.0.1 release notes](https://github.com/AurieFramework/YYToolkit/releases/tag/v4.0.1) 开头写着 `# Developers: Your plugins may be broken!`，[v4 API 变更](https://github.com/AurieFramework/YYToolkit/wiki/API-changes-in-v4) 列出 `AsReal→ToDouble`、`AsBool→ToBoolean`、`AsString→ToString`、删除 `CInstance::at/operator[]`、新增 `GetMember/GetRefMember/GetMemberCount`、`RValue` 初始化语义变化 | v4 是明确的破坏性升级，旧插件必须重新编译 |
| 本机 `Chronicon.exe` / `AurieCore.dll` / `YYToolkit.dll` / `ChroniconSkillMeter.dll` 全为 PE `0x8664` | 不是 32/64 位错配 |

**唯一正确的版本组合**（x64，已在本机装好）：

| 组件 | 版本 | 发布日期 | 大小 (B) | SHA-256 前 16 位 |
|---|---|---|---|---|
| `AurieLoader.exe` | Aurie v1.0.5 | 2024-01-15 | 272896 | `8ce89a0b518f65e4` |
| `native\AurieCore.dll` | Aurie v1.0.5 | 2024-01-15 | 606720 | `4b26ccf828bed538` |
| `aurie\YYToolkit.dll` | YYToolkit v3.2.3 | 2024-01-21 | 699904 | `1ab7bb9413228b1c` |
| `aurie\ChroniconSkillMeter.dll` | 本项目 x64 | — | 442368 | 由 Actions 构建 |

> ❌ 不要混搭：`Aurie v1.2.1 + YYToolkit v4.0.1` 是官方配对，但**只适用于按 v4 头文件重编过的插件**；`Aurie v2.x + YYToolkit v5.x` 更是另一套模块入口。想用 v4 就得把 `Gml.cpp` / `ModuleMain.cpp` 迁到 v4 API 并换全套头文件重新编译，本项目没有做这条路线。

**一键安装 / 回滚**（脚本会先备份、校验 SHA-256，不删除任何文件）：

```powershell
cd "C:\Users\FIREBAT\WorkBuddy AI\2026-11-09-08-54-10\ChroniconSkillMeter"
powershell -NoProfile -ExecutionPolicy Bypass -File .\install_runtime.ps1
```

常用参数：

| 参数 | 作用 |
|---|---|
| `-GameDir "D:\...\Chronicon"` | 游戏不在默认路径时指定（默认 `D:\SteamLibrary\steamapps\common\Chronicon`） |
| `-ModDll "C:\...\ChroniconSkillMeter.dll"` | 指定插件 DLL，默认找 `dist\ChroniconSkillMeter.dll` |
| `-NoDownload` | 只用 `dist\runtime\` 里已缓存的文件，完全离线 |
| `-UseControl` | 改装官方 [ChroniconDPS.dll v1.0.0](https://github.com/atty303/chronicon-dps/releases/tag/v1.0.0) 做 A/B 对照 |
| `-Rollback` | 从 `<游戏目录>\_ChroniconSkillMeter_backup\` 还原原来的那套 |

原栈会被完整备份到 `<游戏目录>\_ChroniconSkillMeter_backup\`（`AurieLoader.exe`、`native\`、`aurie\`、以及官方对照 DLL）。`-UseControl` / `-Rollback` 切换时，当前 DLL 会被 **移动** 到备份目录而不是删除。

**排查顺序建议**：

1. 先跑一次普通安装，启动游戏。不再闪退 = 版本问题已解决，按 **F5** 看窗口。
2. 若仍闪退，用 `-UseControl` 换上官方 `ChroniconDPS.dll`（F1 / F2 切换），启动游戏。**官方 mod 能跑 → 框架没问题，问题在本插件的 Hook 逻辑**；官方 mod 也闪退 → 是 Aurie / IFEO / 游戏版本问题，先看 3.7。
3. 若换成官方 mod 就不闪退、换回本插件又闪退，把 Windows 事件查看器里最新的 `Application Error` 的「出错模块 / 异常代码 / 错误偏移」记下来，那是定位到具体 Hook 的关键。
4. 任何时候想回到原样：`-Rollback`。

自查闪退原因（PowerShell）：

```powershell
Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000} -MaxEvents 5 |
  ForEach-Object { $_.Message } | Select-String -Pattern 'Chronicon|YYToolkit|Aurie|0xc0000005'
```

### 3.9 游戏能进、不闪退，但看不到统计窗口

这是闪退修好之后最容易卡住的一步，现象通常是：

- 游戏正常进图、能打怪，按 **F5** 没反应；
- 画面左上角那一堆 `FPS:` / `OSMC:` / `OSID:` / `WV:` / `UI:` / `display:` 还在——**那是 Chronicon 自带的调试 HUD**（字符串就在 `data.win` 里），跟本插件、跟 YYToolkit 都无关，别拿它判断 mod 有没有加载；
- `mods\aurie\` 里**没有生成 `skillmeter.ini`**，也没有 `unknown_skills.txt` / `damage_log.csv`；
- 游戏根目录下的 `YYToolkit.log` 时间戳还是旧的。注意 **YYToolkit v3.2.3 根本不写这个文件**（它只有标题为 `YYToolkit Log` 的控制台窗口），所以「日志没更新」不能证明 mod 没加载。

「连 ini 都没写」是最强的线索：`Config::Load()` 在 `ModuleInitialize` 里，只要它跑过就一定会落一份默认 ini。没落 = 我们的模块入口没跑到那一步。

#### 根因：Aurie v1.0.5 会把初始化失败的模块直接卸载

对着 Aurie v1.0.5 的 `Aurie/source/AurieMain.cpp` 和 `Module Manager/module.cpp` 看一遍，加载时序是这样的：

1. `ArProcessAttach` 先把 `mods\aurie\*.dll` **全部** `LoadLibraryW` 进来（按文件名排序，此时不执行任何入口）；
2. 再遍历所有模块调用 `ModulePreinitialize`——YYToolkit 就在这里 `ObCreateInterface("YYTK_Main")`；
3. 恢复进程、等游戏窗口进入 idle；
4. 最后才遍历所有模块调用 `ModuleInitialize`，**返回值非 `AURIE_SUCCESS` 的模块会被立刻标记 purge 并 `FreeLibrary`**。

旧版 `ModuleInitialize` 第一行就是：

```cpp
if (!AurieSuccess(ObGetInterface("YYTK_Main", ...)))
    return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;   // ← 致命：直接被卸载，且没有任何日志
```

只要接口在那一刻还没就绪（框架版本差异、加载时序、YYToolkit 自身初始化失败都会导致），插件就**静默消失**：不写 ini、不挂 Hook、不画窗口，游戏却一切正常。这类失败最难受的地方就是它完全不留痕迹。

#### 现在的做法

`ModuleMain.cpp` 已改成：

- 新增 `ModulePreinitialize` / `ModuleUnload` 导出；
- **入口永远返回 `AURIE_SUCCESS`**，绝不因为依赖未就绪让 Aurie 卸载自己；
- 在 `ModulePreinitialize` 阶段就先写好 `skillmeter.ini`，**只要 DLL 被加载就一定留痕**；
- `YYTK_Main` 拿不到时只记一条日志，然后由后台线程在 150 秒内每 250 ms 重试，拿到后再装 Hook；
- Hook 安装、首次 GML 调用、第一帧绘制、第一次伤害都写在 **`mods\aurie\skillmeter_diag.log`** 里；
- GML 内置函数（`instance_find`、`variable_global_get` 等）统一挪到 `gml_Script_GUIdraw` 里、也就是游戏主线程上调用，避免跨线程调引擎。

`data.win` 里已核对过两个 Hook 目标确实存在：`gml_Script_actorDamage`、`gml_Script_GUIdraw`（前者 2 处、后者 47 处），所以不是脚本名写错。

#### 一次启动就能定位：读 `skillmeter_diag.log`

把新 DLL 放进 `mods\aurie\`，启动游戏进图、打几下怪、正常退出，然后：

```powershell
Get-Content "D:\SteamLibrary\steamapps\common\Chronicon\mods\aurie\skillmeter_diag.log" -Tail 60
```

正常的话应该依次看到：

```text
dll_process_attach        模块映像已映射
module_preinitialize      dir=...\mods\aurie
config_early              ini=...\skillmeter.ini
retry_thread              started
module_initialize         dir=...\mods\aurie
setup_begin               YYTK_Main 已解析
script_lookup             actorDamage=0x... GUIdraw=0x...
hooks                     actorDamage hook=1 GUIdraw hook=1
setup_done                module is live
gui_hook_first_frame      GUIdraw hook is alive
damage_hook_first_hit     first damage: skill=...
```

按最后停在哪一行判断：

| 最后一行 | 说明 | 下一步 |
|---|---|---|
| 文件根本不存在 | DLL 没被 `LoadLibrary`，问题在 `mods\native\AurieCore.dll` 有没有被注入 | 查 3.7 的 IFEO；跑一次 `install_runtime.ps1` 重装固定版 |
| 只有 `dll_process_attach` | 进了进程但 Aurie 没派发入口 | 检查 `mods\aurie` 里是否同时有 `YYToolkit.dll v3.2.3`、位数是否 x64 |
| 到 `module_preinitialize` 但没有 `module_initialize` | Aurie 卡在 `WaitForInputIdle` | 后台重试线程仍会尝试；看有没有 `retry_poll` / `setup_begin` |
| 有 `setup_deferred` / 一路 `retry_timeout` | `YYTK_Main` 始终没注册 | YYToolkit 版本不匹配（回到 3.8），或用 `-UseControl` 做 A/B |
| `script_lookup` 里指针是 `0000000000000000` | YYToolkit 的 runner interface 没就绪，脚本表拿不到 | 看 YYToolkit 控制台报错；优先确认 3.8 的版本组合 |
| `hooks` 里 `GUIdraw hook=0` | Hook 没装上，窗口不会画 | 把整份日志贴出来，通常是框架/游戏版本差异 |
| 有 `gui_hook_first_frame` 但没窗口 | Hook 正常，问题在绘制或 `skillmeter.ini` 的 `visible=0` / 窗口坐标 | 按 F5 切一次；检查 ini 的 `x` / `y` / `width` / `height` |
| 没有 `damage_hook_first_hit` | 伤害 Hook 没命中或玩家来源判断没过 | 确认 `Display Numbers` 无关；本插件不依赖飘字，但依赖 `actorDamage` 的参数顺序 |

诊断日志是**追加**写入的，多次启动会累积；每次都有新的 `dll_process_attach` 作为分界。定位完可以直接删掉这个文件，它会在下次启动时重建。

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
- **本机没有 C++ 编译器**（无 VS2022 / Windows SDK / MSBuild / MinGW / LLVM），所以本地 `build.bat` 只能跑到「Locating MSBuild」并正确报缺 VS2022；本地编译与 `dist\` 复制未实机验证。
- ✅ **GitHub Actions 云端编译已实跑成功**：`windows-2022` + VS2022 Enterprise + MSVC v143 + C++20，`Release|x64`。`run 42559137738`（commit `1871bcf`）全部步骤绿灯，产出 Artifact `ChroniconSkillMeter-x64`。
- ✅ **产物已实际校验**：x64 `ChroniconSkillMeter.dll` 442368 B，PE 头 `0x8664` = x64，导出表含 `DllMain`、`ModuleInitialize`、`__AurieFrameworkInit`——`ModuleInitialize` 正是 `mods\aurie\` 加载时需要的入口。首次失败的 6 条 MSVC 报错（`C2039 m_Object` ×3、`C2397` 窄化转换 ×3）已全部修复。
- ✅ **Win32 配置也实跑通过**（`run 42559265956`，`Release|Win32`）：365056 B，PE 头 `0x014c` = x86。两个平台的 Artifact 分别是 `ChroniconSkillMeter-x64` / `ChroniconSkillMeter-Win32`；本机 `dist\` 下也已放好两份，按第 3.5 节查到游戏位数后选对应的一个即可。
- ✅ **本机已定位并修复一次真实闪退**：Steam 报「游戏可执行文件缺失」是 IFEO `Debugger` 指向的 `AurieLoader.exe` 未落地（见 3.7）；补齐后出现的 0xc0000005 闪退，经 WER 确认出错模块为 `YYToolkit.dll 4.0.1.0` 偏移 `0x17752`，根因是 v3 头文件编出的插件配 v4 运行时（见 3.8）。现已改成 **Aurie v1.0.5 + YYToolkit v3.2.3 + 本插件 x64**，四个文件均校验为 PE `0x8664`。
- ✅ **`install_runtime.ps1` 三条路径实跑通过**：普通安装 → `272896 / 606720 / 699904`；`-UseControl` → 只加载官方 `ChroniconDPS.dll`；`-Rollback` → 精确还原 `92160 / 625664 / 1094656`。安装前自动备份到 `_ChroniconSkillMeter_backup\`，未删除任何文件。
- ✅ **头文件版本已用哈希坐实**：`extern/YYToolkit/Shared.hpp` = `16631db58725`、`Shared.cpp` = `88d2d7490b9c`、`Aurie/shared.hpp` = `15885650ecd4`、`FunctionWrapper.hpp` = `2eec7c5b8aaf`，与 `atty303/chronicon-dps` 的 `ChroniconDPS/include/` git blob SHA 完全一致。
- ✅ **本机实机状态已确认**：换成 `Aurie v1.0.5 + YYToolkit v3.2.3` 后，游戏能正常进图、不再闪退，13:20 的 Steam 会话 `exit code 0`，且 13:20 之后没有任何新的 WER / `Application Error`。
- ✅ **Hook 目标脚本名已在本机 `data.win` 中核对**：`gml_Script_actorDamage`（2 处）、`gml_Script_GUIdraw`（47 处）、`gml_Script_floatNumCreate`、`obj_player`、`font_small_GUI_latin` 全部真实存在。
- ✅ **截图里的调试文字已排除误判**：`FPS:` / `OSMC:` / `OSID:` / `WV:` / `UI:` / `display:` 是 Chronicon 自带 HUD（字符串在 `data.win` / `Chronicon.exe`），YYToolkit v3.2.3 也不写 `YYToolkit.log`，两者都不能用来判断 mod 是否加载。
- ✅ **已定位「不闪退但没窗口」的根因**：旧 `ModuleInitialize` 在 `YYTK_Main` 未就绪时返回 `AURIE_MODULE_DEPENDENCY_NOT_RESOLVED`，被 Aurie v1.0.5 静默 purge，因此全盘都没有 `skillmeter.ini`。详见 3.9，已改为永不失败 + 后台重试 + 全程 `skillmeter_diag.log` 留痕。
- ⚠️ **仍未验证**：新 DLL 在游戏进程内 Hook 是否命中、窗口是否显示、技能伤害是否按 ID 正确归因。这部分只能在你把新 DLL 放进 `mods\aurie\` 并重新启动一次游戏后，按 3.9 读 `skillmeter_diag.log` 确认。引擎侧调用是对照 `atty303/chronicon-dps`、`falorfrozen-cmd/ChroniconPlus` 源码核对的，游戏版本差异可能导致参数顺序不同。若仍闪退，按 3.8 的 `-UseControl` 做 A/B 对照。
- `extern/` 的 4 个依赖头文件已通过 GitHub contents API 实际下载并解码成功（73619 / 7585 / 16118 / 2114 B）。`fetch_deps.ps1` 三条通道都实测过：raw 直连成功、`curl.exe` + API 回退成功、PowerShell 5.1 直连 API 会 403（已用前两条绕开）。
- 本机 `curl` 连续请求 raw 偶发 `schannel: server closed abruptly`，因此工作流加了 `--retry 3` 与 API 回退；GitHub runner 使用不同 TLS 栈，实测四个头文件均一次下载成功。
- 安装章节（第 3 节）已按 Aurie `v1.0.5` / `v1.2.1` / `v2.0.2`、YYToolkit `v3.2.3` / `v4.0.1` 的实际 release 资产与 `AurieMain.cpp`、`AurieInstaller` 源码逐条核对；`mods\` 目录的离线补齐与降级已在**本机游戏目录实际执行并校验文件尺寸 / PE 头 / IFEO 路径**。
- 结论：**DLL 已经用 GitHub Actions 编出来了，运行时版本组合也已在本机装好，闪退已解决**；剩下的是把带诊断的新 DLL 放进去、重新启动游戏，用 `skillmeter_diag.log` 确认 Hook 命中与窗口显示。

---

## 10. 文件清单

```
ChroniconSkillMeter/
├─ README.md                     本文件
├─ preview.html                  界面与交互预览（单文件，浏览器打开）
├─ build.bat                     一键编译 → dist\ChroniconSkillMeter.dll
├─ install_runtime.ps1           一键装/回滚固定版运行时（Aurie 1.0.5 + YYTK 3.2.3）
├─ push_to_github.bat            一键推送到你的 GitHub 仓库
├─ fetch_deps.bat / .ps1         拉取 YYToolkit / Aurie 头文件（含 API 回退）
├─ .gitignore                    排除编译产物与第三方头文件
├─ .github/workflows/build.yml   云端编译 DLL，产出 Actions Artifact
├─ dist\                         编译后生成，DLL 成品固定放这里
│  └─ runtime\                   固定版运行时缓存（AurieLoader/AurieCore/YYToolkit/对照 DLL）
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
- [Aurie v1.0.5 release](https://github.com/AurieFramework/Aurie/releases/tag/v1.0.5) — ★ 本项目固定使用的运行时，与头文件同源
- [YYToolkit v3.2.3 release](https://github.com/AurieFramework/YYToolkit/releases/tag/v3.2.3) — ★ 本项目固定使用的 YYToolkit，v3 API
- [YYToolkit v4.0.1 release](https://github.com/AurieFramework/YYToolkit/releases/tag/v4.0.1) / [API changes in v4](https://github.com/AurieFramework/YYToolkit/wiki/API-changes-in-v4) — 破坏性升级说明，解释了 3.8 的闪退
- [chronicon-dps v1.0.0](https://github.com/atty303/chronicon-dps/releases/tag/v1.0.0) — 已知可用的对照 mod，用于 3.8 的 A/B 排查
- [Aurie v1.2.1 release](https://github.com/AurieFramework/Aurie/releases/tag/v1.2.1) — 最后一个带 `AurieManager.exe` / `Add Game` 的版本（只用它建启动项，别用它配的 v4 组件）
- [Aurie v2.0.2 release](https://github.com/AurieFramework/Aurie/releases/tag/v2.0.2) — 当前的 `AuriePatcher.exe` 方案
- [Aurie Installer / Patcher 源码](https://github.com/AurieFramework/Aurie/tree/master/AurieInstaller) — `mods\aurie` / `mods\native` 目录结构与 patch 流程的依据
