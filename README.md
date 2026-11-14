# 炳哥输入

炳哥输入是一个面向 **Windows 10 / Windows 11 x64** 的本地输入法原型，基于 Windows Text Services Framework（TSF）和原生 C++17 开发，支持五笔86、简体全拼以及五笔状态下的五笔＋拼音混输。

项目不依赖第三方输入法框架，不需要联网，不记录输入原文。词库、转换脚本、构建脚本和安装脚本均随源代码提供。

> 当前处于原型阶段，尚未进行代码签名。建议先在记事本等应用中测试，再决定是否用于日常工作。

## 功能介绍

- **简体全拼**：内置 65,125 条转换后数据，支持 `wen`、`jiang`、`nihao` 等完整拼音编码。
- **五笔86**：内置 136,916 条转换后数据，五笔编码最多四码。
- **五笔＋拼音混输**：五笔状态下可直接输入全拼；五笔候选优先，拼音候选随后，重复候选去重。
- **候选窗**：数字选词、首选上屏、逗号/句号翻页、横排/竖排切换，并适配 DPI、多显示器和屏幕边缘。
- **中英文与大写输入**：Shift 单按切换中英文，Shift+字母直接输入大写英文；CapsLock 开启时进入英文，关闭后恢复之前的中文状态。
- **全角/半角标点**：支持常用 OEM 标点和 Shift 数字标点，切换状态会保存。
- **设置持久化**：保存输入方案、横竖排和全半角标点设置。
- **并行升级**：版本化 DLL 注册，不覆盖仍被应用占用的旧文件，通常无需重启 Windows。
- **离线运行**：无网络请求、云端服务或 AI 调用。

目前不包含整句输入、简拼、模糊音、个人输入习惯学习、联想输入和 AI 功能；这些属于后续规划，不应视为已实现功能。

## 快捷键

| 按键 | 功能 |
| --- | --- |
| Shift 单按 | 切换中英文 |
| Shift+字母 | 取消当前组合串并直接输入大写英文字母 |
| CapsLock | 开启时进入英文；关闭时恢复之前的中文状态 |
| Ctrl+Space | 切换中英文 |
| F8 | 切换拼音 / 五笔86 |
| Ctrl+. | 切换全角 / 半角标点 |
| F6 | 有组合串时切换候选窗横排 / 竖排 |
| 1～5 | 选择当前页候选 |
| Space | 上屏首选 |
| , / . | 有组合串时向前 / 向后翻页 |
| Backspace | 删除一个编码字符 |
| Esc | 取消当前组合串 |
| Enter | 原码上屏 |

## 安装

### 环境要求

- Windows 10 22H2 或 Windows 11，x64。
- 管理员 PowerShell，用于复制 DLL 和注册 COM/TSF Profile。

### 从发布包安装

1. 打开[最新 Release](../../releases/latest)，下载 Windows x64 ZIP 并完整解压。
2. 以管理员身份打开 Windows PowerShell，进入解压目录。
3. 执行：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\install.ps1
```

4. 关闭并重新打开记事本等测试应用，使其加载新版 DLL。
5. 首次使用时，在“设置 → 时间和语言 → 语言和区域 → 中文(简体，中国) → 语言选项 → 键盘”中添加 **炳哥输入**。
6. 切换到“炳哥输入”，输入 `wen` 验证拼音；按 F8 后输入 `wq` 验证五笔。

升级时不需要先卸载。安装脚本默认使用 `TV1TSF.v11.dll` 并行注册；已经打开的应用不会自动切换 DLL，需关闭后重开。不要重复使用相同版本名安装不同内容的 DLL。

### 卸载

先切换到其他输入法，再在管理员 PowerShell 中进入发布包目录并执行：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\uninstall.ps1
```

注册会被取消。仍被应用占用的旧 DLL 会保留，关闭相关应用后可再次运行卸载脚本清理。

## 从源代码构建

### 构建依赖

- Visual Studio 2022 Build Tools。
- MSVC x64 C++ 工具链。
- Windows 10 SDK 19041。
- CMake 与 Ninja。
- Windows PowerShell 5.1。

可在管理员 PowerShell 中运行以下脚本安装官方构建依赖：

```powershell
.\scripts\bootstrap-build-tools.ps1
```

安装完成后重新打开 PowerShell，在项目根目录执行：

```powershell
.\scripts\build.ps1 -Configuration Release
.\build\bin\Release\DictionaryTests.exe
```

构建产物位于 `build/bin/Release/`。安装刚刚构建的 DLL：

```powershell
.\scripts\install.ps1 -DllPath .\build\bin\Release\TV1TSF.dll -Version local1
```

后续本地构建请更换版本名，例如 `local2`、`local3`，避免覆盖已加载的 DLL。

## 项目结构

```text
TV1TSF/
├── src/                 TSF、COM、候选窗、词典查询和编辑会话
├── tests/               词典与混输排序回归测试
├── data/                可修改的拼音、五笔 TSV 数据及来源说明
├── tools/               Rime YAML 到 TSV 的转换脚本
├── scripts/             构建、依赖安装、安装和卸载脚本
├── licenses/            第三方许可证和数据声明
├── docs/                开发过程文档
└── 构建与验证.md         各阶段构建和实机验证记录
```

## 词库与许可证

项目自有源代码采用 [MIT License](LICENSE)。

第三方数据分别保持原许可证：

- 拼音数据来自 `rime/rime-pinyin-simp`，Apache License 2.0。
- 五笔86数据来自 `rime/rime-wubi`，GNU LGPL-3.0。

上游提交、作者、修改方式和转换命令见 [data/SOURCES.txt](data/SOURCES.txt) 与 [licenses/NOTICE.txt](licenses/NOTICE.txt)。TSV 数据和转换脚本共同提供，便于审查、修改和再生成。

## 开发过程与验证

阶段设计、关键问题及 TV1～v11 的演进过程见[开发过程](docs/DEVELOPMENT.md)，详细构建和实机验证记录见[构建与验证](构建与验证.md)。

自动测试主要覆盖词典查询、五笔优先、混输去重和未知编码。TSF 按键、候选窗、安装升级及应用兼容性仍需要 Windows 实机验证。

## 后续方向

- 输入习惯记录与本地候选排序。
- 个性化联想输入。
- 更多五笔方案和配置界面。
- 更完整的整句、简拼和应用兼容性测试。
- 在明确隐私边界后评估可选的本地 AI 能力。
