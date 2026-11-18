// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationInputLibrary.h"

#include "EditorAutomationCoreLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Math/UnrealMathUtility.h"
#include "Widgets/SWindow.h"

namespace
{
	/**
	 * Single-flight guard for synthetic mouse input: while an asynchronous SendDrag
	 * occupies the primary pointer, further mouse input is rejected rather than
	 * colliding with it.
	 */
	bool bMouseDragInProgress = false;

	/**
	 * Resolve one widget's Slate widget and painted geometry.
	 *
	 * A widget's geometry (and its hit-test entry) are written by Slate's paint pass, so a
	 * widget that was never drawn reports NotReady instead of receiving input at a degenerate
	 * point; every failure carries its reason in the envelope.
	 */
	FEditorAutomationResult ResolvePaintedWidget(
		UWidget* Widget,
		const TCHAR* FunctionName,
		TSharedPtr<SWidget>& OutSlateWidget,
		const FGeometry*& OutGeometry)
	{
		if (!IsInGameThread())
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::NotReady,
				FString::Printf(TEXT("%s must be called on the game thread."), FunctionName),
				TEXT("Retry from the editor's game thread."));
		}
		if (Widget == nullptr)
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::InvalidArgument,
				FString::Printf(TEXT("%s: widget is null."), FunctionName),
				TEXT("Pass a UWidget object path; use GetWidgetRoots to find one."));
		}
		OutSlateWidget = Widget->GetCachedWidget();
		if (!OutSlateWidget.IsValid())
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::NotReady,
				FString::Printf(TEXT("%s: widget '%s' has no cached Slate widget."), FunctionName, *Widget->GetPathName()),
				TEXT("Ensure the widget is constructed and visible, then retry."));
		}
		OutGeometry = &OutSlateWidget->GetCachedGeometry();
		if (OutGeometry->GetAbsoluteSize().IsNearlyZero())
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::NotReady,
				FString::Printf(TEXT("%s: widget '%s' has no painted geometry yet (zero size)."), FunctionName, *Widget->GetPathName()),
				TEXT("Wait for the next paint/frame pass, then retry."));
		}
		return FEditorAutomationResult::Success();
	}

	/** Bring the owning window to the front; returns its native window when found. */
	TSharedPtr<FGenericWindow> BringOwnerToFront(const TSharedPtr<SWidget>& SlateWidget)
	{
		TSharedPtr<FGenericWindow> NativeWindow;
		if (const TSharedPtr<SWindow> FoundWindow = FSlateApplication::Get().FindWidgetWindow(SlateWidget.ToSharedRef()))
		{
			FoundWindow->BringToFront(true);
			NativeWindow = FoundWindow->GetNativeWindow();
		}
		return NativeWindow;
	}

	FEditorAutomationResult MouseInputBusy(const TCHAR* FunctionName)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			FString::Printf(TEXT("%s: a mouse input is already in progress."), FunctionName),
			TEXT("Wait for IsMouseInputPending to report false, then retry."));
	}
}

FModifierKeysState FEditorAutomationModifierKeys::ToSlate() const
{
	// FModifierKeysState stores each modifier's left/right state separately, so an
	// agnostic bX field sets both sides while bLeftX/bRightX express a single side.
	auto Left = [](bool Any, bool InLeft, bool InRight) { return InLeft || (Any && !InRight); };
	auto Right = [](bool Any, bool InLeft, bool InRight) { return InRight || (Any && !InLeft); };

	return FModifierKeysState(
		Left(bShift, bLeftShift, bRightShift),
		Right(bShift, bLeftShift, bRightShift),
		Left(bControl, bLeftControl, bRightControl),
		Right(bControl, bLeftControl, bRightControl),
		Left(bAlt, bLeftAlt, bRightAlt),
		Right(bAlt, bLeftAlt, bRightAlt),
		Left(bCommand, bLeftCommand, bRightCommand),
		Right(bCommand, bLeftCommand, bRightCommand),
		false);
}

