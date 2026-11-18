// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "EditorAutomationCoreLibrary.h"

#include "EditorAutomationLevelLibrary.generated.h"

class ULevelScriptBlueprint;

/** Current editor level identity and dirty state. */
USTRUCT(BlueprintType)
struct FEditorAutomationLevelInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString LevelPath;
	UPROPERTY(BlueprintReadOnly) FString LevelName;
	UPROPERTY(BlueprintReadOnly) bool bDirty = false;
};

UCLASS()
class EDITORAUTOMATION_API UEditorAutomationLevelLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Create a Level asset. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static FEditorAutomationResult CreateLevel(
		const FString& LevelPath,
		const FString& TemplatePath = TEXT(""),
		bool bPartitioned = false);

	/** Open a Level asset in the editor. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static FEditorAutomationResult OpenLevel(const FString& LevelPath);

	/** Save the current Level. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static FEditorAutomationResult SaveCurrentLevel();

	/** Describe the current editor Level. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static FEditorAutomationResult GetCurrentLevelInfo(FEditorAutomationLevelInfo& OutInfo);

	/** Whether the current editor Level has unsaved changes. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static FEditorAutomationResult IsCurrentLevelDirty(bool& OutDirty);

	/** The current Level's Level Script Blueprint, when it has one. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static FEditorAutomationResult GetLevelScriptBlueprint(ULevelScriptBlueprint*& OutBlueprint);
};
