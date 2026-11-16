# EditorAutomation Remote Control API — wire contract

This document is the wire contract for harness adapters (for example the separate `dsh-unreal` DSH
plugin) that drive EditorAutomation through Unreal Engine's **Remote Control** Web API. It describes
the RC-facing functions, the `Type` string grammar, the value-JSON encoding and the transport
conventions. The core surface (`Ping`, `GetEditorInfo`, reflection, property, PIE, screenshot,
`GetRecentLog`, `CallFunction`/`CallFunctions`) lives on `UEditorAutomationCoreLibrary`; the domain
libraries (`Blueprint`, `Graph`, `Level`, `Asset`, `UMG`, `UMGEditor`) each expose their own CDO. It
matches the implementation in `Source/EditorAutomation/Private/Library/` and is exercised by the
automation tests in `Source/EditorAutomation/Private/Tests/EditorAutomationCoreLibraryTests.cpp`.

Every function returns its native value; failures are reported by the return value and logged to
`LogEditorAutomation` (read it through `GetRecentLog`). Only `CallFunction`/`CallFunctions` keep a
result envelope, because a generic dispatcher cannot express a typed failure in its native return.

---

## 1. Entry point

Remote Control reaches the library through its class default object (CDO):

```
/Script/EditorAutomation.Default__EditorAutomationCoreLibrary
```

The module must be loaded (it is an editor module, enabled through the `.uplugin`) and the Remote
Control Web API must be listening (default `127.0.0.1:30010`).

Every function below is `BlueprintCallable`, so it can be invoked with
`PUT /remote/object/call`:

```json
{
  "objectPath": "/Script/EditorAutomation.Default__EditorAutomationCoreLibrary",
  "functionName": "GetEnumInfo",
  "parameters": { "Type": "enum(/Script/Engine.ECollisionEnabled)" }
}
```

The HTTP response always wraps the function result in a single `ReturnValue` field:

```json
{
  "ReturnValue": {
    "ErrorCode": 0,
    "ErrorMessage": "",
    "Hint": "",
    "Info": { "Enum": "/Script/Engine.ECollisionEnabled", "Entries": [ ... ] }
  }
}
```

`Ping` returns a bare string, not a struct:

```json
{ "ReturnValue": "pong" }
```

Remote Control itself can fail the request before the plugin runs; that produces **HTTP 400** with a
body such as `{"errorMessage": "..."}` (unknown function on the object, unknown object, wrong
argument JSON). Those transport errors are distinct from the domain `ErrorCode` values inside
`ReturnValue`.

Parameter names are the exact `UFunction` parameter names (case sensitive). Unknown parameter keys in
the RC request are ignored by Remote Control. Pass values with the JSON type the parameter declares:
in particular, pass **strings as JSON strings** — Remote Control's implicit conversion is limited and
a JSON number supplied for a string parameter arrives as an empty string.

---

## 2. Native values and the one result envelope

Every function returns its native value: a data struct, an `FString`, a `bool`, a mutable object
pointer, or nothing. On failure the return value itself says so (empty string, `false`, default
struct, `nullptr`) and the reason is written to `LogEditorAutomation` — read it with `GetRecentLog`.
An adapter must therefore treat an empty answer as a failure candidate and consult the log for the
engine's reason; there is no per-function `ErrorCode` to check.

The **only** result envelope is the generic dispatcher, `CallFunction`/`CallFunctions`:

| Field | Type | Meaning |
|---|---|---|
| `ErrorCode` | int32 (JSON number) | `0` on success; see `EEditorAutomationCallFunctionErrorCode` for the non-zero values. |
| `ErrorMessage` | string | Empty on success; a human-readable reason on failure. |
| `Hint` | string | Empty on success; a suggested fix on failure. |
| `ReturnValueJson` | string | Return value as nested JSON text; empty when the function returns nothing. |
| `OutParametersJson` | string | Out parameters as a JSON object keyed by parameter name; `"{}"` when none. |
| `ReturnType` | string | Canonical Type string of the return value; empty when the function returns nothing. |

`CallFunctions` reports one such entry per batched call, plus a batch-level `bSuccess`/`ErrorMessage`.

---

## 3. `Type` string grammar

A Type string describes the declared type of a property, parameter, return value or object. It is
produced canonically by the library and can be cached per `(Type, PropertyPath)`.

