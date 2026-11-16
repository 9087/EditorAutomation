// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "EditorAutomationLogCapture.h"

#include "EditorAutomationCoreLibrary.generated.h"

class UEnum;
class UScriptStruct;
class UClass;
class UObject;

// ============================================================================
// Shared types
// ============================================================================

/** One localizable FText reference: identity plus the native fallback text. */
USTRUCT(BlueprintType)
struct FEditorAutomationTextRef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Namespace;
	UPROPERTY(BlueprintReadOnly) FString Key;
	UPROPERTY(BlueprintReadOnly) FString SourceString;
	UPROPERTY(BlueprintReadOnly) int64 SourceHash = 0;
	UPROPERTY(BlueprintReadOnly) FString TableId;
	UPROPERTY(BlueprintReadOnly) FString NativeText;
};

/** One property of a struct or class. */
USTRUCT(BlueprintType)
struct FEditorAutomationPropertyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Name;
	/** Canonical Type string, e.g. "double" or "struct(/Script/CoreUObject.Vector)". */
	UPROPERTY(BlueprintReadOnly) FString Type;
};

/** Parameter direction; the wire value is a plain integer so Remote Control never emits a display name. */
UENUM(BlueprintType)
enum class EEditorAutomationParameterDirection : uint8
{
	Input = 0,
	/** Out-only parameter (a plain non-const reference). */
	Output = 1,
	/** In/out parameter (a UPARAM(ref) non-const reference). */
	InputOutput = 2
};

/** One parameter of a BlueprintCallable function. */
USTRUCT(BlueprintType)
struct FEditorAutomationParameterInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString Type;
	/** UE display name of the parameter. */
	UPROPERTY(BlueprintReadOnly) FEditorAutomationTextRef DisplayNameText;
	/** EEditorAutomationParameterDirection as an integer; resolve names through GetEnumInfo. */
	UPROPERTY(BlueprintReadOnly) int32 Direction = 0;
};

/** One BlueprintCallable function of a class: its parameters and return type (empty when it returns nothing). */
USTRUCT(BlueprintType)
struct FEditorAutomationFunctionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) bool bBlueprintCallable = false;
	UPROPERTY(BlueprintReadOnly) bool bBlueprintPure = false;
	UPROPERTY(BlueprintReadOnly) bool bStatic = false;
	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationParameterInfo> Parameters;
	UPROPERTY(BlueprintReadOnly) FString ReturnType;
};

// ============================================================================
// Editor info
// ============================================================================



/** What a screenshot captures; the wire value is a plain integer. */
UENUM(BlueprintType)
enum class EEditorAutomationScreenshotTarget : uint8
{
	/** The running PIE game viewport (the game picture). */
	GameViewport = 0,
	/** The active editor window (Slate window capture). */
	EditorWindow = 1
};



/** Identity of the editor (or game) process EditorAutomation is running in; returned by GetEditorInfo. */
USTRUCT(BlueprintType)
struct FEditorAutomationEditorInfo
{
	GENERATED_BODY()

	/** Operating-system process id of this process; useful to prove which editor served a request. */
	UPROPERTY(BlueprintReadOnly) int64 ProcessId = 0;
	/** Engine version text, e.g. "5.5.0-0+UE5". */
	UPROPERTY(BlueprintReadOnly) FString EngineVersion;
	/** Project name, e.g. "Game". */
	UPROPERTY(BlueprintReadOnly) FString ProjectName;
	/** Full project directory, e.g. "C:/PROJECTS/UnrealEngine/Game/". */
	UPROPERTY(BlueprintReadOnly) FString ProjectDir;
	/** True in the editor, false in a cooked game build. */
	UPROPERTY(BlueprintReadOnly) bool bIsEditor = false;
};

// ============================================================================
// Enum
// ============================================================================

/** One entry of an enumerated type. */
USTRUCT(BlueprintType)
struct FEditorAutomationEnumEntry
{
	GENERATED_BODY()

	/** The canonical C++ entry name, e.g. "QueryAndPhysics". */
	UPROPERTY(BlueprintReadOnly) FString Name;
	/** The editor display name, e.g. "Query And Physics". */
	UPROPERTY(BlueprintReadOnly) FString DisplayName;
	UPROPERTY(BlueprintReadOnly) int64 Value = 0;
};

/** The data of GetEnumInfo. */
USTRUCT(BlueprintType)
struct FEditorAutomationEnumInfo
{
	GENERATED_BODY()

