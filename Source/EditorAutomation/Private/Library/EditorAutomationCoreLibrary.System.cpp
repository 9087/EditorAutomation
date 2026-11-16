// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationCoreLibrary.h"

#include "EditorAutomationLog.h"

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

FString UEditorAutomationCoreLibrary::Ping()
{
	return TEXT("pong");
}

FEditorAutomationEditorInfo UEditorAutomationCoreLibrary::GetEditorInfo()
{
	FEditorAutomationEditorInfo Info;
	Info.ProcessId = static_cast<int64>(FPlatformProcess::GetCurrentProcessId());
	Info.EngineVersion = FEngineVersion::Current().ToString();
	Info.ProjectName = FApp::GetProjectName();
	Info.ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	Info.bIsEditor = GIsEditor;
	return Info;
}

bool UEditorAutomationCoreLibrary::StartPIE(const FString& LevelPath, bool bNewWindow)
{
	if (!IsInGameThread() || GEditor == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("StartPIE must be called on the game thread."));
		return false;
	}
	if (GEditor->IsPlaySessionInProgress())
	{
		return true;
	}
	if (!LevelPath.IsEmpty() && !FPackageName::DoesPackageExist(LevelPath))
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("StartPIE: Level '%s' does not exist."), *LevelPath);
		return false;
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
	return true;
}

FString UEditorAutomationCoreLibrary::CaptureScreenshot(
	const FString& FilePath,
	const EEditorAutomationScreenshotTarget Target,
	const bool bShowUI)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CaptureScreenshot must be called on the game thread."));
		return FString();
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
			UE_LOG(LogEditorAutomation, Error, TEXT("CaptureScreenshot: no active editor window to capture."));
			return FString();
		}
		TArray<FColor> Pixels;
		FIntVector Size;
		if (!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size))
		{
			UE_LOG(LogEditorAutomation, Error, TEXT("CaptureScreenshot: Slate window screenshot failed."));
			return FString();
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Png);
		if (Png.Num() == 0)
		{
			UE_LOG(LogEditorAutomation, Error, TEXT("CaptureScreenshot: PNG compression produced no data."));
			return FString();
		}
		TArray<uint8> PngBytes;
		PngBytes.Append(Png.GetData(), static_cast<int32>(Png.Num()));
		if (!FFileHelper::SaveArrayToFile(PngBytes, *ResolvedPath))
		{
			UE_LOG(LogEditorAutomation, Error, TEXT("CaptureScreenshot: could not write '%s'."), *ResolvedPath);
			return FString();
		}
		return ResolvedPath;
	}

	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CaptureScreenshot: no PIE game viewport; start PIE before capturing the game viewport."));
		return FString();
	}
	FScreenshotRequest::RequestScreenshot(ResolvedPath, bShowUI, /*bAddFilenameSuffix=*/false);
	return ResolvedPath;
}

bool UEditorAutomationCoreLibrary::StopPIE()
{
	if (!IsInGameThread() || GEditor == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("StopPIE must be called on the game thread."));
		return false;
	}
	GEditor->RequestEndPlayMap();
	return true;
}

bool UEditorAutomationCoreLibrary::IsPIERunning()
{
	return IsInGameThread() && GEditor != nullptr && GEditor->IsPlaySessionInProgress();
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

bool UEditorAutomationCoreLibrary::FocusPIEViewport()
{
	if (!IsInGameThread() || GEngine == nullptr || GEditor == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("FocusPIEViewport must be called on the game thread."));
		return false;
	}
	TSharedPtr<SViewport> ViewportWidget = GetPIEViewportWidget();
	if (!ViewportWidget.IsValid())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("FocusPIEViewport: no PIE game viewport is currently shown."));
		return false;
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
	return true;
}

bool UEditorAutomationCoreLibrary::SendKeyToPIE(const FString& KeyName, bool bFocusViewport)
{
	if (!IsInGameThread() || GEngine == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SendKeyToPIE must be called on the game thread."));
		return false;
	}
	FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SendKeyToPIE: '%s' is not a valid FKey name."), *KeyName);
		return false;
	}
	if (bFocusViewport && !FocusPIEViewport())
	{
		return false;
	}

	FSlateApplication& SlateApp = FSlateApplication::Get();
	const FModifierKeysState ModifierKeys = SlateApp.GetModifierKeys();
	FKeyEvent KeyDownEvent(Key, ModifierKeys, /*InUserIndex=*/0, /*bInIsRepeat=*/false, /*InCharacterCode=*/0, /*InKeyCode=*/0);
	SlateApp.ProcessKeyDownEvent(KeyDownEvent);
	FKeyEvent KeyUpEvent(Key, ModifierKeys, /*InUserIndex=*/0, /*bInIsRepeat=*/false, /*InCharacterCode=*/0, /*InKeyCode=*/0);
	SlateApp.ProcessKeyUpEvent(KeyUpEvent);
	return true;
}
