// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "EditorAutomationCoreLibrary.h"

#include "EditorAutomationAssetLibrary.generated.h"

class UObject;

USTRUCT(BlueprintType)
struct FEditorAutomationAssetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString AssetPath;
	UPROPERTY(BlueprintReadOnly) FString PackagePath;
	UPROPERTY(BlueprintReadOnly) FString AssetName;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UClass> AssetClass = nullptr;
};

/** One loaded package with unsaved changes. */
USTRUCT(BlueprintType)
struct FEditorAutomationDirtyAssetInfo
{
	GENERATED_BODY()

	/** Long package name, e.g. /Game/UI/WBP_X. */
	UPROPERTY(BlueprintReadOnly) FString PackageName;

	/** Asset object paths inside that package (empty when the registry knows none). */
	UPROPERTY(BlueprintReadOnly) TArray<FString> AssetPaths;

	/** Whether the package holds a World (i.e. is a level). */
	UPROPERTY(BlueprintReadOnly) bool bIsMap = false;
};

UCLASS()
class EDITORAUTOMATION_API UEditorAutomationAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** List assets under a content directory, optionally recursive and filtered by class. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static FEditorAutomationResult ListAssets(
		const FString& DirectoryPath,
		bool bRecursive,
		const FString& ClassPath,
		TArray<FString>& OutAssets);

	/** List child folders under a content directory. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static FEditorAutomationResult ListFolders(
		const FString& DirectoryPath,
		bool bRecursive,
		TArray<FString>& OutFolders);

	/** Get one asset's registry info; AssetClass is null when the asset is not registered. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static FEditorAutomationResult GetAssetInfo(const FString& AssetPath, FEditorAutomationAssetInfo& OutInfo);

	/** Whether one asset is registered; the answer is data, so a missing asset is still a success. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static FEditorAutomationResult DoesAssetExist(const FString& AssetPath, bool& OutExists);

	/** Load one asset and hold a strong reference in the asset cache. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static FEditorAutomationResult LoadAsset(const FString& AssetPath, UObject*& OutAsset);

	/** Release the cache's strong reference; does not force a GC. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static FEditorAutomationResult UnloadAsset(const FString& AssetPath);

	/**
	 * Every loaded package with unsaved changes (maps and content; transient packages are skipped).
	 *
	 * Read-only: inspect this before closing the editor or restarting the host, then ask the
	 * user whether to save; SaveAssets performs the save without any dialog.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static FEditorAutomationResult GetDirtyAssets(TArray<FEditorAutomationDirtyAssetInfo>& OutDirtyAssets);

	/**
	 * Silently save the given assets/packages (no save dialog).
	 *
	 * Asset paths ("/Game/UI/WBP_X.WBP_X"), package names ("/Game/UI/WBP_X") and disk
	 * filenames are accepted. An empty list saves every currently dirty package.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static FEditorAutomationResult SaveAssets(const TArray<FString>& AssetOrPackagePaths, bool bOnlyDirty);

	/** Silently save every dirty map/content package (shortcut for SaveAssets with an empty list). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static FEditorAutomationResult SaveAllDirtyAssets();
};
