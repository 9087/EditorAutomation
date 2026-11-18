// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationAssetLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

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

FEditorAutomationResult UEditorAutomationAssetLibrary::ListAssets(
	const FString& DirectoryPath,
	const bool bRecursive,
	const FString& ClassPath,
	TArray<FString>& OutAssets)
{
	if (DirectoryPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("DirectoryPath is empty."),
			TEXT("Pass a content directory, e.g. /Game/UI."));
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*DirectoryPath));
	Filter.bRecursivePaths = bRecursive;
	Filter.bRecursiveClasses = true;
	if (!ClassPath.IsEmpty())
	{
		UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
		if (Class == nullptr)
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::NotFound,
				FString::Printf(TEXT("Class '%s' could not be loaded."), *ClassPath),
				TEXT("Pass a class object path, e.g. /Script/UMG.Widget or /Game/BP.BP_C."));
		}
		Filter.ClassPaths.Add(Class->GetClassPathName());
	}
	TArray<FAssetData> Found;
	AssetRegistry()->GetAssets(Filter, Found);
	for (const FAssetData& Asset : Found) OutAssets.Add(Asset.GetObjectPathString());
	OutAssets.Sort();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationAssetLibrary::ListFolders(
	const FString& DirectoryPath,
	const bool bRecursive,
	TArray<FString>& OutFolders)
{
	if (DirectoryPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("DirectoryPath is empty."),
			TEXT("Pass a content directory, e.g. /Game."));
	}
	TArray<FString> Found;
	AssetRegistry()->GetSubPaths(DirectoryPath, Found, bRecursive);
	for (const FString& Folder : Found) OutFolders.Add(Folder);
	OutFolders.Sort();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationAssetLibrary::GetAssetInfo(
	const FString& AssetPath,
	FEditorAutomationAssetInfo& OutInfo)
{
	if (AssetPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("AssetPath is empty."),
			TEXT("Pass an asset object path, e.g. /Game/UI/WBP_X.WBP_X."));
	}
	const FAssetData Data = AssetRegistry()->GetAssetByObjectPath(FSoftObjectPath(AssetPath));
	if (!Data.IsValid())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Asset '%s' is not registered."), *AssetPath),
			TEXT("Pass a full asset object path (/Game/Dir/Asset.Asset); use ListAssets to find it."));
	}
	OutInfo.AssetPath = Data.GetObjectPathString();
	OutInfo.PackagePath = Data.PackagePath.ToString();
	OutInfo.AssetName = Data.AssetName.ToString();
	OutInfo.AssetClass = Data.GetClass();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationAssetLibrary::DoesAssetExist(const FString& AssetPath, bool& OutExists)
{
	if (AssetPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("AssetPath is empty."),
			TEXT("Pass an asset object path, e.g. /Game/UI/WBP_X.WBP_X."));
	}
	OutExists = AssetRegistry()->GetAssetByObjectPath(FSoftObjectPath(AssetPath)).IsValid();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationAssetLibrary::LoadAsset(const FString& AssetPath, UObject*& OutAsset)
{
	if (AssetPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("AssetPath is empty."),
			TEXT("Pass an asset object path, e.g. /Game/UI/WBP_X.WBP_X."));
	}
	if (const TStrongObjectPtr<UObject>* Existing = GLoadedAssets.Find(AssetPath))
	{
		OutAsset = Existing->Get();
		return FEditorAutomationResult::Success();
	}
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (Asset == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Asset '%s' could not be loaded."), *AssetPath),
			TEXT("Pass a full asset object path (/Game/Dir/Asset.Asset); use ListAssets to find it."));
	}
	GLoadedAssets.Add(AssetPath, TStrongObjectPtr<UObject>(Asset));
	OutAsset = Asset;
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationAssetLibrary::UnloadAsset(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("AssetPath is empty."),
			TEXT("Pass the same path that was given to LoadAsset."));
	}
	if (GLoadedAssets.Remove(AssetPath) == 0)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Asset '%s' is not held by the asset cache."), *AssetPath),
			TEXT("Pass the exact path string given to LoadAsset; this only releases cached strong references."));
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationAssetLibrary::GetDirtyAssets(TArray<FEditorAutomationDirtyAssetInfo>& OutDirtyAssets)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("GetDirtyAssets must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
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
		OutDirtyAssets.Add(MoveTemp(Info));
	}
	OutDirtyAssets.Sort([](const FEditorAutomationDirtyAssetInfo& A, const FEditorAutomationDirtyAssetInfo& B)
	{
		return A.PackageName < B.PackageName;
	});
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationAssetLibrary::SaveAssets(
	const TArray<FString>& AssetOrPackagePaths,
	const bool bOnlyDirty)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("SaveAssets must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
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
				return FEditorAutomationResult::Failure(
					EEditorAutomationResultCode::NotFound,
					FString::Printf(TEXT("'%s' did not resolve to a loaded package."), *Path),
					TEXT("Pass an asset object path, a package name or a disk filename of a loaded asset."));
			}
			Packages.AddUnique(Package);
		}
	}

	// Save the packages directly: the editor-level save path runs a revision-control checkout,
	// and a failed checkout opens a modal dialog. Agents must never be interrupted by an
	// editor dialog, so this path writes each package itself and reports failures in the envelope.
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
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::EditorRefused,
				FString::Printf(TEXT("Failed to write '%s' to '%s'."), *Package->GetName(), *FileName),
				TEXT("Check that the file is writable; a read-only file (for example under revision control) cannot be saved silently."));
		}
		++SavedCount;
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationAssetLibrary::SaveAllDirtyAssets()
{
	return SaveAssets(TArray<FString>(), true);
}
