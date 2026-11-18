// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationBlueprintLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
	FEditorAutomationResult ResolutionFailure(
		const EEditorAutomationResultCode Code,
		const FString& Message,
		const FString& Hint = FString())
	{
		return FEditorAutomationResult::Failure(Code, Message, Hint);
	}

	FEditorAutomationResult ResolutionSuccess(
		UBlueprint* Blueprint,
		UBlueprint*& OutBlueprint,
		const FString& Hint = FString())
	{
		OutBlueprint = Blueprint;
		FEditorAutomationResult Result = FEditorAutomationResult::Success();
		Result.Hint = Hint;
		return Result;
	}

	/** Resolve the soft pointer through its path text so every accepted form uses one resolver. */
	FEditorAutomationResult ResolveBlueprintForOperation(
		const TSoftObjectPtr<UBlueprint>& Blueprint,
		UBlueprint*& OutBlueprint)
	{
		// TSoftObjectPtr::ToString() is empty for a null pointer; no null check needed.
		return UEditorAutomationBlueprintLibrary::ResolveBlueprintPath(Blueprint.ToString(), OutBlueprint);
	}

	UEditorAssetSubsystem* EditorAssetSubsystem()
	{
		return GEditor != nullptr ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	}
}

FEditorAutomationResult UEditorAutomationBlueprintLibrary::ResolveBlueprintPath(const FString& BlueprintPath, UBlueprint*& OutBlueprint)
{
	const FString Trimmed = BlueprintPath.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return ResolutionFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Blueprint path is empty."),
			TEXT("Pass an asset object path (/Game/Dir/BP.BP), an asset path (/Game/Dir/BP) or a generated-class path (/Game/Dir/BP.BP_C)."));
	}

	// 1) A full object path, or a generated-class path whose ClassGeneratedBy is the Blueprint.
	if (Trimmed.Contains(TEXT(".")))
	{
		if (UBlueprint* Loaded = LoadObject<UBlueprint>(nullptr, *Trimmed))
		{
			return ResolutionSuccess(Loaded, OutBlueprint);
		}
		if (Trimmed.EndsWith(TEXT("_C")))
		{
			if (UClass* GeneratedClass = LoadClass<UClass>(nullptr, *Trimmed))
			{
				if (UBlueprint* Owner = Cast<UBlueprint>(GeneratedClass->ClassGeneratedBy))
				{
					return ResolutionSuccess(
						Owner,
						OutBlueprint,
						FString::Printf(
							TEXT("'%s' names a generated class, not an asset; resolved it to Blueprint '%s'. Pass the asset object path '%s' instead."),
							*Trimmed, *Owner->GetPathName(), *Owner->GetPathName()));
				}
			}
			if (UBlueprint* Loaded = LoadObject<UBlueprint>(nullptr, *Trimmed.LeftChop(2)))
			{
				return ResolutionSuccess(
					Loaded,
					OutBlueprint,
					FString::Printf(
						TEXT("'%s' names a generated class, not an asset; resolved it to Blueprint '%s'. Pass the asset object path '%s' instead."),
						*Trimmed, *Loaded->GetPathName(), *Loaded->GetPathName()));
			}
		}
		return ResolutionFailure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Blueprint '%s' could not be loaded."), *Trimmed),
			TEXT("Pass an asset object path (/Game/Dir/BP.BP), a package path (/Game/Dir/BP), a generated-class path (/Game/Dir/BP.BP_C) or a disk filename; check the path with ListAssets / GetAssetInfo."));
	}

	// 2) An asset/package path or disk filename. Remote Control binds the package-only form of
	//    a soft object path when the caller passes an asset path, so widen it here.
	bool bFromFilename = false;
	FString PackageName = Trimmed;
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		const FString FromFile = FPackageName::FilenameToLongPackageName(Trimmed);
		if (FPackageName::IsValidLongPackageName(FromFile))
		{
			PackageName = FromFile;
			bFromFilename = true;
		}
		else
		{
			return ResolutionFailure(
				EEditorAutomationResultCode::InvalidArgument,
				FString::Printf(TEXT("'%s' is not a Blueprint path."), *Trimmed),
				TEXT("Pass an asset object path (/Game/Dir/BP.BP), a package path (/Game/Dir/BP), a generated-class path or a disk filename."));
		}
	}

	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssetsByPackageName(FName(*PackageName), Assets);

	TArray<FAssetData> BlueprintAssets;
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.GetClass() != nullptr && Asset.GetClass()->IsChildOf(UBlueprint::StaticClass()))
		{
			BlueprintAssets.Add(Asset);
		}
	}
	if (BlueprintAssets.Num() > 1)
	{
		FString Candidates;
		for (const FAssetData& Asset : BlueprintAssets)
		{
			Candidates += FString::Printf(TEXT("\n  %s"), *Asset.GetObjectPathString());
		}
		return ResolutionFailure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(
				TEXT("Package '%s' contains %d Blueprint assets, so it is ambiguous. Pass one of these object paths:%s"),
				*PackageName, BlueprintAssets.Num(), *Candidates),
			TEXT("Pass a single asset object path instead of the package path."));
	}
	if (BlueprintAssets.Num() == 1)
	{
		UBlueprint* Loaded = Cast<UBlueprint>(BlueprintAssets[0].GetAsset());
		if (Loaded == nullptr)
		{
			return ResolutionFailure(
				EEditorAutomationResultCode::NotFound,
				FString::Printf(TEXT("Blueprint '%s' could not be loaded from its package."), *BlueprintAssets[0].GetObjectPathString()),
				TEXT("Check the package for load errors in the output log."));
		}
		return ResolutionSuccess(
			Loaded,
			OutBlueprint,
			bFromFilename
				? FString::Printf(
					TEXT("'%s' is a disk filename, not a Blueprint path; resolved it to '%s' (package '%s'). Pass that object path instead."),
					*Trimmed, *Loaded->GetPathName(), *PackageName)
				: FString::Printf(
					TEXT("'%s' is a package path without the object name; resolved it to '%s'. Pass the asset object path '%s' instead."),
					*Trimmed, *Loaded->GetPathName(), *Loaded->GetPathName()));
	}

	// Conventional fallback for a package the registry does not list yet (just created).
	const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetShortName(PackageName);
	if (UBlueprint* Loaded = LoadObject<UBlueprint>(nullptr, *ObjectPath))
	{
		return ResolutionSuccess(
			Loaded,
			OutBlueprint,
			FString::Printf(
				TEXT("'%s' is a package path; the asset registry listed no Blueprint in it, so it was resolved by convention to '%s'. Pass that object path instead."),
				*Trimmed, *Loaded->GetPathName()));
	}

	return ResolutionFailure(
		EEditorAutomationResultCode::NotFound,
		FString::Printf(TEXT("No Blueprint asset was found in package '%s'."), *PackageName),
		TEXT("Check the path with ListAssets / GetAssetInfo; a package path is widened only when it contains exactly one Blueprint asset."));
}

