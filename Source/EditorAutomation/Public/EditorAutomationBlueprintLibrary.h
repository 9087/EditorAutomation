// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "EditorAutomationCoreLibrary.h"

#include "EditorAutomationBlueprintLibrary.generated.h"

class UBlueprint;

/** Identity and compile state of one Blueprint asset. */
USTRUCT(BlueprintType)
struct FEditorAutomationBlueprintInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString AssetPath;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UClass> AssetClass = nullptr;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UClass> ParentClass = nullptr;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UClass> GeneratedClass = nullptr;
	/** EBlueprintType as an integer. */
	UPROPERTY(BlueprintReadOnly) int32 BlueprintType = 0;
	/** EBlueprintStatus as an integer. */
	UPROPERTY(BlueprintReadOnly) int32 Status = 0;
	UPROPERTY(BlueprintReadOnly) bool bDirty = false;
	UPROPERTY(BlueprintReadOnly) bool bBeingCompiled = false;
};


UCLASS()
class EDITORAUTOMATION_API UEditorAutomationBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Read a Blueprint's identity and state.
	 *
	 * Blueprint accepts an asset/package path (/Game/UI/WBP_X), a full object path
	 * (/Game/UI/WBP_X.WBP_X), a generated-class path (/Game/UI/WBP_X.WBP_X_C) or a
	 * disk filename; a widened form sets the result's Hint to the preferred object path.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static FEditorAutomationResult GetBlueprintInfo(
		const TSoftObjectPtr<UBlueprint>& Blueprint,
		FEditorAutomationBlueprintInfo& OutInfo);

	/** Mark the Blueprint dirty or clean. Accepts the same path forms as GetBlueprintInfo. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static FEditorAutomationResult SetBlueprintDirty(const TSoftObjectPtr<UBlueprint>& Blueprint, bool bDirty);

	/** Compile the Blueprint. Accepts the same path forms as GetBlueprintInfo. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static FEditorAutomationResult CompileBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint);

	/** Save the Blueprint asset without any save dialog. Accepts the same path forms as GetBlueprintInfo. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static FEditorAutomationResult SaveBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint);

	/** Create a Blueprint asset. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static FEditorAutomationResult CreateBlueprint(
		const FString& AssetPath,
		const TSoftClassPtr<UObject>& ParentClass,
		int32 BlueprintType = 0);

	/** Delete a Blueprint asset. Accepts the same path forms as GetBlueprintInfo. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static FEditorAutomationResult DeleteBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint);

	/** Create a Blueprint asset from a parent class path. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static FEditorAutomationResult CreateBlueprintFromParentPath(
		const FString& AssetPath,
		const FString& ParentClassPath,
		int32 BlueprintType = 0);

	/** KGUI/blueprint-editor Lua path bound to this Blueprint (empty when none). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static FEditorAutomationResult GetBlueprintLuaPath(
		const TSoftObjectPtr<UBlueprint>& Blueprint,
		FString& OutLuaPath);

	/**
	 * Resolve one Blueprint path to a loaded asset (C++ helper, not a Remote Control entry point).
	 *
	 * Accepts a package/asset path, a full object path, a generated-class path or a disk
	 * filename. Remote Control can only bind the object-path form of a soft object path, so
	 * the other forms are widened here: a successful call reports the widening through the
	 * envelope's Hint, and a missing or ambiguous asset through ErrorCode/ErrorMessage.
	 * The resolved asset is data, so it comes back through OutBlueprint like every other
	 * operation's data output.
	 */
	static FEditorAutomationResult ResolveBlueprintPath(const FString& BlueprintPath, UBlueprint*& OutBlueprint);
};
