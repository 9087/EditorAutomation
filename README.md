# EditorAutomation

An Unreal Engine 5 editor plugin that exposes editor capabilities as `BlueprintCallable` functions, so external tools and AI harnesses can drive the editor through the engine's **Remote Control** Web API.

Its primary consumer is **dsh-unreal**, the DeepSeek Harness adapter that turns these functions into a 1:1 agent tool surface (all calls routed through CallFunction, diagnostics through GetRecentLog). The API itself stays harness-agnostic, so any Remote Control client can drive it.

The plugin is **harness-agnostic**: it knows nothing about any particular agent product. A harness adapter (for example the separate `dsh-unreal` DSH plugin) calls these functions and adds its own tool surface, approvals and UI.

[中文](README.zh.md)

## Status

The first RC-infrastructure slice is implemented and covered by automation tests:

- `Ping`
- editor identity: `GetEditorInfo` (process id, engine version, project name/directory, editor flag)
- reflection: `GetEnumInfo`, `GetStructInfo`, `GetClassInfo`, `GetFunctionInfo`, `GetPropertyType`
- object access: `GetObjectClass`, `GetPropertyValue`, `SetPropertyValue`, `CallFunction`,
  `CallFunctions` (batch several calls in one round trip)
- diagnostics: `GetRecentLog` — an in-memory ring buffer (4096 lines) of warning/error lines
  captured from the engine output log at module load, filterable by severity and category
- PIE: `StartPIE`, `StopPIE`, `IsPIERunning`, `CaptureScreenshot` (one entry point for the game
  viewport or the editor window), `FocusPIEViewport`, `SendKeyToPIE` (focus the game viewport and
  inject keys for automation/debug)
- Blueprint graph authoring: `GetBlueprintGraphs`, `GetGraphNodes`, `GetNodePins` (with
  `LinkedPinIds` for exec-chain tracing), `AddCallFunctionNode` (array functions automatically use
  the wildcard-aware `K2Node_CallArrayFunction`), `AddEventNode`, `AddComponentBoundEventNode`,
  `AddBranchNode`, variable get/set nodes, `AddBlueprintVariable` (including array types such as
  `int32[]` / `array(int32)`), `ConnectPins`, pin defaults/objects and `RemoveGraphNode`

Property values are nested JSON text (`ValueJson` / `ParametersJson`), with scalars encoded as strings and
enums as numeric entry values; type information stays in the reflection API and can be cached by callers.
Calling `SetPropertyValue` marks the object modified, optionally opens an editor transaction and calls
`PostEditChangeProperty`. A failed write is rolled back to the previous value, so the object is left
unchanged (if the rollback itself fails, the result reports that — see `docs/api.md`).

Functions return their native value: mutating operations return `bool` and reads return their data
struct or plain value. There is no per-function error envelope — a `false` or empty result is logged
by EditorAutomation (category `LogEditorAutomation`), and `GetRecentLog` returns those lines in memory so a caller
can read the reason without touching log files.

The full wire contract — parameters, result fields, error codes, the Type-string grammar, the value-JSON
encoding and transport caveats — is documented in [`docs/api.md`](docs/api.md).

Capabilities implemented so far: asset/level/PIE control, reflection and nested-JSON object access,
Blueprint graph authoring, UMG design-time widget-tree editing and UMG runtime observation.
Planned next: a generic Details engine and animation read/write/evaluation.

## Versioning

The `Version` / `VersionName` fields in the `.uplugin` are release labels, not a compatibility contract:
`Version` is the integer a marketplace uses for plugin updates and `VersionName` is a human-readable tag.
During development neither is bumped per commit — they change only at tagged releases. No runtime version
handshake is performed; a caller installs a known tag of this plugin.

## Requirements

- Unreal Engine **5.5** (source build) for now; multi-engine support is out of scope for the first version.
- The engine's **Remote Control** plugin. The `.uplugin` declares it as a dependency, so enabling this plugin
  enables Remote Control too.
- Remote Control Web API listening on the default `127.0.0.1:30010`.

## Layout

One editor module holds every library — there are no sub-modules to wire up.