	/** The resolved enum object; serializes over Remote Control as its object path. */
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UEnum> Enum = nullptr;
	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationEnumEntry> Entries;
};

// ============================================================================
// Struct
// ============================================================================

/** The data of GetStructInfo. */
USTRUCT(BlueprintType)
struct FEditorAutomationStructInfo
{
	GENERATED_BODY()

	/** The resolved struct object; serializes over Remote Control as its object path. */
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UScriptStruct> Struct = nullptr;
	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationPropertyInfo> Properties;
};

// ============================================================================
// Class
// ============================================================================

/** The data of GetClassInfo. */
USTRUCT(BlueprintType)
struct FEditorAutomationClassInfo
{
	GENERATED_BODY()

	/** The resolved class; serializes over Remote Control as its object path. */
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UClass> Class = nullptr;
	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationPropertyInfo> Properties;
	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationFunctionInfo> Functions;
};

// ============================================================================
// Object
// ============================================================================

// ============================================================================
// Property
// ============================================================================

/** The data of GetPropertyType: the canonical Type plus the property's display name. */
USTRUCT(BlueprintType)
struct FEditorAutomationPropertyTypeInfo
{
	GENERATED_BODY()

	/** Canonical Type string, e.g. "double" or "struct(/Script/CoreUObject.Vector)". */
	UPROPERTY(BlueprintReadOnly) FString Type;
	/** UE display name of the resolved property. */
	UPROPERTY(BlueprintReadOnly) FEditorAutomationTextRef DisplayNameText;
};

/** The outcome of one SetPropertyValue call: success plus the values around the write. */
USTRUCT(BlueprintType)
struct FEditorAutomationPropertyWrite
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
	/** The value before the write, as nested JSON text. */
	UPROPERTY(BlueprintReadOnly) FString OldValueJson;
	/** The value after the write, as nested JSON text. */
	UPROPERTY(BlueprintReadOnly) FString NewValueJson;
	/** UE display name of the resolved property. */
	UPROPERTY(BlueprintReadOnly) FEditorAutomationTextRef DisplayNameText;
};
/** Failure codes of CallFunction; the wire value is a plain integer. */
enum class EEditorAutomationCallFunctionErrorCode : int32
{
	None = 0,
	InvalidArgument = 1,
	ObjectNotFound = 2,
	FunctionNotFound = 3,
	InvalidJson = 4,
	ParameterError = 5,
	SerializationFailed = 6,
	CallFailed = 7
};

/** The outcome of one CallFunction call. */
USTRUCT(BlueprintType)
struct FEditorAutomationCallFunctionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 ErrorCode = 0;
	UPROPERTY(BlueprintReadOnly) FString ErrorMessage;
	UPROPERTY(BlueprintReadOnly) FString Hint;
	/** Return value as nested JSON text; empty when the function returns nothing. */
	UPROPERTY(BlueprintReadOnly) FString ReturnValueJson;
	/** Out parameters as a JSON object keyed by parameter name; "{}" when there are none. */
	UPROPERTY(BlueprintReadOnly) FString OutParametersJson;
	/** Canonical Type string of the return value; empty when the function returns nothing. */
	UPROPERTY(BlueprintReadOnly) FString ReturnType;

	static FEditorAutomationCallFunctionResult Failed(
		const EEditorAutomationCallFunctionErrorCode ErrorCode,
		const FString& ErrorMessage,
		const FString& Hint = FString())
	{
		FEditorAutomationCallFunctionResult Result;
		Result.ErrorCode = static_cast<int32>(ErrorCode);
		Result.ErrorMessage = ErrorMessage;
		Result.Hint = Hint;
		return Result;
	}
};

/** One entry of a batched CallFunctions request: the outcome plus the request it belonged to. */
USTRUCT(BlueprintType)
struct FEditorAutomationBatchCallEntry
{
	GENERATED_BODY()

	/** Zero-based position of this call in the request array. */
	UPROPERTY(BlueprintReadOnly) int32 Index = 0;
	UPROPERTY(BlueprintReadOnly) FString ObjectPath;
	UPROPERTY(BlueprintReadOnly) FString FunctionName;
	/** EEditorAutomationCallFunctionErrorCode as an integer; 0 means success. */
	UPROPERTY(BlueprintReadOnly) int32 ErrorCode = 0;
	UPROPERTY(BlueprintReadOnly) FString ErrorMessage;
	UPROPERTY(BlueprintReadOnly) FString Hint;
	UPROPERTY(BlueprintReadOnly) FString ReturnValueJson;
	UPROPERTY(BlueprintReadOnly) FString OutParametersJson;
	UPROPERTY(BlueprintReadOnly) FString ReturnType;
};