```
Type        := plain-kind
             | named-kind "(" Path ")"
             | "array" "(" Type ")"
             | "set" "(" Type ")"
             | "map" "(" Type "," Type ")"        // key first, value second
plain-kind  := "bool" | "byte" | "int8" | "int16" | "int32" | "int64"
             | "uint16" | "uint32" | "uint64"
             | "float" | "double" | "string" | "name" | "text"
named-kind  := "enum" | "struct" | "object" | "class"
             | "softobject" | "softclass" | "interface"
             | "weakobject" | "lazyobject"
             | "delegate" | "multicastdelegate"
Path        := an object path that `LoadObject` resolves, e.g.
               /Script/Engine.ECollisionEnabled
               /Script/CoreUObject.Vector
               /Script/Engine.StaticMesh
```

Rules and examples:

- Kind names are lowercase and case sensitive. `Bool` and `STRUCT(...)` are rejected.
- Plain kinds take no arguments. Named kinds take exactly one `Path`. `array`/`set` take one nested
  `Type`; `map` takes two (key then value). Whitespace around a kind name and around each argument is
  ignored.
- Container nesting is allowed: `map(string,array(struct(/Script/CoreUObject.Vector)))`,
  `array(array(int32))`.
- The canonical spelling is compact: `array(object(/Script/Engine.MaterialInterface))`,
  `struct(/Script/CoreUObject.Vector)`, `enum(/Script/Engine.ECollisionEnabled)`.
- `Path` for a named kind is the reflected object path, not a C++ spelling:
  - `class(/Script/Engine.Actor)` and `object(/Script/Engine.StaticMesh)` refer to **class** objects
    (the declared class of an object property, or the runtime class of an object);
  - `enum(/Script/Engine.ECollisionEnabled)` is the UEnum path (for a C++ `enum class EFoo` the UEnum
    is named `EFoo`, e.g. `/Script/EditorAutomation.EEditorAutomationParameterDirection`);
  - `delegate(...)` / `multicastdelegate(...)` hold a function object path.
- Outer-kind requirements per entry point:

| Entry point | Required root kind |
|---|---|
| `GetEnumInfo` | `enum(...)` |
| `GetStructInfo` | `struct(...)` |
| `GetClassInfo`, `GetFunctionInfo`, `GetPropertyType` | `class(...)` or `object(...)` |

`object(...)` is accepted wherever `class(...)` is, as long as its path resolves to a class object.

---

## 4. Value-JSON encoding

Property and parameter **values** are carried as nested **JSON text inside a JSON string field**
(`ValueJson`, `OldValueJson`, `NewValueJson`, `ReturnValueJson`, `OutParametersJson`,
`ParametersJson`). The adapter must therefore parse the string a second time:

```
RC response: { "ReturnValue": { "ValueJson": "{\"X\":\"0\",\"Y\":\"0\",\"Z\":\"0\"}" } }
after one parse: ValueJson == '{"X":"0","Y":"0","Z":"0"}'
after two parses: { "X": "0", "Y": "0", "Z": "0" }
```

Encoding conventions, interpreted through the property's `Type`:

| Type | JSON encoding |
|---|---|
| `bool` | JSON string `"true"` / `"false"` |
| `int8`…`int64`, `uint16`…`uint64` | JSON string containing a decimal integer |
| `float`, `double` | JSON string; canonical text is `%.9g` (float) / `%.17g` (double); non-finite values read as `"NaN"`, `"Infinity"`, `"-Infinity"` |
| `string` | JSON string |
| `name` | JSON string of the name |
| `text` | JSON string of `FText::ToString()` |
| `enum(...)` | JSON string containing the **numeric** entry value, e.g. `"2"` (see `GetEnumInfo`). Writing also accepts the entry name as a fallback, e.g. `"QueryAndPhysics"` |
| `byte` | JSON string containing a decimal number; when the property is backed by an enum, same rules as `enum(...)` |
| `struct(...)` | JSON object keyed by member name; each member follows its own encoding. Members with the `CPF_Deprecated` flag are skipped |
| `array(T)`, `set(T)` | JSON array of `T` values. Sets are unordered |
| `map(K,V)` | JSON array of `{"Key": <K>, "Value": <V>}` objects (key/value in any order) |
| `object(...)`, `interface(...)`, `weakobject(...)`, `lazyobject(...)` | object path JSON string, or `null` when unset |
| `class(...)`, `softclass(...)` | class path / soft class path JSON string, or `null` |
| `softobject(...)` | soft object path JSON string, or `null` |
| `delegate(...)`, `multicastdelegate(...)` | reads as JSON `null`; **cannot be written** |

Additional rules:

- Integers are encoded as JSON **strings** (including int64/uint64) to avoid double-precision loss in
  the transports that carry them. Enum entry values are small, but they are strings inside value JSON
  as well.
- Floating point text is canonical but not fixed-format: compare numerically, not textually.
- Writing requires numeric text for numeric properties; the special read values `"NaN"`,
  `"Infinity"` and `"-Infinity"` are **not** accepted as input (they fail with `InvalidJson`).
