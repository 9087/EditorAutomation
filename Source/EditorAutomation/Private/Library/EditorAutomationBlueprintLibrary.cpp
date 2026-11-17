// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationBlueprintLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EditorAutomationLog.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace
{
	UBlueprint* ResolveBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint)
	{
		return Blueprint.IsNull() ? nullptr : Blueprint.LoadSynchronous();
	}

	UEditorAssetSubsystem* EditorAssetSubsystem()
	{
		return GEditor != nullptr ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	}
}

FEditorAutomationBlueprintInfo UEditorAutomationBlueprintLibrary::GetBlueprintInfo(const TSoftObjectPtr<UBlueprint>& Blueprint)
{
	FEditorAutomationBlueprintInfo Info;
	UBlueprint* Loaded = ResolveBlueprint(Blueprint);
	if (Loaded == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("GetBlueprintInfo: Blueprint could not be loaded."));
		return Info;
	}
	Info.AssetPath = Loaded->GetPathName();
	Info.AssetClass = Loaded->GetClass();
	Info.ParentClass = Loaded->ParentClass;
	Info.GeneratedClass = Loaded->GeneratedClass;
	Info.BlueprintType = static_cast<int32>(Loaded->BlueprintType);
	Info.Status = static_cast<int32>(Loaded->Status);
	Info.bDirty = Loaded->GetOutermost() != nullptr && Loaded->GetOutermost()->IsDirty();
	Info.bBeingCompiled = Loaded->bBeingCompiled;
	return Info;
}

bool UEditorAutomationBlueprintLibrary::SetBlueprintDirty(const TSoftObjectPtr<UBlueprint>& Blueprint, const bool bDirty)
{
	UBlueprint* Loaded = ResolveBlueprint(Blueprint);
	if (Loaded == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SetBlueprintDirty: Blueprint could not be loaded."));
		return false;
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
	return true;
}

bool UEditorAutomationBlueprintLibrary::CompileBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint)
{
	UBlueprint* Loaded = ResolveBlueprint(Blueprint);
	if (Loaded == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CompileBlueprint: Blueprint could not be loaded."));
		return false;
	}
	FKismetEditorUtilities::CompileBlueprint(Loaded);
	if (Loaded->Status == EBlueprintStatus::BS_Error)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CompileBlueprint: '%s' compiled with errors."), *Loaded->GetPathName());
		return false;
	}
	return true;
}

bool UEditorAutomationBlueprintLibrary::SaveBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint)
{
	UBlueprint* Loaded = ResolveBlueprint(Blueprint);
	if (Loaded == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SaveBlueprint: Blueprint could not be loaded."));
		return false;
	}
	UEditorAssetSubsystem* Assets = EditorAssetSubsystem();
	if (Assets == nullptr || !Assets->SaveLoadedAsset(Loaded, false))
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("SaveBlueprint: '%s' could not be saved. If PIE is running, stop it first; otherwise check source control and file permissions."),
			*Loaded->GetPathName());
		return false;
	}
	return true;
}

bool UEditorAutomationBlueprintLibrary::CreateBlueprint(
	const FString& AssetPath,
	const TSoftClassPtr<UObject>& ParentClass,
	const int32 BlueprintType)
{
	if (AssetPath.IsEmpty() || ParentClass.IsNull())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CreateBlueprint: AssetPath and ParentClass are required."));
		return false;
	}
	if (LoadObject<UObject>(nullptr, *AssetPath) != nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CreateBlueprint: asset '%s' already exists."), *AssetPath);
		return false;
	}
	UClass* LoadedParent = Cast<UClass>(ParentClass.LoadSynchronous());
	if (LoadedParent == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CreateBlueprint: parent class could not be loaded."));
		return false;
	}
	const FString AssetName = FPaths::GetBaseFilename(AssetPath);
	UPackage* Package = CreatePackage(*AssetPath);
	if (Package == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CreateBlueprint: could not create the target package '%s'."), *AssetPath);
		return false;
	}
	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
		LoadedParent,
		Package,
		FName(*AssetName),
		static_cast<EBlueprintType>(BlueprintType));
	if (NewBlueprint == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CreateBlueprint: FKismetEditorUtilities::CreateBlueprint failed for '%s'."), *AssetPath);
		return false;
	}
	FAssetRegistryModule::AssetCreated(NewBlueprint);
	return true;
}

bool UEditorAutomationBlueprintLibrary::DeleteBlueprint(const TSoftObjectPtr<UBlueprint>& Blueprint)
{
	UBlueprint* Loaded = ResolveBlueprint(Blueprint);
	if (Loaded == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("DeleteBlueprint: Blueprint could not be loaded."));
		return false;
	}
	UEditorAssetSubsystem* Assets = EditorAssetSubsystem();
	if (Assets == nullptr || !Assets->DeleteAsset(Loaded->GetPathName()))
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("DeleteBlueprint: '%s' could not be deleted."), *Loaded->GetPathName());
		return false;
	}
	return true;
}

bool UEditorAutomationBlueprintLibrary::CreateBlueprintFromParentPath(
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
		UE_LOG(LogEditorAutomation, Error, TEXT("CreateBlueprintFromParentPath: parent class '%s' could not be loaded."), *ParentClassPath);
		return false;
	}
	return CreateBlueprint(AssetPath, TSoftClassPtr<UObject>(ParentClass), BlueprintType);
}

FString UEditorAutomationBlueprintLibrary::GetBlueprintLuaPath(UBlueprint* Blueprint)
{
	if (Blueprint == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("GetBlueprintLuaPath: Blueprint must not be null."));
		return FString();
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
	FString LuaPath = FindLuaPathOn(Blueprint);
	if (LuaPath.IsEmpty() && Blueprint->GeneratedClass != nullptr)
	{
		LuaPath = FindLuaPathOn(Blueprint->GeneratedClass->GetDefaultObject(false));
	}
	return LuaPath;
}