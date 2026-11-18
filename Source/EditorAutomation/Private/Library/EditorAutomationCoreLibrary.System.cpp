// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationCoreLibrary.h"

#include "CoreGlobals.h"
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

// ============================================================================
// System
// ============================================================================

FEditorAutomationResult UEditorAutomationCoreLibrary::Ping(FString& OutValue)
{
	OutValue = TEXT("pong");
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::GetEditorInfo(FEditorAutomationEditorInfo& OutInfo)
{
	OutInfo.ProcessId = static_cast<int64>(FPlatformProcess::GetCurrentProcessId());
	OutInfo.EngineVersion = FEngineVersion::Current().ToString();
	OutInfo.ProjectName = FApp::GetProjectName();
	OutInfo.ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	OutInfo.bIsEditor = GIsEditor;
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::StartPIE(const FString& LevelPath, const bool bNewWindow)
{
	if (!IsInGameThread() || GEditor == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("StartPIE must be called on the game thread."),
			TEXT("Retry from the editor's game thread; Remote Control calls normally already run there."));
	}
	if (GEditor->IsPlaySessionInProgress())
	{
		return FEditorAutomationResult::Success();
	}
	if (!LevelPath.IsEmpty() && !FPackageName::DoesPackageExist(LevelPath))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Level '%s' does not exist."), *LevelPath),
			TEXT("Pass a package path that exists, e.g. /Game/Maps/L_MyMap, or an empty path to use the current level."));
	}
	FRequestPlaySessionParams Params;
	if (!LevelPath.IsEmpty()) Params.GlobalMapOverride = LevelPath;
	ULevelEditorPlaySettings* Settings = NewObject<ULevelEditorPlaySettings>();
	Settings->LastExecutedPlayModeType = bNewWindow ? PlayMode_InEditorFloating : PlayMode_InViewPort;
	Params.EditorPlaySettings = Settings;
	if (!bNewWindow)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		Params.DestinationSlateViewport = LevelEditorModule.GetFirstActiveViewport();
	}
	GEditor->RequestPlaySession(Params);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::CaptureScreenshot(
	const FString& FilePath,
	const EEditorAutomationScreenshotTarget Target,
	const bool bShowUI,
	FString& OutFilePath)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("CaptureScreenshot must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}

	FString ResolvedPath = FilePath;
	if (ResolvedPath.IsEmpty())
	{
		ResolvedPath = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Screenshots"),
			FString::Printf(TEXT("EditorAutomation_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	}
	ResolvedPath = FPaths::ConvertRelativePathToFull(ResolvedPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ResolvedPath), /*Tree=*/true);

	if (Target == EEditorAutomationScreenshotTarget::EditorWindow)
	{
		const TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (!Window.IsValid())
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::NotReady,
				TEXT("No active editor window to capture."),
				TEXT("Focus the editor window first, or capture the game viewport with Target=GameViewport."));
		}
		TArray<FColor> Pixels;
		FIntVector Size;
		if (!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size))
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::EditorRefused,
				TEXT("Slate window screenshot failed."),
				TEXT("The window may be minimized or driven by a renderer without screenshot support."));
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
		if (Png.Num() == 0)
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::InternalError,
				TEXT("PNG compression produced no data."),
				TEXT("This is an EditorAutomation/engine bug; report the capture that produced it."));
		}
		TArray<uint8> PngBytes;
		PngBytes.Append(Png.GetData(), static_cast<int32>(Png.Num()));
		if (!FFileHelper::SaveArrayToFile(PngBytes, *ResolvedPath))
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::EditorRefused,
				FString::Printf(TEXT("Could not write '%s'."), *ResolvedPath),
				TEXT("Check that the directory exists and is writable."));
		}
		OutFilePath = ResolvedPath;
		return FEditorAutomationResult::Success();
	}

	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("No PIE game viewport is available."),
			TEXT("Start PIE before capturing the game viewport."));
	}
	FScreenshotRequest::RequestScreenshot(ResolvedPath, bShowUI, /*bAddFilenameSuffix=*/false);
	OutFilePath = ResolvedPath;
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::StopPIE()
{
	if (!IsInGameThread() || GEditor == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("StopPIE must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	GEditor->RequestEndPlayMap();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::IsPIERunning(bool& OutRunning)
{
	OutRunning = IsInGameThread() && GEditor != nullptr && GEditor->IsPlaySessionInProgress();
	return FEditorAutomationResult::Success();
}

namespace
{
	/** The Slate widget of the running PIE game viewport, or null when PIE is not showing one. */
	TSharedPtr<SViewport> GetPIEViewportWidget()
	{
		if (GEngine == nullptr || GEngine->GameViewport == nullptr) return nullptr;
		return GEngine->GameViewport->GetGameViewportWidget();
	}
}

FEditorAutomationResult UEditorAutomationCoreLibrary::FocusPIEViewport()
{
	if (!IsInGameThread() || GEngine == nullptr || GEditor == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("FocusPIEViewport must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	TSharedPtr<SViewport> ViewportWidget = GetPIEViewportWidget();
	if (!ViewportWidget.IsValid())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("No PIE game viewport is currently shown."),
			TEXT("Start PIE first."));
	}

	FSlateApplication& SlateApp = FSlateApplication::Get();
	const TSharedPtr<SWindow> ViewportWindow = SlateApp.FindWidgetWindow(ViewportWidget.ToSharedRef());
	if (ViewportWindow.IsValid())
	{
		ViewportWindow->BringToFront(/*bForce=*/true);
	}
	SlateApp.SetKeyboardFocus(ViewportWidget, EFocusCause::SetDirectly);

	if (UWorld* World = GEngine->GameViewport->GetWorld())
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			PlayerController->bShowMouseCursor = false;
			FInputModeGameOnly InputMode;
			PlayerController->SetInputMode(InputMode);
		}
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::SendKeyToPIE(const FString& KeyName, const bool bFocusViewport)
{
	if (!IsInGameThread() || GEngine == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("SendKeyToPIE must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	const FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("'%s' is not a valid FKey name."), *KeyName),
			TEXT("Pass an FKey name such as Enter, SpaceBar, Tab, Escape, Up, Down, Left, Right, W, A, S, D or R."));
	}
	if (bFocusViewport)
	{
		const FEditorAutomationResult FocusResult = FocusPIEViewport();
		if (!FocusResult.bSuccess)
		{
			return FocusResult;
		}
	}

	// The modifier state is whatever Slate currently reports, matching the pre-existing
	// behavior: for explicit modifiers use FocusPIEViewport() + the Input library's SendKey.
	FSlateApplication& SlateApp = FSlateApplication::Get();
	const FModifierKeysState ModifierKeys = SlateApp.GetModifierKeys();
	FKeyEvent KeyDownEvent(Key, ModifierKeys, /*InUserIndex=*/0, /*bInIsRepeat=*/false, /*InCharacterCode=*/0, /*InKeyCode=*/0);
	SlateApp.ProcessKeyDownEvent(KeyDownEvent);
	FKeyEvent KeyUpEvent(Key, ModifierKeys, /*InUserIndex=*/0, /*bInIsRepeat=*/false, /*InCharacterCode=*/0, /*InKeyCode=*/0);
	SlateApp.ProcessKeyUpEvent(KeyUpEvent);
	return FEditorAutomationResult::Success();
}