- Reading a property whose reflected type is outside the table above (rare/future engine property
  kinds) fails with `UnsupportedProperty`; writing one fails with `InvalidJson`
  (`UnsupportedProperty` for the read check). Delegates are the notable read-`null`, write-`InvalidJson`
  case.
- Writing a `struct` updates only the members present in the JSON object; omitted members keep their
  current value. Writing an `array`/`set`/`map` replaces the whole container.

- **Atomic failure of `SetPropertyValue`:** the function snapshots the old value first and, if the
  write fails, compares the live value with that snapshot. If nothing changed (a scalar rejected
  before it was written, or a property type with no writable encoding such as a delegate), it returns
  the original error. Otherwise it restores the snapshot through the same value-JSON encoding, fires
  `PostEditChangeProperty`, marks the package dirty, and returns `InvalidJson` with an `ErrorMessage`
  noting that the previous value was restored and a `Hint` stating the object was left unchanged.
  Rollback itself can fail in rare cases (an old value that is not representable as writable JSON, for
  example a struct member holding `NaN`/`Infinity`); then the `ErrorMessage` names the rollback
  failure and the `Hint` warns that the property may be partially modified. Passing
  `bGenerateTransaction = true` and undoing the transaction is the backstop for that case.

---

## 5. Function reference

Common signatures of `UEditorAutomationCoreLibrary` (all `static`, all `BlueprintCallable`,
`Category = "EditorAutomation"`). Domain libraries follow the same style: getters return data
structs, mutators return `bool`, creators return the new object path.

```cpp
// core / identity / dispatcher
FString Ping();
FEditorAutomationEditorInfo GetEditorInfo();
FEditorAutomationCallFunctionResult CallFunction(const FString& ObjectPath, const FString& FunctionName,
                                                 const FString& ParametersJson, bool bGenerateTransaction);
FEditorAutomationBatchCallResult CallFunctions(const FString& CallsJson);

// reflection
FEditorAutomationEnumInfo GetEnumInfo(const FString& Type);
FEditorAutomationStructInfo GetStructInfo(const FString& Type);
FEditorAutomationClassInfo GetClassInfo(const FString& Type);
FEditorAutomationFunctionInfo GetFunctionInfo(const FString& Type, const FString& FunctionName);
FString GetObjectClass(const FString& ObjectPath);

// property
FString GetPropertyType(const FString& Type, const FString& PropertyPath);
FString GetPropertyValue(const FString& ObjectPath, const FString& PropertyPath);
FEditorAutomationPropertyWrite SetPropertyValue(const FString& ObjectPath, const FString& PropertyPath,
                                               const FString& ValueJson, bool bGenerateTransaction);

// PIE
bool StartPIE(const FString& LevelPath = TEXT(""), bool bNewWindow = false);
bool StopPIE();
bool IsPIERunning();
bool FocusPIEViewport();
bool SendKeyToPIE(const FString& KeyName, bool bFocusViewport = true);

// diagnostics
FString CaptureScreenshot(const FString& FilePath,
                          EEditorAutomationScreenshotTarget Target = GameViewport,
                          bool bShowUI = true);
FEditorAutomationLogPage GetRecentLog(int32 MaxLines = 200, int32 MinVerbosity = 3,
                                      const FString& CategoryFilter = TEXT(""), bool bClear = false);
```

The per-function sections below describe the semantics; where an older table still lists an
`ErrorCode`, treat it as historical — the native contract above applies.

### 5.1 `Ping()`

Liveness probe.

- Returns `"pong"` (`ReturnValue: "pong"`) when the module is loaded and the call reached it.
- No error struct.

### 5.2 `GetEnumInfo(Type)`

Describes one enum. `Type` must be `enum(<UEnum path>)`.

Result (`FEditorAutomationGetEnumInfoResult`):

```
ErrorCode, ErrorMessage, Hint,
Info: {
  Enum:    object path of the UEnum (string),
  Entries: [ { Name: string, DisplayName: string, Value: int64 (JSON number) }, ... ]
}
```

- `Name` is the canonical C++ entry name (e.g. `QueryAndPhysics`) — use it as the identifier.
- `DisplayName` is the editor display text (e.g. `Collision Enabled (Query and Physics)`); it is
  human-only and may be localized. Never use it as an identifier.
- `Value` is the numeric entry value; pass that number back when writing an enum property (as a JSON
  string inside value JSON).
- Entries whose name ends in `_MAX` are omitted (UHT bookkeeping, not a value callers can pass).

Error codes (`EEditorAutomationGetEnumInfoErrorCode`):

