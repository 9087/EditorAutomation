// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
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
	/** Create a Level asset; returns false (and logs the reason) when the editor refuses. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static bool CreateLevel(
		const FString& LevelPath,
		const FString& TemplatePath = TEXT(""),
		bool bPartitioned = false);

	/** Open a Level asset in the editor; returns false (and logs the reason) when it cannot load. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static bool OpenLevel(const FString& LevelPath);

	/** Save the current Level; returns false (and logs the reason) when the editor refuses. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static bool SaveCurrentLevel();

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static FEditorAutomationLevelInfo GetCurrentLevelInfo();

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static bool IsCurrentLevelDirty();

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationLevel")
	static ULevelScriptBlueprint* GetLevelScriptBlueprint();
};
