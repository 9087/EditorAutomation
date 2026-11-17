// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationAssetLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

#include "EditorAutomationLog.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

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
namespace
{
	/** Resolve an asset object path, a package name or a disk filename to a long package name. */
	FString PackageNameFromPath(const FString& Path)
	{
		const FString Trimmed = Path.TrimStartAndEnd();
		if (Trimmed.IsEmpty()) return FString();
		if (FPackageName::IsValidLongPackageName(Trimmed)) return Trimmed;
		const FString FromObject = FPackageName::ObjectPathToPackageName(Trimmed);
		if (!FromObject.IsEmpty() && FPackageName::IsValidLongPackageName(FromObject)) return FromObject;
		const FString FromFile = FPackageName::FilenameToLongPackageName(Trimmed);
		if (!FromFile.IsEmpty() && FPackageName::IsValidLongPackageName(FromFile)) return FromFile;
		return FString();
	}

	/** Find (or load) the package behind one path. */
	UPackage* ResolvePackage(const FString& Path)
	{
		const FString PackageName = PackageNameFromPath(Path);
		if (PackageName.IsEmpty()) return nullptr;
		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (Package == nullptr)
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		}
		return Package;
	}
}

TArray<FEditorAutomationDirtyAssetInfo> UEditorAutomationAssetLibrary::GetDirtyAssets()
{
	TArray<FEditorAutomationDirtyAssetInfo> Result;
	if (!IsInGameThread())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("GetDirtyAssets must be called on the game thread."));
		return Result;
	}

	TArray<UPackage*> Packages;
	FEditorFileUtils::GetDirtyPackages(Packages);
	IAssetRegistry* Registry = AssetRegistry();
	for (UPackage* Package : Packages)
	{
		if (Package == nullptr) continue;
		const FString PackageName = Package->GetName();
		// Engine-transient packages are never assets the user can confirm saving.
		if (PackageName.StartsWith(TEXT("/Temp/")) || PackageName.StartsWith(TEXT("/Engine/Transient"))) continue;

		FEditorAutomationDirtyAssetInfo Info;
		Info.PackageName = PackageName;
		if (Registry != nullptr)
		{
			TArray<FAssetData> Assets;
			Registry->GetAssetsByPackageName(Package->GetFName(), Assets);
			for (const FAssetData& Asset : Assets)
			{
				Info.AssetPaths.Add(Asset.GetObjectPathString());
			}
		}
		Info.bIsMap = UWorld::FindWorldInPackage(Package) != nullptr;
		Result.Add(MoveTemp(Info));
	}
	Result.Sort([](const FEditorAutomationDirtyAssetInfo& A, const FEditorAutomationDirtyAssetInfo& B)
	{
		return A.PackageName < B.PackageName;
	});
	return Result;
}

bool UEditorAutomationAssetLibrary::SaveAssets(const TArray<FString>& AssetOrPackagePaths, bool bOnlyDirty)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SaveAssets must be called on the game thread."));
		return false;
	}

	TArray<UPackage*> Packages;
	if (AssetOrPackagePaths.Num() == 0)
	{
		FEditorFileUtils::GetDirtyPackages(Packages);
	}
	else
	{
		for (const FString& Path : AssetOrPackagePaths)
		{
			UPackage* Package = ResolvePackage(Path);
			if (Package == nullptr)
			{
				UE_LOG(LogEditorAutomation, Error,
					TEXT("SaveAssets: '%s' did not resolve to a loaded package."), *Path);
				return false;
			}
			Packages.AddUnique(Package);
		}
	}

	if (Packages.Num() == 0)
	{
		UE_LOG(LogEditorAutomation, Log, TEXT("SaveAssets: no dirty packages to save."));
		return true;
	}

	// Save the packages directly: the editor-level save path runs a revision-control checkout,
	// and a failed checkout opens a modal dialog. Agents must never be interrupted by an
	// editor dialog, so this path writes each package itself and reports failures as logs.
	int32 SavedCount = 0;
	for (UPackage* Package : Packages)
	{
		if (Package == nullptr) continue;
		if (bOnlyDirty && !Package->IsDirty()) continue;
		const bool bIsMap = UWorld::FindWorldInPackage(Package) != nullptr;
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			bIsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bSlowTask = false;
		if (!UPackage::SavePackage(Package, nullptr, *FileName, SaveArgs))
		{
			UE_LOG(LogEditorAutomation, Error,
				TEXT("SaveAssets: failed to write '%s' to '%s' (is the file read-only?)."),
				*Package->GetName(), *FileName);
			return false;
		}
		++SavedCount;
	}
	UE_LOG(LogEditorAutomation, Log, TEXT("SaveAssets: saved %d package(s) without a dialog."), SavedCount);
	return true;
}

bool UEditorAutomationAssetLibrary::SaveAllDirtyAssets()
{
	return SaveAssets(TArray<FString>(), true);
}