| Code | Name | Meaning |
|---|---|---|
| 0 | None | Success |
| 1 | InvalidArgument | `Type` is empty |
| 2 | InvalidType | `Type` does not parse, or is not an `enum(...)` Type |
| 3 | EnumNotFound | The enum path does not resolve to a `UEnum` |

### 5.3 `GetStructInfo(Type)`

Describes one struct. `Type` must be `struct(<UScriptStruct path>)`.

Result (`FEditorAutomationGetStructInfoResult`):

```
ErrorCode, ErrorMessage, Hint,
Info: { Struct: object path (string), Properties: [ { Name: string, Type: Type-string }, ... ] }
```

Error codes (`EEditorAutomationGetStructInfoErrorCode`):

| Code | Name | Meaning |
|---|---|---|
| 0 | None | Success |
| 1 | InvalidArgument | `Type` is empty |
| 2 | InvalidType | `Type` does not parse, or is not a `struct(...)` Type |
| 3 | StructNotFound | The struct path does not resolve to a `UScriptStruct` |

### 5.4 `GetClassInfo(Type)`

Describes one class. `Type` must be `class(<UClass path>)` or `object(<UClass path>)`.

Result (`FEditorAutomationGetClassInfoResult`):

```
ErrorCode, ErrorMessage, Hint,
Info: {
  Class:      class object path (string),
  Properties: [ { Name, Type }, ... ],                       // inherited members included
  Functions:  [ FEditorAutomationFunctionInfo, ... ]                // only BlueprintCallable functions
}
```

`FEditorAutomationFunctionInfo`:

```
Name: string, bBlueprintCallable: bool, bBlueprintPure: bool, bStatic: bool,
Parameters: [ { Name: string, Type: Type-string, Direction: int32 }, ... ],
ReturnType: Type-string          // empty when the function returns nothing
```

`Direction` is a plain int32 (deliberately not an enum, so Remote Control never emits a display name):

| Value | Meaning | Direction enum entry (`/Script/EditorAutomation.EEditorAutomationParameterDirection`) |
|---|---|---|
| 0 | Input | `Input` |
| 1 | Output (out-only reference) | `Output` |
| 2 | In/out (`UPARAM(ref)` reference) | `InputOutput` |

Error codes (`EEditorAutomationGetClassInfoErrorCode`):

| Code | Name | Meaning |
|---|---|---|
| 0 | None | Success |
| 1 | InvalidArgument | `Type` is empty |
| 2 | InvalidType | `Type` does not parse, or is not a `class(...)`/`object(...)` Type |
| 3 | ClassNotFound | The class path does not resolve to a `UClass` |

### 5.5 `GetFunctionInfo(Type, FunctionName)`

Describes one reflected `UFunction` of a class. Accepts `class(...)` or `object(...)` Types. The
function does **not** have to be `BlueprintCallable` to be described.

Result (`FEditorAutomationGetFunctionInfoResult`) has the same `Info` shape as
`FEditorAutomationFunctionInfo` above.

Error codes (`EEditorAutomationGetFunctionInfoErrorCode`):

| Code | Name | Meaning |
|---|---|---|
| 0 | None | Success |
| 1 | InvalidArgument | `Type` or `FunctionName` is empty |
| 2 | InvalidType | `Type` does not parse, or is not a `class(...)`/`object(...)` Type |
| 3 | ClassNotFound | The class path does not resolve to a `UClass` |
| 4 | FunctionNotFound | The class has no UFunction with that name |

### 5.6 `GetPropertyType(Type, PropertyPath)`

Schema-only resolution of a property path against a class; no object instance is needed. The result
can be cached per `(Type, PropertyPath)`.

Result (`FEditorAutomationGetPropertyTypeResult`):

```
ErrorCode, ErrorMessage, Hint, Type: Type-string
```

Error codes (`EEditorAutomationGetPropertyTypeErrorCode`):

| Code | Name | Meaning |
|---|---|---|
| 0 | None | Success |
| 1 | InvalidArgument | `Type` or `PropertyPath` is empty, or the path syntax is invalid |
| 2 | InvalidType | `Type` does not parse, or is not a `class(...)`/`object(...)` Type |
| 3 | ClassNotFound | The class path does not resolve to a `UClass` |
| 4 | PropertyNotFound | The path names a property that does not exist, or cannot be traversed |
| 5 | UnsupportedProperty | The leaf property's type is outside the `Type` grammar |

Note the mapping: **path syntax errors are `InvalidArgument`; resolution errors are
`PropertyNotFound`.** An index on a non-array (`RelativeLocation[0]`) is a resolution error.

### 5.7 `GetPropertyValue(ObjectPath, PropertyPath)`

Reads one property of a loaded object as nested JSON text.

