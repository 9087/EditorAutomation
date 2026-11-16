// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationLevelLibrary.h"

#include "Editor.h"
#include "EditorAutomationLog.h"
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

bool UEditorAutomationLevelLibrary::CreateLevel(
	const FString& LevelPath,
	const FString& TemplatePath,
	const bool bPartitioned)
{
	ULevelEditorSubsystem* Levels = LevelSubsystem();
	if (Levels == nullptr || LevelPath.IsEmpty())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CreateLevel: LevelPath is required."));
		return false;
	}
	if (IsCurrentLevelDirty())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CreateLevel: the current Level is dirty; save it before creating a new Level."));
		return false;
	}
	const bool bCreated = TemplatePath.IsEmpty()
		? Levels->NewLevel(LevelPath, bPartitioned)
		: Levels->NewLevelFromTemplate(LevelPath, TemplatePath);
	if (!bCreated)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CreateLevel: could not create Level '%s'."), *LevelPath);
		return false;
	}
	return true;
}

bool UEditorAutomationLevelLibrary::OpenLevel(const FString& LevelPath)
{
	ULevelEditorSubsystem* Levels = LevelSubsystem();
	if (Levels == nullptr || LevelPath.IsEmpty())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("OpenLevel: LevelPath is required."));
		return false;
	}
	if (!Levels->LoadLevel(LevelPath))
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("OpenLevel: could not load Level '%s'."), *LevelPath);
		return false;
	}
	return true;
}

bool UEditorAutomationLevelLibrary::SaveCurrentLevel()
{
	ULevelEditorSubsystem* Levels = LevelSubsystem();
	if (Levels == nullptr || !Levels->SaveCurrentLevel())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SaveCurrentLevel: the editor refused to save the current Level."));
		return false;
	}
	return true;
}

FEditorAutomationLevelInfo UEditorAutomationLevelLibrary::GetCurrentLevelInfo()
{
	FEditorAutomationLevelInfo Info;
	UWorld* World = EditorWorld();
	if (World == nullptr || World->PersistentLevel == nullptr)
	{
		return Info;
	}
	Info.LevelPath = World->PersistentLevel->GetOutermost() != nullptr
		? World->PersistentLevel->GetOutermost()->GetName()
		: FString();
	Info.LevelName = World->PersistentLevel->GetName();
	Info.bDirty = World->PersistentLevel->GetOutermost() != nullptr
		&& World->PersistentLevel->GetOutermost()->IsDirty();
	return Info;
}

bool UEditorAutomationLevelLibrary::IsCurrentLevelDirty()
{
	return GetCurrentLevelInfo().bDirty;
}

ULevelScriptBlueprint* UEditorAutomationLevelLibrary::GetLevelScriptBlueprint()
{
	UWorld* World = EditorWorld();
	return World != nullptr && World->PersistentLevel != nullptr
		? World->PersistentLevel->GetLevelScriptBlueprint(false)
		: nullptr;
}