FEditorAutomationResult UEditorAutomationBlueprintLibrary::GetBlueprintInfo(
	const TSoftObjectPtr<UBlueprint>& Blueprint,
	FEditorAutomationBlueprintInfo& OutInfo)
{
	UBlueprint* Loaded = nullptr;
	const FEditorAutomationResult Resolution = ResolveBlueprintForOperation(Blueprint, Loaded);
	if (!Resolution.bSuccess)
	{
		return Resolution;
	}
	OutInfo.AssetPath = Loaded->GetPathName();
	OutInfo.AssetClass = Loaded->GetClass();
	OutInfo.ParentClass = Loaded->ParentClass;
	OutInfo.GeneratedClass = Loaded->GeneratedClass;
	OutInfo.BlueprintType = static_cast<int32>(Loaded->BlueprintType);
	OutInfo.Status = static_cast<int32>(Loaded->Status);
	OutInfo.bDirty = Loaded->GetOutermost() != nullptr && Loaded->GetOutermost()->IsDirty();
	OutInfo.bBeingCompiled = Loaded->bBeingCompiled;
	return Resolution;
}

FEditorAutomationResult UEditorAutomationBlueprintLibrary::SetBlueprintDirty(
	const TSoftObjectPtr<UBlueprint>& Blueprint,
	const bool bDirty)
{
	UBlueprint* Loaded = nullptr;
	const FEditorAutomationResult Resolution = ResolveBlueprintForOperation(Blueprint, Loaded);
	if (!Resolution.bSuccess)
	{
		return Resolution;
	}
	if (Loaded->GetOutermost() != nullptr)
	{
		Loaded->GetOutermost()->SetDirtyFlag(bDirty);
	}
	if (bDirty)
	{
		Loaded->Modify();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Loaded);
	}
	return Resolution;
}

