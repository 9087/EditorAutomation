// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationAssetLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	TMap<FString, TStrongObjectPtr<UObject>> GLoadedAssets;

	IAssetRegistry* AssetRegistry()
	{
		return &FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	}
}

TArray<FString> UEditorAutomationAssetLibrary::ListAssets(
	const FString& DirectoryPath,
	bool bRecursive,
	const FString& ClassPath)
{
	TArray<FString> Assets;
	if (DirectoryPath.IsEmpty()) return Assets;
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*DirectoryPath));
	Filter.bRecursivePaths = bRecursive;
	Filter.bRecursiveClasses = true;
	if (!ClassPath.IsEmpty())
	{
		if (UClass* Class = LoadObject<UClass>(nullptr, *ClassPath))
		{
			Filter.ClassPaths.Add(Class->GetClassPathName());
		}
		else
		{
			return Assets;
		}
	}
	TArray<FAssetData> Found;
	AssetRegistry()->GetAssets(Filter, Found);
	for (const FAssetData& Asset : Found) Assets.Add(Asset.GetObjectPathString());
	Assets.Sort();
	return Assets;
}

TArray<FString> UEditorAutomationAssetLibrary::ListFolders(const FString& DirectoryPath, bool bRecursive)
{
	TArray<FString> Folders;
	if (DirectoryPath.IsEmpty()) return Folders;
	TArray<FString> Found;
	AssetRegistry()->GetSubPaths(DirectoryPath, Found, bRecursive);
	for (const FString& Folder : Found) Folders.Add(Folder);
	Folders.Sort();
	return Folders;
}

FEditorAutomationAssetInfo UEditorAutomationAssetLibrary::GetAssetInfo(const FString& AssetPath)
{
	FEditorAutomationAssetInfo Info;
	if (AssetPath.IsEmpty()) return Info;
	const FAssetData Data = AssetRegistry()->GetAssetByObjectPath(FSoftObjectPath(AssetPath));
	if (!Data.IsValid()) return Info;
	Info.AssetPath = Data.GetObjectPathString();
	Info.PackagePath = Data.PackagePath.ToString();
	Info.AssetName = Data.AssetName.ToString();
	Info.AssetClass = Data.GetClass();
	return Info;
}

bool UEditorAutomationAssetLibrary::DoesAssetExist(const FString& AssetPath)
{
	if (AssetPath.IsEmpty()) return false;
	return AssetRegistry()->GetAssetByObjectPath(FSoftObjectPath(AssetPath)).IsValid();
}

UObject* UEditorAutomationAssetLibrary::LoadAsset(const FString& AssetPath)
{
	if (AssetPath.IsEmpty()) return nullptr;
	if (const TStrongObjectPtr<UObject>* Existing = GLoadedAssets.Find(AssetPath))
	{
		return Existing->Get();
	}
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (Asset == nullptr) return nullptr;
	GLoadedAssets.Add(AssetPath, TStrongObjectPtr<UObject>(Asset));
	return Asset;
}

bool UEditorAutomationAssetLibrary::UnloadAsset(const FString& AssetPath)
{
	return GLoadedAssets.Remove(AssetPath) > 0;
}