/** The outcome of one CallFunctions batch. */
USTRUCT(BlueprintType)
struct FEditorAutomationBatchCallResult
{
	GENERATED_BODY()

	/** True when the batch was accepted and every entry succeeded. */
	UPROPERTY(BlueprintReadOnly) bool bSuccess = true;
	/** Batch-level failure (bad JSON, too many calls); empty when the batch ran. */
	UPROPERTY(BlueprintReadOnly) FString ErrorMessage;
	/** Per-call outcomes, in request order. */
	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationBatchCallEntry> Results;
};

// ============================================================================
// Library
// ============================================================================

/**
 * RC-facing entry points of EditorAutomation.
 *
 * Reached from Remote Control with the CDO path
 * `/Script/EditorAutomation.Default__EditorAutomationCoreLibrary`.
 */
UCLASS()
class EDITORAUTOMATION_API UEditorAutomationCoreLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ------------------------------------------------------------------------
	// System
	// ------------------------------------------------------------------------

	/**
	 * Liveness probe for external callers.
	 *
	 * @returns "pong" when the plugin module is loaded and the call reached it.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FString Ping();

	/**
	 * Describe the process EditorAutomation is running in.
	 *
	 * Returns the operating-system process id, engine version, project name and directory, and whether
	 * this is an editor (rather than a cooked game build). Useful to identify which editor served a
	 * request and to sanity-check an adapter-to-editor connection.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FEditorAutomationEditorInfo GetEditorInfo();

	/** Start PIE; an empty LevelPath uses the editor's current level. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static bool StartPIE(const FString& LevelPath = TEXT(""), bool bNewWindow = false);

	/**
	 * Capture a screenshot into a PNG file.
	 *
	 * Target selects the source: the running PIE game viewport (the "game picture") or the active
	 * editor window. The game-viewport path is asynchronous: the call only requests the capture and
	 * the engine writes the file after the next rendered frame, so callers must poll for the file.
	 * The editor-window path is synchronous and the file exists when this returns (bSuccess).
	 * An empty FilePath generates <Project>/Saved/Screenshots/EditorAutomation_<timestamp>.png.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationPIE")
	static FString CaptureScreenshot(
		const FString& FilePath,
		EEditorAutomationScreenshotTarget Target = EEditorAutomationScreenshotTarget::GameViewport,
		bool bShowUI = true);

	/** Request the current PIE session to stop. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static bool StopPIE();

	/** Whether a PIE session is running or queued. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static bool IsPIERunning();

	/**
	 * Bring the running PIE game viewport to the front and make it own keyboard focus, and force
	 * game-only input mode on the local player controller.
	 *
	 * Generic PIE debug helper: physical keyboard input reaches the game's PlayerController only
	 * while the game viewport owns keyboard focus. A focusable UMG widget would steal it instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationPIE")
	static bool FocusPIEViewport();

	/**
	 * Simulate one key press (key down + key up) inside the running PIE session.
	 *
	 * KeyName is an FKey name such as "Up", "Down", "Left", "Right", "W", "A", "S", "D", "R",
	 * "Space", "Enter". When bFocusViewport is true the game viewport is focused first, so the
	 * event is routed to the game rather than to whatever editor widget had focus.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationPIE")
	static bool SendKeyToPIE(const FString& KeyName, bool bFocusViewport = true);

	// ------------------------------------------------------------------------
	// Reflection
	// ------------------------------------------------------------------------

	/**
	 * Describe one enumerated type: every entry's name, display name and numeric value.
	 *
	 * @param Type The enum Type, e.g. "enum(/Script/Engine.ECollisionEnabled)".
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FEditorAutomationEnumInfo GetEnumInfo(const FString& Type);

	/**
	 * Describe one struct: every member's name and type.
	 *
	 * @param Type The struct Type, e.g. "struct(/Script/CoreUObject.Vector)".
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FEditorAutomationStructInfo GetStructInfo(const FString& Type);

	/**
	 * Describe one class: its properties and BlueprintCallable functions.
	 *
	 * Accepts a class Type ("class(/Script/Engine.Actor)") or an object Type ("object(...)").
	 *
	 * @param Type The class Type, e.g. "class(/Script/Engine.StaticMeshComponent)".
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FEditorAutomationClassInfo GetClassInfo(const FString& Type);

	/**
	 * Describe one function of a class: its parameters, directions and return type.
	 *
	 * @param Type         The class/object Type, e.g. "class(/Script/Engine.StaticMeshComponent)".
	 * @param FunctionName The UFunction name, e.g. "GetClosestPointOnCollision".
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FEditorAutomationFunctionInfo GetFunctionInfo(const FString& Type, const FString& FunctionName);

	// ------------------------------------------------------------------------
	// Object
	// ------------------------------------------------------------------------

	/**
	 * Resolve one object path to the canonical Type of its class.
	 *
	 * Use this to type arbitrary object paths - especially level instances, whose paths name no class -
	 * before asking for schema information: the returned ClassType is accepted directly by
	 * GetClassInfo, GetFunctionInfo and GetPropertyType.
	 *
	 * @param ObjectPath Full object path, e.g. "/Script/Engine.Default__StaticMeshComponent" or a level
	 *                   instance path such as "/Game/Maps/Map.Map:PersistentLevel.Cube_1".
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FString GetObjectClass(const FString& ObjectPath);

	// ------------------------------------------------------------------------
	// Property
	// ------------------------------------------------------------------------

	/**
	 * Resolve the Type of one property path without touching an instance.
	 *
	 * Purely schema based, so callers can cache the result per (Type, PropertyPath).
	 *
	 * @param Type         The class/object Type, e.g. "class(/Script/Engine.StaticMeshComponent)".
	 * @param PropertyPath The property path, e.g. "RelativeLocation.X".
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FEditorAutomationPropertyTypeInfo GetPropertyType(const FString& Type, const FString& PropertyPath);

	/**
	 * Read one property of an object as nested JSON.
	 *
	 * PropertyPath accepts dotted struct traversal and array indices, e.g. "RelativeLocation.X",
	 * "OverrideMaterials[0]".
	 *
	 * @param ObjectPath   Full object path, e.g. "/Script/Engine.Default__StaticMeshComponent".
	 * @param PropertyPath The property path to read.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FString GetPropertyValue(const FString& ObjectPath, const FString& PropertyPath);

	/**
	 * Write one property of an object from nested JSON.
	 *
	 * @param ObjectPath          Full object path.
	 * @param PropertyPath        The property path to write.
	 * @param ValueJson           The new value as nested JSON text.
	 * @param bGenerateTransaction Whether to wrap the write in an editor transaction.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FEditorAutomationPropertyWrite SetPropertyValue(
		const FString& ObjectPath,
		const FString& PropertyPath,
		const FString& ValueJson,
		bool bGenerateTransaction);

	// ------------------------------------------------------------------------
	// Function call
	// ------------------------------------------------------------------------

	/**
	 * Call one UFunction on an object with nested-JSON parameters.
	 *
	 * ParametersJson is a JSON object keyed by parameter name; omitted parameters keep their defaults.
	 * Out parameters and the return value come back as nested JSON.
	 *
	 * @param ObjectPath           Full object path.
	 * @param FunctionName         The UFunction name, e.g. "SetStaticMesh".
	 * @param ParametersJson       JSON object text with the input parameters, or "{}".
	 * @param bGenerateTransaction Whether to wrap the call in an editor transaction.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FEditorAutomationCallFunctionResult CallFunction(
		const FString& ObjectPath,
		const FString& FunctionName,
		const FString& ParametersJson,
		bool bGenerateTransaction);

	/**
	 * Call several UFunctions in one round trip.
	 *
	 * CallsJson is a JSON array; each element is {"objectPath": "...", "functionName": "...",
	 * "parametersJson": "{...}", "generateTransaction": false}. Calls run sequentially in order,
	 * so later calls can consume the side effects of earlier ones. Failures do not stop the batch:
	 * every entry reports its own ErrorCode/ErrorMessage and the batch bSuccess is true only when
	 * all entries succeeded. Up to 256 calls per batch.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FEditorAutomationBatchCallResult CallFunctions(const FString& CallsJson);

	/**
	 * Read recent editor diagnostics from the in-memory log capture.
	 *
	 * EditorAutomationCore records warning/error lines from the engine output log into a bounded
	 * ring buffer (4096 lines, Warning and above) as soon as the module loads, so a harness
	 * can inspect what the editor reported without reading or rotating log files.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation")
	static FEditorAutomationLogPage GetRecentLog(
		int32 MaxLines = 200,
		int32 MinVerbosity = 3,
		const FString& CategoryFilter = TEXT(""),
		bool bClear = false);
};