FEditorAutomationResult UEditorAutomationBlueprintLibrary::CompileBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint)
{
	UBlueprint* Loaded = nullptr;
	const FEditorAutomationResult Resolution = ResolveBlueprintForOperation(Blueprint, Loaded);
	if (!Resolution.bSuccess)
	{
		return Resolution;
	}
	FKismetEditorUtilities::CompileBlueprint(Loaded);
	if (Loaded->Status == EBlueprintStatus::BS_Error)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::EditorRefused,
			FString::Printf(TEXT("Blueprint '%s' compiled with errors."), *Loaded->GetPathName()),
			TEXT("Call GetRecentLog to read the compiler diagnostics, fix the graph, then compile again."));
	}
	return Resolution;
}

FEditorAutomationResult UEditorAutomationBlueprintLibrary::SaveBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint)
{
	UBlueprint* Loaded = nullptr;
	const FEditorAutomationResult Resolution = ResolveBlueprintForOperation(Blueprint, Loaded);
	if (!Resolution.bSuccess)
	{
		return Resolution;
	}
	UPackage* Package = Loaded->GetOutermost();
	if (Package == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InternalError,
			FString::Printf(TEXT("Blueprint '%s' has no package."), *Loaded->GetPathName()),
			TEXT("This is an EditorAutomation bug; report the asset."));
	}

	// Save the package directly: the editor-level save path can open a revision-control dialog,
	// which would block an agent. This path writes the file and reports failures in the envelope.
	const FString FileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	SaveArgs.bSlowTask = false;
	if (!UPackage::SavePackage(Package, Loaded, *FileName, SaveArgs))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::EditorRefused,
			FString::Printf(TEXT("Blueprint '%s' could not be written to '%s'."), *Loaded->GetPathName(), *FileName),
			TEXT("Check that the file is writable; a read-only file (for example under revision control) cannot be saved silently."));
	}
	return Resolution;
}