- `ObjectPath` is any path `LoadObject` resolves. CDOs work, e.g.
  `/Script/Engine.Default__StaticMeshComponent`; level actors use their actor object path.
- `PropertyPath` is a dotted path, see §6.

Result (`FEditorAutomationGetPropertyValueResult`):

```
ErrorCode, ErrorMessage, Hint, ValueJson: nested JSON text (string, see §4)
```

Error codes (`EEditorAutomationGetPropertyValueErrorCode`):

| Code | Name | Meaning |
|---|---|---|
| 0 | None | Success |
| 1 | InvalidArgument | `ObjectPath`/`PropertyPath` is empty, or the path syntax is invalid |
| 2 | ObjectNotFound | `ObjectPath` does not resolve to a `UObject` |
| 3 | PropertyNotFound | The path does not resolve on the object (missing property, null object mid-path, bad index, …) |
| 4 | UnsupportedProperty | The leaf property's type is outside the `Type` grammar |
| 5 | SerializationFailed | The value exists but could not be converted to JSON (treated as a plugin bug) |

### 5.8 `SetPropertyValue(ObjectPath, PropertyPath, ValueJson, bGenerateTransaction)`

Writes one property of a loaded object from nested JSON text.

Behavior, in order:

1. Validate arguments, load the object, parse and resolve the property path.
2. Read the old value into `OldValueJson` (this is the rollback snapshot).
3. If `bGenerateTransaction` is true, open an editor transaction (`Set EditorAutomation Property`).
4. `Object->Modify()`, write `ValueJson` into the property. If the write fails, roll back to
   `OldValueJson` (see “Atomic failure” in §4) and return the error.
5. On success call `PostEditChangeProperty`, then `MarkPackageDirty()`.
6. Read the value back into `NewValueJson`.

Result (`FEditorAutomationSetPropertyValueResult`):

```
ErrorCode, ErrorMessage, Hint,
OldValueJson: nested JSON text of the value before the write,
NewValueJson: nested JSON text of the value after the write
```

On failure `OldValueJson` and `NewValueJson` are empty.

Error codes (`EEditorAutomationSetPropertyValueErrorCode`):

| Code | Name | Meaning |
|---|---|---|
| 0 | None | Success |
| 1 | InvalidArgument | `ObjectPath`, `PropertyPath` or `ValueJson` is empty, or the path syntax is invalid |
| 2 | ObjectNotFound | `ObjectPath` does not resolve to a `UObject` |
| 3 | PropertyNotFound | The path does not resolve on the object |
| 4 | InvalidJson | The JSON does not parse, does not match the property's type, or the property type cannot be written (delegates, unknown types). A failed aggregate write is rolled back to the previous value — see §4 |
| 5 | UnsupportedProperty | The leaf property's type is outside the `Type` grammar (checked before the write) |
| 6 | SerializationFailed | Reading the value before/after the write failed (treated as a plugin bug) |
| 7 | WriteFailed | Reserved; not currently returned |

Notes:

- `SetPropertyValue` is atomic on failure: an invalid write either leaves the value untouched (a scalar
  rejected before it was written, e.g. a delegate property) or rolls the value back to the pre-write
  snapshot and says so in `ErrorMessage`; see §4. If the rollback itself fails, the result reports it
  and the property may be partially modified.
- `bGenerateTransaction` adds undo support on top of that; it is not required for atomicity.
- Writing a CDO changes the template for the current editor process. Tests and read-only adapters
  should restore values they modify.
- A successful write fires `PostEditChangeProperty` and marks the object's package dirty, so the
  editor may show the package as needing save.

### 5.9 `CallFunction(ObjectPath, FunctionName, ParametersJson, bGenerateTransaction)`

Calls a `UFunction` on a loaded object with nested-JSON parameters.

- `ParametersJson` is a JSON **object keyed by UFunction parameter name**, or `"{}"`/empty for none.
- Input parameters and in/out parameters are read from it. Out-only parameters (`Direction == 1`)
  cannot be supplied and are ignored if present.
- Omitted parameters are zero-initialized (`false`, `0`, empty string, null object, default-constructed
  struct); C++ default arguments are **not** applied through reflection. Set every parameter that
  matters explicitly.
- Unknown keys are ignored.
- The function is found with `UObject::FindFunction`, so any reflected UFunction of the object's class
  can be called — `BlueprintCallable` is not required by this entry point.
- If `bGenerateTransaction` is true, `CallFunction` opens a transaction (`Call EditorAutomation Function`)
  and calls `Object->Modify()` first, so the call can be undone in the editor.

Result (`FEditorAutomationCallFunctionResult`):

