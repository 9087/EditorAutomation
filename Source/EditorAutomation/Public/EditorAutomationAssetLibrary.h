// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EditorAutomationAssetLibrary.generated.h"

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
	static TArray<FString> ListAssets(
		const FString& DirectoryPath,
		bool bRecursive = false,
		const FString& ClassPath = TEXT(""));

	/** List child folders under a content directory. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static TArray<FString> ListFolders(const FString& DirectoryPath, bool bRecursive = false);

	/** Get one asset's registry info; AssetClass is null when the asset is not registered. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static FEditorAutomationAssetInfo GetAssetInfo(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static bool DoesAssetExist(const FString& AssetPath);

	/** Load one asset and hold a strong reference in the asset cache. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static UObject* LoadAsset(const FString& AssetPath);

	/** Release the cache's strong reference; does not force a GC. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static bool UnloadAsset(const FString& AssetPath);

	/**
	 * Every loaded package with unsaved changes (maps and content; transient packages are skipped).
	 *
	 * Read-only: inspect this before closing the editor or restarting the host, then ask the
	 * user whether to save; SaveAssets performs the save without any dialog.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static TArray<FEditorAutomationDirtyAssetInfo> GetDirtyAssets();

	/**
	 * Silently save the given assets/packages (no save dialog).
	 *
	 * Asset paths ("/Game/UI/WBP_X.WBP_X"), package names ("/Game/UI/WBP_X") and disk
	 * filenames are accepted. An empty list saves every currently dirty package.
	 *
	 * @returns true when every package was saved; failures are logged.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static bool SaveAssets(const TArray<FString>& AssetOrPackagePaths, bool bOnlyDirty = true);

	/** Silently save every dirty map/content package (shortcut for SaveAssets with an empty list). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationAsset")
	static bool SaveAllDirtyAssets();
};