FEditorAutomationResult UEditorAutomationBlueprintLibrary::CreateBlueprint(
	const FString& AssetPath,
	const TSoftClassPtr<UObject>& ParentClass,
	const int32 BlueprintType)
{
	if (AssetPath.IsEmpty() || ParentClass.IsNull())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("AssetPath and ParentClass are required."),
			TEXT("Pass the new package path (/Game/Dir/BP) and a parent class object path (/Script/Engine.Actor)."));
	}
	if (LoadObject<UObject>(nullptr, *AssetPath) != nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("Asset '%s' already exists."), *AssetPath),
			TEXT("Delete the existing asset first, or choose another AssetPath."));
	}
	UClass* LoadedParent = Cast<UClass>(ParentClass.LoadSynchronous());
	if (LoadedParent == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			TEXT("ParentClass could not be loaded."),
			TEXT("Pass a class object path such as /Script/Engine.Actor; for Blueprint classes use /Game/Dir/BP.BP_C."));
	}
	const FString AssetName = FPaths::GetBaseFilename(AssetPath);
	UPackage* Package = CreatePackage(*AssetPath);
	if (Package == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::EditorRefused,
			FString::Printf(TEXT("Could not create the target package '%s'."), *AssetPath),
			TEXT("Check that the path is a valid content path the editor can write."));
	}
	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
		LoadedParent,
		Package,
		FName(*AssetName),
		static_cast<EBlueprintType>(BlueprintType));
	if (NewBlueprint == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::EditorRefused,
			FString::Printf(TEXT("FKismetEditorUtilities::CreateBlueprint failed for '%s'."), *AssetPath),
			TEXT("Check that no asset already exists at that path and that the parent class allows Blueprint children."));
	}
	FAssetRegistryModule::AssetCreated(NewBlueprint);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationBlueprintLibrary::DeleteBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint)
{
	UBlueprint* Loaded = nullptr;
	const FEditorAutomationResult Resolution = ResolveBlueprintForOperation(Blueprint, Loaded);
	if (!Resolution.bSuccess)
	{
		return Resolution;
	}
	UEditorAssetSubsystem* Assets = EditorAssetSubsystem();
	if (Assets == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("The editor asset subsystem is not available."),
			TEXT("Run this call inside the Unreal editor process."));
	}
	if (!Assets->DeleteAsset(Loaded->GetPathName()))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::EditorRefused,
			FString::Printf(TEXT("Blueprint '%s' could not be deleted."), *Loaded->GetPathName()),
			TEXT("Check GetRecentLog; the asset may be referenced, read-only or checked out."));
	}
	return Resolution;
}

FEditorAutomationResult UEditorAutomationBlueprintLibrary::CreateBlueprintFromParentPath(
	const FString& AssetPath,
	const FString& ParentClassPath,
	const int32 BlueprintType)
{
	UClass* ParentClass = nullptr;
	if (!ParentClassPath.IsEmpty())
	{
		ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
		if (ParentClass == nullptr)
		{
			ParentClass = LoadClass<UClass>(nullptr, *ParentClassPath);
		}
	}
	if (ParentClass == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Parent class '%s' could not be loaded."), *ParentClassPath),
			TEXT("Pass a class object path such as /Script/Engine.Actor; for Blueprint classes use /Game/Dir/BP.BP_C."));
	}
	return CreateBlueprint(AssetPath, TSoftClassPtr<UObject>(ParentClass), BlueprintType);
}

FEditorAutomationResult UEditorAutomationBlueprintLibrary::GetBlueprintLuaPath(
	const TSoftObjectPtr<UBlueprint>& Blueprint,
	FString& OutLuaPath)
{
	UBlueprint* Loaded = nullptr;
	const FEditorAutomationResult Resolution = ResolveBlueprintForOperation(Blueprint, Loaded);
	if (!Resolution.bSuccess)
	{
		return Resolution;
	}

	// KGUI-style tools store the binding as a reflected property on the Blueprint or its
	// generated class; look in both so the caller does not need to know which.
	auto FindLuaPathOn = [](UObject* Container) -> FString
	{
		if (Container == nullptr) return FString();
		for (TFieldIterator<FProperty> It(Container->GetClass()); It; ++It)
		{
			if (!It->GetName().Contains(TEXT("LuaPath"))) continue;
			FString Value;
			It->ExportTextItem_InContainer(Value, Container, nullptr, Container, PPF_None);
			if (!Value.IsEmpty()) return Value;
		}
		return FString();
	};
	OutLuaPath = FindLuaPathOn(Loaded);
	if (OutLuaPath.IsEmpty() && Loaded->GeneratedClass != nullptr)
	{
		OutLuaPath = FindLuaPathOn(Loaded->GeneratedClass->GetDefaultObject(false));
	}
	return Resolution;
}