```
ErrorCode, ErrorMessage, Hint,
ReturnValueJson: nested JSON text of the return value; empty when the function returns nothing,
OutParametersJson: JSON object text keyed by out-parameter name; "{}" when there are none,
ReturnType: Type-string of the return value; empty when the function returns nothing
```

`ReturnValueJson` is text, not a JSON value: for a `string` return it is `"\"pong\""`; for a struct
return it is the serialized struct object.

On failure all three output fields are empty (not `"{}"`).

Error codes (`EEditorAutomationCallFunctionErrorCode`):

| Code | Name | Meaning |
|---|---|---|
| 0 | None | Success |
| 1 | InvalidArgument | `ObjectPath` or `FunctionName` is empty |
| 2 | ObjectNotFound | `ObjectPath` does not resolve to a `UObject` |
| 3 | FunctionNotFound | The object's class has no UFunction with that name |
| 4 | InvalidJson | `ParametersJson` is non-empty but is not a parsable JSON object |
| 5 | ParameterError | One parameter's JSON value does not match its declared type |
| 6 | SerializationFailed | The return value or an out parameter could not be serialized (treated as a plugin bug) |
| 7 | CallFailed | Reserved; not currently returned |

### 5.10 `GetObjectClass(ObjectPath)`

Resolves one object path to the canonical class Type of the object's class. Use it to type arbitrary
object paths before asking for schema information — especially level instances, whose paths name no
class (for example `/Game/Maps/X.X:PersistentLevel.Cube_1`).

- `ObjectPath` is any path `LoadObject` resolves while the object is loaded: a CDO
  (`/Script/Engine.Default__StaticMeshComponent`), a script object
  (`/Script/Engine.StaticMeshComponent`), an asset (`/Engine/EngineResources/DefaultTexture`), or a
  level instance path.
- `ClassType` is the canonical `class(<class path>)` Type and is accepted directly by `GetClassInfo`,
  `GetFunctionInfo` and `GetPropertyType` — for example `class(/Script/Engine.StaticMeshComponent)`,
  `class(/Script/Engine.World)`, or a generated Blueprint class path such as
  `class(/Game/Blueprints/BP_Cube.BP_Cube_C)`.
- The class of a class object is itself a class: `GetObjectClass("/Script/Engine.StaticMeshComponent")`
  returns `class(/Script/CoreUObject.Class)`.

Result (`FEditorAutomationGetObjectClassResult`):

```
ErrorCode, ErrorMessage, Hint, ClassType: canonical class Type string
```

Error codes (`EEditorAutomationGetObjectClassErrorCode`):

| Code | Name | Meaning |
|---|---|---|
| 0 | None | Success |
| 1 | InvalidArgument | `ObjectPath` is empty |
| 2 | ObjectNotFound | `ObjectPath` does not resolve to a loaded `UObject` |

### 5.11 `GetEditorInfo()`

Describes the process EditorAutomation is running in. Like `Ping`, this has no error envelope: it returns the
info struct directly, so it also works as an editor-identity sanity check for an adapter.

Result (`FEditorAutomationEditorInfo`, returned directly and wrapped in `ReturnValue` by Remote Control):

| Field | Type | Meaning |
|---|---|---|
| `ProcessId` | int64 (JSON number) | Operating-system process id of the editor (or game) process. |
| `EngineVersion` | string | Engine version text, e.g. `5.5.0-0+UE5` (the exact format comes from `FEngineVersion::Current().ToString()` and varies by build). |
| `ProjectName` | string | Project name, e.g. `Game`. |
| `ProjectDir` | string | Full project directory, e.g. `C:/PROJECTS/UnrealEngine/Game/` (trailing separator included; forward slashes). |
| `bIsEditor` | bool | `true` in the editor, `false` in a cooked game build. |

Values come from `FPlatformProcess::GetCurrentProcessId()`, `FEngineVersion::Current().ToString()`,
`FApp::GetProjectName()`, `FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())` and `GIsEditor`.
The process id is the value the adapter can compare against the OS process that owns Remote Control's
port.

Sample Remote Control response (raw RC uses native JSON types; the DSH `unreal_*` tool layer
canonicalizes scalars to JSON strings by its own convention):

```json
{
  "ReturnValue": {
    "ProcessId": 76644,
    "EngineVersion": "5.5.0-0+UE5",
    "ProjectName": "Game",
    "ProjectDir": "C:/PROJECTS/UnrealEngine/Game/",
    "bIsEditor": true
  }
}
```

### 5.12 `CallFunctions(CallsJson)` (batch)

Calls several UFunctions sequentially in one round trip. Use it to cut Remote Control chatter when a
task is a known list of calls; each entry is the same shape as one `CallFunction` request.

