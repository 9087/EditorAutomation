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
};
