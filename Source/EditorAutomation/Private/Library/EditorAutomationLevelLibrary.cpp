// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationLevelLibrary.h"

#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "LevelEditorSubsystem.h"

namespace
{
	ULevelEditorSubsystem* LevelSubsystem()
	{
		return GEditor != nullptr ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	}

	UWorld* EditorWorld()
	{
		return GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	}
}

FEditorAutomationResult UEditorAutomationLevelLibrary::CreateLevel(
	const FString& LevelPath,
	const FString& TemplatePath,
	const bool bPartitioned)
{
	ULevelEditorSubsystem* Levels = LevelSubsystem();
	if (Levels == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("The level editor subsystem is not available."),
			TEXT("Run this call inside the Unreal editor process."));
	}
	if (LevelPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("LevelPath is required."),
			TEXT("Pass a package path, e.g. /Game/Maps/L_NewMap."));
	}

	FEditorAutomationLevelInfo LevelInfo;
	const FEditorAutomationResult InfoResult = GetCurrentLevelInfo(LevelInfo);
	if (InfoResult.bSuccess && LevelInfo.bDirty)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("The current Level has unsaved changes; save it before creating a new Level."),
			TEXT("Call SaveCurrentLevel (or save the dirty assets) first."));
	}

	const bool bCreated = TemplatePath.IsEmpty()
		? Levels->NewLevel(LevelPath, bPartitioned)
		: Levels->NewLevelFromTemplate(LevelPath, TemplatePath);
	if (!bCreated)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::EditorRefused,
			FString::Printf(TEXT("Could not create Level '%s'."), *LevelPath),
			TEXT("Check that the path is writable and that no asset already exists there."));
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationLevelLibrary::OpenLevel(const FString& LevelPath)
{
	ULevelEditorSubsystem* Levels = LevelSubsystem();
	if (Levels == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("The level editor subsystem is not available."),
			TEXT("Run this call inside the Unreal editor process."));
	}
	if (LevelPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("LevelPath is required."),
			TEXT("Pass a package path, e.g. /Game/Maps/L_MyMap."));
	}
	if (!Levels->LoadLevel(LevelPath))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Could not load Level '%s'."), *LevelPath),
			TEXT("Check the package path; the Level may be missing or the editor may have refused to switch."));
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationLevelLibrary::SaveCurrentLevel()
{
	ULevelEditorSubsystem* Levels = LevelSubsystem();
	if (Levels == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("The level editor subsystem is not available."),
			TEXT("Run this call inside the Unreal editor process."));
	}
	if (!Levels->SaveCurrentLevel())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::EditorRefused,
			TEXT("The editor refused to save the current Level."),
			TEXT("Check the output log for the engine reason (read-only file, source control, or a running PIE session)."));
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationLevelLibrary::GetCurrentLevelInfo(FEditorAutomationLevelInfo& OutInfo)
{
	UWorld* World = EditorWorld();
	if (World == nullptr || World->PersistentLevel == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("No editor world is available."),
			TEXT("Wait for the editor to finish loading and retry."));
	}
	OutInfo.LevelPath = World->PersistentLevel->GetOutermost() != nullptr
		? World->PersistentLevel->GetOutermost()->GetName()
		: FString();
	OutInfo.LevelName = World->PersistentLevel->GetName();
	OutInfo.bDirty = World->PersistentLevel->GetOutermost() != nullptr
		&& World->PersistentLevel->GetOutermost()->IsDirty();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationLevelLibrary::IsCurrentLevelDirty(bool& OutDirty)
{
	FEditorAutomationLevelInfo LevelInfo;
	const FEditorAutomationResult Result = GetCurrentLevelInfo(LevelInfo);
	if (!Result.bSuccess)
	{
		return Result;
	}
	OutDirty = LevelInfo.bDirty;
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationLevelLibrary::GetLevelScriptBlueprint(ULevelScriptBlueprint*& OutBlueprint)
{
	UWorld* World = EditorWorld();
	if (World == nullptr || World->PersistentLevel == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("No editor world is available."),
			TEXT("Wait for the editor to finish loading and retry."));
	}
	OutBlueprint = World->PersistentLevel->GetLevelScriptBlueprint(false);
	if (OutBlueprint == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			TEXT("The current Level has no Level Script Blueprint."),
			TEXT("Open the Level Blueprint in the editor once, or create it via the editor UI."));
	}
	return FEditorAutomationResult::Success();
}