- `CallsJson` is a JSON **array** of objects:
  `{"objectPath": "...", "functionName": "...", "parametersJson": "{...}", "generateTransaction": false}`.
  `parametersJson` defaults to `"{}"`; `generateTransaction` defaults to `false`.
- Calls run in array order on the game thread, so later calls may consume side effects of earlier ones.
- A failing call does not stop the batch; every entry reports its own result.
- Limits: non-empty array, at most **256** calls.

Result (`FEditorAutomationBatchCallResult`):

```
bSuccess: true only when the batch ran and all entries succeeded,
ErrorMessage: batch-level failure (bad JSON, empty array, too many calls); empty when the batch ran,
Results: array of FEditorAutomationBatchCallEntry {
  Index, ObjectPath, FunctionName, ErrorCode, ErrorMessage, Hint,
  ReturnValueJson, OutParametersJson, ReturnType
}
```

`ErrorCode` inside an entry is an `EEditorAutomationCallFunctionErrorCode` integer (see 5.9). A batch-level
failure returns `bSuccess=false` **and** an empty `Results` array.

### 5.13 `CaptureScreenshot(FilePath, Target, bShowUI)`

Captures a PNG screenshot; `Target` selects the source.

- `Target = GameViewport` (0, default): the running PIE game viewport ("game picture"), through
  `FScreenshotRequest`. This is **asynchronous**: the call only requests the capture, the engine writes
  the file after the next rendered frame. The result sets `bRequested=true`; poll `FilePath` for the file.
  Capturing without a PIE game viewport fails with a descriptive `ErrorMessage`.
- `Target = EditorWindow` (1): the active editor window, through Slate. **Synchronous**: the PNG is
  written before the call returns.
- `bShowUI` (game viewport only): whether HUD/UMG overlays are included in the capture.
- An empty `FilePath` generates `<Project>/Saved/Screenshots/EditorAutomation_<timestamp>.png`; the absolute
  path is always returned in `FilePath`.

Result (`FEditorAutomationScreenshotResult`): `bSuccess, bRequested, FilePath, ErrorMessage`.

### 5.14 `GetRecentLog(MaxLines, MinVerbosity, CategoryFilter, bClear)`

Reads recent editor diagnostics from the **in-memory** capture: EditorAutomationCore registers a bounded
`FOutputDevice` with `GLog` at module load and records Warning and above into a ring buffer
(4096 lines, 2048 chars per line), so a caller can inspect what the editor reported without reading
or rotating log files. Verbose/Log traffic is not captured.

- `MaxLines`: page size, default 200, clamped to 1..2000. Entries are returned oldest-first.
- `MinVerbosity`: `ELogVerbosity` numeric minimum severity — `1` Fatal, `2` Error, `3` Warning
  (default); the capture holds nothing below Warning.
- `CategoryFilter`: case-insensitive substring match on the log category, e.g. `"LogSavePackage"`.
- `bClear`: clear the buffer after copying.

Result (`FEditorAutomationLogPage`): `Entries` (`[{Time, Verbosity, Category, Message}]`, oldest first),
`TotalRecorded` (lines captured since the editor started, unfiltered), `bTruncated` (more matches
existed than the page holds).

---

`PropertyPath` (used by `GetPropertyType`, `GetPropertyValue`, `SetPropertyValue`) is a dotted path:

```
PropertyPath := segment ( "." segment )*
segment      := identifier ( "[" digits "]" )?
identifier   := [A-Za-z_][A-Za-z0-9_]*
```

Examples:

```
RelativeLocation            // struct member of a component
RelativeLocation.X          // nested struct member
OverrideMaterials[0]        // array element
BodyInstance.CollisionEnabled
```

Traversal rules:

- A non-final segment must be a struct or a non-null object reference; arrays can only be traversed
  element-wise with `[index]`.
- `[index]` is only valid on an `array` property (the property must actually be a `TArray`). Indexes
  are zero-based; an out-of-range index fails resolution.
- Map keys and set iteration are not addressable through a path; use `GetPropertyValue` on the whole
  map/set and address the elements through their JSON encoding instead.
- `GetPropertyType` resolves against the class schema, so object-reference traversal uses the declared
  class; the value-based entry points require the intermediate object to be non-null.

Path syntax errors are reported as `InvalidArgument` by all three functions; resolution failures as
`PropertyNotFound` (or `ClassNotFound`/`ObjectNotFound` where applicable).

---

## 7. Remote Control transport notes

- Endpoint: `PUT http://127.0.0.1:30010/remote/object/call`.
- Response wrapper: `{"ReturnValue": ...}`.
- A non-2xx response with `{"errorMessage": "..."}` is a transport/reflection error raised by Remote
  Control before the plugin runs (unknown object/function, mismatched argument JSON, …). Do not
  confuse it with the plugin's `ReturnValue.ErrorCode`.