FEditorAutomationResult UEditorAutomationInputLibrary::SendClick(
	UWidget* Widget,
	const FEditorAutomationSendClickOptions& Options)
{
	if (bMouseDragInProgress)
	{
		return MouseInputBusy(TEXT("SendClick"));
	}

	TSharedPtr<SWidget> SlateWidget;
	const FGeometry* Geometry = nullptr;
	const FEditorAutomationResult Resolved = ResolvePaintedWidget(Widget, TEXT("SendClick"), SlateWidget, Geometry);
	if (!Resolved.bSuccess)
	{
		return Resolved;
	}

	const FKey ButtonKey(*Options.Button);
	if (!ButtonKey.IsValid())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("SendClick: '%s' is not a valid mouse-button FKey name."), *Options.Button),
			TEXT("Pass an FKey name such as LeftMouseButton or RightMouseButton."));
	}

	const FVector2D Clamped(
		FMath::Clamp(Options.RelativePosition.X, 0.f, 1.f),
		FMath::Clamp(Options.RelativePosition.Y, 0.f, 1.f));
	const FVector2D ClickPoint = Geometry->GetAbsolutePosition() + Geometry->GetAbsoluteSize() * Clamped;

	const FModifierKeysState ModifierState = Options.ModifierKeys.ToSlate();
	const TSharedPtr<FGenericWindow> NativeWindow = BringOwnerToFront(SlateWidget);

	// Position the cursor directly at the click point; an off-screen detour degrades
	// Slate's hover/pointer state over repeated synthetic inputs.
	const TSet<FKey> NoButtons;
	FPointerEvent MoveEvent(FInputDeviceId::CreateFromInternalId(0), 0,
		ClickPoint, ClickPoint, NoButtons, FKey(), 0.f, ModifierState);
	FSlateApplication::Get().ProcessMouseMoveEvent(MoveEvent);

	const TSet<FKey> PressedButtons{ ButtonKey };
	FPointerEvent MouseDownEvent(FInputDeviceId::CreateFromInternalId(0), 0,
		ClickPoint, ClickPoint, PressedButtons, ButtonKey, 0.f, ModifierState);
	FPointerEvent MouseUpEvent(FInputDeviceId::CreateFromInternalId(0), 0,
		ClickPoint, ClickPoint, NoButtons, ButtonKey, 0.f, ModifierState);
	FSlateApplication::Get().ProcessMouseButtonDownEvent(NativeWindow, MouseDownEvent);
	FSlateApplication::Get().ProcessMouseButtonUpEvent(MouseUpEvent);

	if (Options.ClickType == EEditorAutomationClickType::Double)
	{
		FPointerEvent DoubleClickEvent(FInputDeviceId::CreateFromInternalId(0), 0,
			ClickPoint, ClickPoint, PressedButtons, ButtonKey, 0.f, ModifierState);
		FSlateApplication::Get().ProcessMouseButtonDoubleClickEvent(NativeWindow, DoubleClickEvent);
		FSlateApplication::Get().ProcessMouseButtonUpEvent(MouseUpEvent);
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationInputLibrary::Focus(UWidget* Widget)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("Focus must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	if (Widget == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Focus: widget is null."),
			TEXT("Pass a UWidget object path; use GetWidgetRoots to find one."));
	}
	const TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			FString::Printf(TEXT("Focus: widget '%s' has no cached Slate widget."), *Widget->GetPathName()),
			TEXT("Ensure the widget is constructed and visible, then retry."));
	}
	BringOwnerToFront(SlateWidget);
	FSlateApplication::Get().SetKeyboardFocus(SlateWidget, EFocusCause::SetDirectly);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationInputLibrary::SendKey(
	const FString& KeyName,
	const FEditorAutomationModifierKeys& Modifiers)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("SendKey must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	const FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("SendKey: '%s' is not a valid FKey name."), *KeyName),
			TEXT("Pass an FKey name such as Enter, SpaceBar, Tab, Escape, Up, Down, Left, Right, W, A, S, D or R."));
	}
	const FModifierKeysState ModifierState = Modifiers.ToSlate();
	FKeyEvent KeyDownEvent(Key, ModifierState, 0, false, 0, 0);
	FKeyEvent KeyUpEvent(Key, ModifierState, 0, false, 0, 0);
	FSlateApplication::Get().ProcessKeyDownEvent(KeyDownEvent);
	FSlateApplication::Get().ProcessKeyUpEvent(KeyUpEvent);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationInputLibrary::SendText(const FString& Text)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("SendText must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	for (const TCHAR Char : Text)
	{
		FCharacterEvent CharEvent(Char, FModifierKeysState(), 0, false);
		FSlateApplication::Get().ProcessKeyCharEvent(CharEvent);
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationInputLibrary::SendMouseMove(
	UWidget* Widget,
	const FVector2D RelativePosition,
	const FEditorAutomationModifierKeys& ModifierKeys)
{
	if (bMouseDragInProgress)
	{
		return MouseInputBusy(TEXT("SendMouseMove"));
	}

	TSharedPtr<SWidget> SlateWidget;
	const FGeometry* Geometry = nullptr;
	const FEditorAutomationResult Resolved = ResolvePaintedWidget(Widget, TEXT("SendMouseMove"), SlateWidget, Geometry);
	if (!Resolved.bSuccess)
	{
		return Resolved;
	}

	const FVector2D Clamped(
		FMath::Clamp(RelativePosition.X, 0.f, 1.f),
		FMath::Clamp(RelativePosition.Y, 0.f, 1.f));
	const FVector2D Point = Geometry->GetAbsolutePosition() + Geometry->GetAbsoluteSize() * Clamped;
	BringOwnerToFront(SlateWidget);

	const TSet<FKey> NoButtons;
	FPointerEvent MoveEvent(FInputDeviceId::CreateFromInternalId(0), 0,
		Point, Point, NoButtons, FKey(), 0.f, ModifierKeys.ToSlate());
	FSlateApplication::Get().ProcessMouseMoveEvent(MoveEvent);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationInputLibrary::SendMouseWheel(UWidget* Widget, const float Delta)
{
	if (bMouseDragInProgress)
	{
		return MouseInputBusy(TEXT("SendMouseWheel"));
	}
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("SendMouseWheel must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	if (Widget == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("SendMouseWheel: widget is null."),
			TEXT("Pass a UWidget object path; use GetWidgetRoots to find one."));
	}
	const TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			FString::Printf(TEXT("SendMouseWheel: widget '%s' has no cached Slate widget."), *Widget->GetPathName()),
			TEXT("Ensure the widget is constructed and visible, then retry."));
	}
	BringOwnerToFront(SlateWidget);

	const TSet<FKey> NoButtons;
	FPointerEvent WheelEvent(
		FInputDeviceId::CreateFromInternalId(0), 0,
		FSlateApplication::Get().GetCursorPos(), FSlateApplication::Get().GetCursorPos(),
		NoButtons, EKeys::Invalid, Delta, FModifierKeysState());
	FSlateApplication::Get().ProcessMouseWheelOrGestureEvent(WheelEvent, nullptr);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationInputLibrary::SendDrag(
	UWidget* Widget,
	const FEditorAutomationSendDragOptions& Options)
{
	if (bMouseDragInProgress)
	{
		return MouseInputBusy(TEXT("SendDrag"));
	}

	TSharedPtr<SWidget> SlateWidget;
	const FGeometry* Geometry = nullptr;
	const FEditorAutomationResult Resolved = ResolvePaintedWidget(Widget, TEXT("SendDrag"), SlateWidget, Geometry);
	if (!Resolved.bSuccess)
	{
		return Resolved;
	}

	const FKey ButtonKey(*Options.Button);
	if (!ButtonKey.IsValid())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("SendDrag: '%s' is not a valid mouse-button FKey name."), *Options.Button),
			TEXT("Pass an FKey name such as LeftMouseButton or RightMouseButton."));
	}

	const FVector2D ClampFrom(
		FMath::Clamp(Options.FromRelativePosition.X, 0.f, 1.f),
		FMath::Clamp(Options.FromRelativePosition.Y, 0.f, 1.f));
	const FVector2D ClampTo(
		FMath::Clamp(Options.ToRelativePosition.X, 0.f, 1.f),
		FMath::Clamp(Options.ToRelativePosition.Y, 0.f, 1.f));
	const FVector2D FromPoint = Geometry->GetAbsolutePosition() + Geometry->GetAbsoluteSize() * ClampFrom;
	const FVector2D ToPoint = Geometry->GetAbsolutePosition() + Geometry->GetAbsoluteSize() * ClampTo;

	const FModifierKeysState ModifierState = Options.ModifierKeys.ToSlate();
	const TSharedPtr<FGenericWindow> NativeWindow = BringOwnerToFront(SlateWidget);

	const TSet<FKey> NoButtons;
	FPointerEvent MoveToFrom(FInputDeviceId::CreateFromInternalId(0), 0,
		FromPoint, FromPoint, NoButtons, FKey(), 0.f, ModifierState);
	FSlateApplication::Get().ProcessMouseMoveEvent(MoveToFrom);

	const int32 Steps = FMath::Clamp(Options.Steps, 1, 200);
	const float StepMs = FMath::Max(1.0f, FMath::Clamp(Options.DurationMs, 1.f, 10000.f)) / Steps;
	const double Interval = FMath::Max(0.001, static_cast<double>(StepMs) * 0.001);

	struct FDragState
	{
		TWeakPtr<SWidget> WeakWidget;
		TSharedPtr<FGenericWindow> NativeWindow;
		FModifierKeysState ModifierState = FModifierKeysState();
		FKey Button;
		FVector2D FromPoint;
		FVector2D ToPoint;
		int32 Steps = 0;
		int32 CurrentStep = 0; // 0 = down, 1..Steps = moves, Steps+1 = up
		bool bDone = false;
	};

	const TSharedPtr<FDragState> State = MakeShared<FDragState>();
	State->WeakWidget = SlateWidget;
	State->NativeWindow = NativeWindow;
	State->ModifierState = ModifierState;
	State->Button = ButtonKey;
	State->FromPoint = FromPoint;
	State->ToPoint = ToPoint;
	State->Steps = Steps;

	bMouseDragInProgress = true;
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[State](float) -> bool
			{
				if (State->bDone)
				{
					bMouseDragInProgress = false;
					return false;
				}
				if (!State->WeakWidget.Pin().IsValid())
				{
					State->bDone = true;
					bMouseDragInProgress = false;
					return false;
				}

				const TSet<FKey> PressedButtons{ State->Button };
				const TSet<FKey> NoButtons;

				if (State->CurrentStep == 0)
				{
					FPointerEvent DownEvent(FInputDeviceId::CreateFromInternalId(0), 0,
						State->FromPoint, State->FromPoint, PressedButtons, State->Button, 0.f, State->ModifierState);
					FSlateApplication::Get().ProcessMouseButtonDownEvent(State->NativeWindow, DownEvent);
					State->CurrentStep = 1;
					return true;
				}
				if (State->CurrentStep <= State->Steps)
				{
					const float Alpha = static_cast<float>(State->CurrentStep) / State->Steps;
					const FVector2D InterpPoint = FMath::Lerp(State->FromPoint, State->ToPoint, Alpha);
					FPointerEvent MoveEvent(FInputDeviceId::CreateFromInternalId(0), 0,
						InterpPoint, InterpPoint, PressedButtons, State->Button, 0.f, State->ModifierState);
					FSlateApplication::Get().ProcessMouseMoveEvent(MoveEvent);
					++State->CurrentStep;
					return true;
				}

				FPointerEvent UpEvent(FInputDeviceId::CreateFromInternalId(0), 0,
					State->ToPoint, State->ToPoint, NoButtons, State->Button, 0.f, State->ModifierState);
				FSlateApplication::Get().ProcessMouseButtonUpEvent(UpEvent);
				State->bDone = true;
				bMouseDragInProgress = false;
				return false;
			}),
		Interval);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationInputLibrary::IsMouseInputPending(bool& OutPending)
{
	OutPending = bMouseDragInProgress;
	return FEditorAutomationResult::Success();
}
