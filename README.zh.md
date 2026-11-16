# EditorAutomation

一个 Unreal Engine 5 编辑器插件：把编辑器能力暴露成 `BlueprintCallable` 函数，让外部工具或 AI harness 通过引擎自带的 **Remote Control** Web API 驱动编辑器。

插件**不绑定任何具体的 harness**：它不知道任何 agent 产品的存在。harness 适配层（例如独立仓库里的 DSH 插件 `dsh-unreal`）负责调用这些函数，并加上自己的工具面、审批和界面。

[English](README.md)

## 状态

第一版 RC 基础设施已经实现，并有自动化测试覆盖：

- `Ping`
- 编辑器身份：`GetEditorInfo`（进程号、引擎版本、工程名/目录、是否编辑器）
- 反射：`GetEnumInfo`、`GetStructInfo`、`GetClassInfo`、`GetFunctionInfo`、`GetPropertyType`
- 对象访问：`GetObjectClass`、`GetPropertyValue`、`SetPropertyValue`、`CallFunction`、
  `CallFunctions`（一次往返批量调用多个函数）
- 诊断：`GetRecentLog` —— 模块加载时挂载内存环形缓冲（4096 行），记录引擎输出日志中的
  Warning/Error 行，可按严重级别与分类过滤
- PIE：`StartPIE`、`StopPIE`、`IsPIERunning`、`CaptureScreenshot`（游戏画面 / 编辑器窗口统一入口）、
  `FocusPIEViewport`、`SendKeyToPIE`（把键盘焦点还给 GameViewport 并注入按键，用于自动化/调试）
- Blueprint 图表编写：`GetBlueprintGraphs`、`GetGraphNodes`、`GetNodePins`（新字段
  `LinkedPinIds` 可正向追踪 exec 链）、`AddCallFunctionNode`（数组函数自动使用通配符感知的
  `K2Node_CallArrayFunction`）、`AddEventNode`、`AddComponentBoundEventNode`、`AddBranchNode`、
  变量 get/set 节点、`AddBlueprintVariable`（支持 `int32[]` / `array(int32)` 等数组类型）、
  `ConnectPins`、引脚默认值/对象、`RemoveGraphNode`

属性值统一用嵌套 JSON 文本（`ValueJson` / `ParametersJson`）：标量都编码成字符串，枚举用数字值；类型信息放在反射接口里，调用方可以自行缓存。`SetPropertyValue` 会标记对象已修改、可选开启编辑器事务，并调用 `PostEditChangeProperty`；写入失败时会回滚到旧值，保证对象不被改坏（回滚本身失败时结果里会明确报告，见 `docs/api.md`）。

函数返回原生值：会失败的操作返回 `bool`，读取类返回数据 struct 或普通值。不再有逐函数的错误信封；
失败时 EditorAutomation 会把原因写入引擎日志（分类 `LogEditorAutomation`），`GetRecentLog` 在内存中读回这些行，
调用方不需要翻日志文件就能知道失败原因。

完整的线上协议（参数、结果字段、错误码、Type 字符串语法、value-JSON 编码与传输注意事项）见 [`docs/api.md`](docs/api.md)（英文）。

已实现能力：资产/关卡/PIE 控制、反射与嵌套 JSON 对象访问、Blueprint 图表编辑、
UMG 设计期控件树读写与 UMG 运行时观察。后续计划：通用 Details 引擎、动画读写与评估。

## 版本

`.uplugin` 里的 `Version` / `VersionName` 是**发布标签**，不是兼容契约：`Version` 是 Marketplace 给插件更新用的整数，`VersionName` 是给人看的标签。开发期不随每次提交改动，只在打 tag/发布时改；调用方安装的是一个已知的 tag，运行时不进行版本握手。

## 环境要求

- 目前只支持 Unreal Engine **5.5**（源码构建）；多引擎兼容不在第一版范围内。
- 引擎自带的 **Remote Control** 插件。本插件的 `.uplugin` 已声明依赖，启用本插件会一并启用 Remote Control。
- Remote Control Web API 监听默认的 `127.0.0.1:30010`。

## 目录

单一编辑器模块承载全部库，不再拆分多个子模块。