```
EditorAutomation.uplugin
LICENSE                     MIT
README.md / README.zh.md    this file (English / Chinese)
docs/api.md                 wire contract for harness adapters (functions, types, value JSON, errors)
Source/EditorAutomation/           the editor module
  EditorAutomation.Build.cs
  Public/EditorAutomationCoreLibrary.h        core: identity, dispatcher, reflection, property, PIE,
                                              screenshot, log
  Public/EditorAutomationBlueprintLibrary.h   Blueprint asset operations
  Public/EditorAutomationGraphLibrary.h       Blueprint graph authoring
  Public/EditorAutomationLevelLibrary.h       level operations
  Public/EditorAutomationAssetLibrary.h       asset queries
  Public/EditorAutomationUMGLibrary.h         UMG runtime observation (PIE)
  Public/EditorAutomationUMGEditorLibrary.h   UMG design-time widget tree editing
  Public/EditorAutomationLog.h                LogEditorAutomation category
  Public/EditorAutomationLogCapture.h         in-memory editor log types
  Private/EditorAutomationModule.cpp          module startup (attaches the log capture)
  Private/Library/                            one .cpp per RC library; the core library is split by
                                              concern (Function/Log/Property/Reflection/System)
  Private/Log/                                log category + in-memory capture implementation
  Private/Reflection/                         Type-string, property-path and FText helpers
  Private/Serialization/                      value-JSON encode/decode
  Private/Tests/                              Unreal automation tests
```

The RC-facing CDO path is `/Script/EditorAutomation.Default__EditorAutomationCoreLibrary`; the other
libraries are reachable as `/Script/EditorAutomation.Default__EditorAutomation<Library>Library`.

## Install and build

1. Put this directory under `<YourProject>/Plugins/EditorAutomation` (or a shared engine `Plugins` folder).
2. Enable `EditorAutomation` for the project (Project Settings → Plugins).
3. Build the editor target, for example:

```powershell
Engine\Build\BatchFiles\Build.bat <ProjectEditorTarget> Win64 Development -Project="<path>\<Project>.uproject"
```

## Tests

The module ships Unreal automation tests (all call the library directly, no HTTP needed):

| Test | Covers |
|---|---|
| `EditorAutomation.TypeTree.Parsing` / `.Formatting` | Type-string grammar and property type formatting |
| `EditorAutomation.ValueJson` | nested-JSON round-trips for scalars, structs and property paths |
| `EditorAutomation.Core.Ping` | liveness, RC CDO path |
| `EditorAutomation.Core.GetEditorInfo` | process id vs platform, engine version, project name/dir, editor flag, reflected field names/types |
| `EditorAutomation.Core.GetEnumInfo` | entry names, display names, numeric values, error paths |
| `EditorAutomation.Core.GetStructInfo` | struct members, nested struct types, error paths |
| `EditorAutomation.Core.GetClassInfo` | properties, BlueprintCallable functions, parameter directions, error paths |
| `EditorAutomation.Core.GetFunctionInfo` | signatures, in/out parameters, return types, error paths |
| `EditorAutomation.Core.GetObjectClass` | CDO/script-object/asset ClassType resolution, GetClassInfo round-trip, error paths |
| `EditorAutomation.Core.GetPropertyType` | schema-only paths (structs, arrays, enums), error paths |
| `EditorAutomation.Core.GetPropertyValue` | scalar/struct/array/enum/object-ref reads, error paths |
| `EditorAutomation.Core.SetPropertyValue` | writes and restores (numbers, bools, structs, enums), atomic rollback of failed struct/enum/array writes, unsupported write, invalid JSON, error paths |
| `EditorAutomation.Core.CallFunction` | returns, out parameters, struct returns, invalid JSON/parameter errors |

Build the editor target, then run the tests headless:

```powershell
# 1. Build
Engine\Build\BatchFiles\Build.bat <ProjectEditorTarget> Win64 Development -Project="<path>\<Project>.uproject"

# 2. Run (close any running editor first)
Engine\Binaries\Win64\UnrealEditor-Cmd.exe "<path>\<Project>.uproject" `
  -ExecCmds="Automation RunTests EditorAutomation" -TestExit="Automation Test Queue Empty" `
  -unattended -nopause -nosplash -nullrhi -NoSound
```

A test run is successful when the log (`<Project>/Saved/Logs/<Project>.log`) ends with
`Test Completed` and reports `Failed: 0`.

## Smoke call (Remote Control)

With the editor running:

```powershell
$payload = @{
  objectPath   = "/Script/EditorAutomation.Default__EditorAutomationCoreLibrary"
  functionName = "Ping"
  parameters   = @{}
} | ConvertTo-Json
Invoke-RestMethod -Method Put -Uri "http://127.0.0.1:30010/remote/object/call" `
  -ContentType "application/json" -Body $payload
```

A successful response carries `"pong"` in `ReturnValue`.

## License

MIT. See [LICENSE](LICENSE).