- Remote Control serializes reflected int64 fields as JSON numbers (for example
  `FEditorAutomationEnumEntry.Value`); values are small enum ordinals, so no precision loss occurs in
  practice. Inside value JSON, int64 is encoded as a string.
- Struct result fields (`Info`, `Parameters`, `Entries`, …) are regular JSON objects/arrays in the RC
  response; only the `*Json` fields are strings that must be parsed again.
- Remote Control serializes a null `UObject` field of a result struct as an **empty string** (for
  example `Info.Enum` in a failed `GetEnumInfo` result). Value JSON, in contrast, encodes unset
  references as JSON `null`.
- Remote Control parameter names are the exact UFunction names shown in the signatures in §5. In
  particular: `Type`, `FunctionName`, `PropertyPath`, `ObjectPath`, `ValueJson`, `ParametersJson`,
  `bGenerateTransaction`.
- Requests execute on the editor's game thread; long operations block the editor, so the adapter
  should keep calls reasonably small.
- Domain enums exposed for callers:
  - The parameter-direction enum is `/Script/EditorAutomation.EEditorAutomationParameterDirection` with entries
    `Input` (0), `Output` (1), `InputOutput` (2). Resolve it through `GetEnumInfo` if needed; on the
    wire `Direction` is an integer.
  - Error-code enums are not reflected; use the integer tables in §5.

---

## 8. Error-code quick reference

| Function | Codes |
|---|---|
| `Ping` | (none; returns `"pong"`) |
| `GetEditorInfo` | (none; returns `FEditorAutomationEditorInfo` directly) |
| `GetEnumInfo` | 0 None, 1 InvalidArgument, 2 InvalidType, 3 EnumNotFound |
| `GetStructInfo` | 0 None, 1 InvalidArgument, 2 InvalidType, 3 StructNotFound |
| `GetClassInfo` | 0 None, 1 InvalidArgument, 2 InvalidType, 3 ClassNotFound |
| `GetFunctionInfo` | 0 None, 1 InvalidArgument, 2 InvalidType, 3 ClassNotFound, 4 FunctionNotFound |
| `GetObjectClass` | 0 None, 1 InvalidArgument, 2 ObjectNotFound |
| `GetPropertyType` | 0 None, 1 InvalidArgument, 2 InvalidType, 3 ClassNotFound, 4 PropertyNotFound, 5 UnsupportedProperty |
| `GetPropertyValue` | 0 None, 1 InvalidArgument, 2 ObjectNotFound, 3 PropertyNotFound, 4 UnsupportedProperty, 5 SerializationFailed |
| `SetPropertyValue` | 0 None, 1 InvalidArgument, 2 ObjectNotFound, 3 PropertyNotFound, 4 InvalidJson, 5 UnsupportedProperty, 6 SerializationFailed, 7 WriteFailed (reserved) |
| `CallFunction` | 0 None, 1 InvalidArgument, 2 ObjectNotFound, 3 FunctionNotFound, 4 InvalidJson, 5 ParameterError, 6 SerializationFailed, 7 CallFailed (reserved) |
| `CallFunctions` | Batch level: `bSuccess=false` + `ErrorMessage` (invalid JSON, empty array, >256 calls). Per entry: same codes as `CallFunction` |
| `CaptureScreenshot` | (no error code; `bSuccess=false` + `ErrorMessage`) |
| `GetRecentLog` | (no error code; unavailable categories/filters simply return an empty page) |

---

## 9. Test fixtures used by the automation tests

The automation tests call the library directly (no HTTP) against stable engine CDOs. They are a good
reference for expected shapes:

- `/Script/EditorAutomation.Default__EditorAutomationCoreLibrary` — the library CDO (`Ping`, `CallFunction` on `Ping`
  and `GetEnumInfo`).
- `/Script/Engine.Default__StaticMeshComponent` — class/struct/property fixtures:
  `Mobility` (`enum(/Script/Engine.EComponentMobility)`, value `2`), `RelativeLocation`
  (`struct(/Script/CoreUObject.Vector)`), `bCastDynamicShadow` (`bool`),
  `OverrideMaterials` (`array(object(/Script/Engine.MaterialInterface))`), `StaticMesh` (null object
  reference), `GetLocalBounds` (two out parameters, no return value).
- `/Script/Engine.Default__Actor` — `OnActorBeginOverlap` (a delegate: reads as JSON `null`, cannot
  be written).
- `/Script/Engine.ECollisionEnabled` — 6-entry namespace enum with display names.