```
EditorAutomation.uplugin
LICENSE                     MIT
README.md / README.zh.md    本文件（英文 / 中文）
docs/api.md                 harness 适配层的线上协议（函数、Type、value JSON、错误码）
Source/EditorAutomation/           编辑器模块
  EditorAutomation.Build.cs
  Public/EditorAutomationCoreLibrary.h        反射 / 属性 / 调用 / PIE / 截图 / 日志
  Public/EditorAutomationBlueprintLibrary.h   Blueprint 资产操作
  Public/EditorAutomationGraphLibrary.h  Blueprint 图表编辑
  Public/EditorAutomationLevelLibrary.h       关卡操作
  Public/EditorAutomationAssetLibrary.h       资产查询
  Public/EditorAutomationUMGLibrary.h         UMG 运行时观察（PIE）
  Public/EditorAutomationUMGEditorLibrary.h   UMG 设计期控件树读写
  Public/EditorAutomationLogCapture.h         内存日志类型
  Private/EditorAutomationModule.cpp          模块启动（挂载日志捕获）
  Private/Library/                     各库实现
  Private/Reflection/                  Type 字符串、属性路径与 FText 辅助
  Private/Serialization/               value-JSON 编解码
  Private/Tests/                       Unreal 自动化测试
```

面向 RC 的 CDO 路径是 `/Script/EditorAutomation.Default__EditorAutomationCoreLibrary`；其余库为
`/Script/EditorAutomation.Default__EditorAutomation<库名>Library`。

## 安装与构建

1. 把本目录放到 `<你的工程>/Plugins/EditorAutomation`（或引擎共享的 `Plugins` 目录）。
2. 在工程里启用 `EditorAutomation`（Project Settings → Plugins）。
3. 构建编辑器目标，例如：

```powershell
Engine\Build\BatchFiles\Build.bat <ProjectEditorTarget> Win64 Development -Project="<path>\<Project>.uproject"
```

## 测试

模块自带 Unreal 自动化测试（全部直接调用 C++ 接口，不需要 HTTP）：

| 测试 | 覆盖内容 |
|---|---|
| `EditorAutomation.TypeTree.Parsing` / `.Formatting` | Type 字符串语法与属性类型格式化 |
| `EditorAutomation.ValueJson` | 标量/结构体/属性路径的嵌套 JSON 往返 |
| `EditorAutomation.Core.Ping` | 存活探测、RC CDO 路径 |
| `EditorAutomation.Core.GetEditorInfo` | 进程号与平台 API 一致、引擎版本、工程名/目录、编辑器标记、反射字段名/类型 |
| `EditorAutomation.Core.GetEnumInfo` | 条目名、显示名、数字值、错误路径 |
| `EditorAutomation.Core.GetStructInfo` | 结构体成员、嵌套结构体类型、错误路径 |
| `EditorAutomation.Core.GetClassInfo` | 属性、BlueprintCallable 函数、参数方向、错误路径 |
| `EditorAutomation.Core.GetFunctionInfo` | 函数签名、输入/输出参数、返回类型、错误路径 |
| `EditorAutomation.Core.GetObjectClass` | CDO/脚本对象/资产路径的 ClassType 解析、经 GetClassInfo 往返验证、错误路径 |
| `EditorAutomation.Core.GetPropertyType` | 纯 schema 路径解析（结构体、数组、枚举）、错误路径 |
| `EditorAutomation.Core.GetPropertyValue` | 标量/结构体/数组/枚举/对象引用读取、错误路径 |
| `EditorAutomation.Core.SetPropertyValue` | 写入并恢复（数字、bool、结构体、枚举）、失败写入的原子回滚（结构体/枚举/数组）、不支持的写入、非法 JSON、错误路径 |
| `EditorAutomation.Core.CallFunction` | 返回值、输出参数、结构体返回值、非法 JSON/参数错误 |

先构建编辑器目标，再以无界面方式运行测试：

```powershell
# 1. 构建
Engine\Build\BatchFiles\Build.bat <ProjectEditorTarget> Win64 Development -Project="<path>\<Project>.uproject"

# 2. 运行（先关闭正在运行的编辑器）
Engine\Binaries\Win64\UnrealEditor-Cmd.exe "<path>\<Project>.uproject" `
  -ExecCmds="Automation RunTests EditorAutomation" -TestExit="Automation Test Queue Empty" `
  -unattended -nopause -nosplash -nullrhi -NoSound
```

测试成功时日志（`<Project>/Saved/Logs/<Project>.log`）以 `Test Completed` 结束并显示 `Failed: 0`。

## 冒烟调用（Remote Control）

编辑器运行后：

```powershell
$payload = @{
  objectPath   = "/Script/EditorAutomation.Default__EditorAutomationCoreLibrary"
  functionName = "Ping"
  parameters   = @{}
} | ConvertTo-Json
Invoke-RestMethod -Method Put -Uri "http://127.0.0.1:30010/remote/object/call" `
  -ContentType "application/json" -Body $payload
```

成功时 `ReturnValue` 里是 `"pong"`。

## 许可证

MIT，见 [LICENSE](LICENSE)。
