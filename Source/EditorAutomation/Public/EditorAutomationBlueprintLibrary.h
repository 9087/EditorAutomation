// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
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
	/** Read a Blueprint's identity and state; an empty struct means the asset could not be loaded. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static FEditorAutomationBlueprintInfo GetBlueprintInfo(const TSoftObjectPtr<UBlueprint>& Blueprint);

	/** Mark the Blueprint dirty or clean; returns false when the asset cannot be loaded. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static bool SetBlueprintDirty(const TSoftObjectPtr<UBlueprint>& Blueprint, bool bDirty);

	/** Compile the Blueprint; returns false when it compiles with errors (the reason is logged). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static bool CompileBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint);

	/** Save the Blueprint asset; returns false and logs why when the editor refuses. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static bool SaveBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint);

	/** Create a Blueprint asset; returns false and logs why when creation fails. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static bool CreateBlueprint(
		const FString& AssetPath,
		const TSoftClassPtr<UObject>& ParentClass,
		int32 BlueprintType = 0);

	/** Delete a Blueprint asset; returns false and logs why when deletion fails. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static bool DeleteBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint);

	/** Create a Blueprint asset from a parent class path (a convenience for harness callers). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprint")
	static bool CreateBlueprintFromParentPath(
		const FString& AssetPath,
		const FString& ParentClassPath,
		int32 BlueprintType = 0);
};
