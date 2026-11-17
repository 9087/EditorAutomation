// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationInputLibrary.h"

#include "EditorAutomationLog.h"

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
	 * A widget's geometry (and its hit-test entry) are written by Slate's paint
	 * pass, so a widget that was never drawn reports "not ready" instead of
	 * receiving input aimed at a degenerate point.
	 */
	bool ResolvePaintedWidget(
		UWidget* Widget,
		const TCHAR* FunctionName,
		TSharedPtr<SWidget>& OutSlateWidget,
		const FGeometry*& OutGeometry)
	{
		if (!IsInGameThread())
		{
			UE_LOG(LogEditorAutomation, Error, TEXT("%s must be called on the game thread."), FunctionName);
			return false;
		}
		if (Widget == nullptr)
		{
			UE_LOG(LogEditorAutomation, Error, TEXT("%s: Widget must not be null."), FunctionName);
			return false;
		}
		OutSlateWidget = Widget->GetCachedWidget();
		if (!OutSlateWidget.IsValid())
		{
			UE_LOG(LogEditorAutomation, Error,
				TEXT("%s: widget '%s' has no cached Slate widget; ensure it is constructed and visible."),
				FunctionName, *Widget->GetPathName());
			return false;
		}
		OutGeometry = &OutSlateWidget->GetCachedGeometry();
		if (OutGeometry->GetAbsoluteSize().IsNearlyZero())
		{
			UE_LOG(LogEditorAutomation, Error,
				TEXT("%s: widget '%s' has no painted geometry yet (zero size); retry after the next frame."),
				FunctionName, *Widget->GetPathName());
			return false;
		}
		return true;
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

bool UEditorAutomationInputLibrary::SendClick(UWidget* Widget, const FEditorAutomationSendClickOptions& Options)
{
	if (bMouseDragInProgress)
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("SendClick: a mouse input is already in progress; wait for IsMouseInputPending to clear."));
		return false;
	}

	TSharedPtr<SWidget> SlateWidget;
	const FGeometry* Geometry = nullptr;
	if (!ResolvePaintedWidget(Widget, TEXT("SendClick"), SlateWidget, Geometry))
	{
		return false;
	}

	const FKey ButtonKey(*Options.Button);
	if (!ButtonKey.IsValid())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SendClick: '%s' is not a valid mouse-button FKey name."), *Options.Button);
		return false;
	}
	const FVector2D Clamped(
		FMath::Clamp(Options.RelativePosition.X, 0.f, 1.f),
		FMath::Clamp(Options.RelativePosition.Y, 0.f, 1.f));
	const FVector2D ClickPoint = Geometry->GetAbsolutePosition() + Geometry->GetAbsoluteSize() * Clamped;

	const FModifierKeysState ModifierState = Options.ModifierKeys.ToSlate();
	const TSharedPtr<FGenericWindow> NativeWindow = BringOwnerToFront(SlateWidget);

	// Position the cursor directly at the click point; an off-screen detour
	// degrades Slate's hover/pointer state over repeated synthetic inputs.
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
	return true;
}

bool UEditorAutomationInputLibrary::Focus(UWidget* Widget)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("Focus must be called on the game thread."));
		return false;
	}
	if (Widget == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("Focus: Widget must not be null."));
		return false;
	}
	const TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("Focus: widget '%s' has no cached Slate widget; ensure it is constructed and visible."),
			*Widget->GetPathName());
		return false;
	}
	BringOwnerToFront(SlateWidget);
	FSlateApplication::Get().SetKeyboardFocus(SlateWidget, EFocusCause::SetDirectly);
	return true;
}

bool UEditorAutomationInputLibrary::SendKey(const FString& KeyName, const FEditorAutomationModifierKeys& Modifiers)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SendKey must be called on the game thread."));
		return false;
	}
	const FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SendKey: '%s' is not a valid FKey name."), *KeyName);
		return false;
	}
	const FModifierKeysState ModifierState = Modifiers.ToSlate();
	FKeyEvent KeyDownEvent(Key, ModifierState, 0, false, 0, 0);
	FKeyEvent KeyUpEvent(Key, ModifierState, 0, false, 0, 0);
	FSlateApplication::Get().ProcessKeyDownEvent(KeyDownEvent);
	FSlateApplication::Get().ProcessKeyUpEvent(KeyUpEvent);
	return true;
}

bool UEditorAutomationInputLibrary::SendText(const FString& Text)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SendText must be called on the game thread."));
		return false;
	}
	for (const TCHAR Char : Text)
	{
		FCharacterEvent CharEvent(Char, FModifierKeysState(), 0, false);
		FSlateApplication::Get().ProcessKeyCharEvent(CharEvent);
	}
	return true;
}

bool UEditorAutomationInputLibrary::SendMouseMove(
	UWidget* Widget, FVector2D RelativePosition, const FEditorAutomationModifierKeys& ModifierKeys)
{
	if (bMouseDragInProgress)
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("SendMouseMove: a mouse input is already in progress; wait for IsMouseInputPending to clear."));
		return false;
	}

	TSharedPtr<SWidget> SlateWidget;
	const FGeometry* Geometry = nullptr;
	if (!ResolvePaintedWidget(Widget, TEXT("SendMouseMove"), SlateWidget, Geometry))
	{
		return false;
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
	return true;
}

bool UEditorAutomationInputLibrary::SendMouseWheel(UWidget* Widget, float Delta)
{
	if (bMouseDragInProgress)
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("SendMouseWheel: a mouse input is already in progress; wait for IsMouseInputPending to clear."));
		return false;
	}
	if (!IsInGameThread())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SendMouseWheel must be called on the game thread."));
		return false;
	}
	if (Widget == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SendMouseWheel: Widget must not be null."));
		return false;
	}
	const TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("SendMouseWheel: widget '%s' has no cached Slate widget."), *Widget->GetPathName());
		return false;
	}
	BringOwnerToFront(SlateWidget);

	const TSet<FKey> NoButtons;
	FPointerEvent WheelEvent(
		FInputDeviceId::CreateFromInternalId(0), 0,
		FSlateApplication::Get().GetCursorPos(), FSlateApplication::Get().GetCursorPos(),
		NoButtons, EKeys::Invalid, Delta, FModifierKeysState());
	FSlateApplication::Get().ProcessMouseWheelOrGestureEvent(WheelEvent, nullptr);
	return true;
}

bool UEditorAutomationInputLibrary::SendDrag(UWidget* Widget, const FEditorAutomationSendDragOptions& Options)
{
	if (bMouseDragInProgress)
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("SendDrag: a mouse input is already in progress; wait for IsMouseInputPending to clear."));
		return false;
	}

	TSharedPtr<SWidget> SlateWidget;
	const FGeometry* Geometry = nullptr;
	if (!ResolvePaintedWidget(Widget, TEXT("SendDrag"), SlateWidget, Geometry))
	{
		return false;
	}

	const FKey ButtonKey(*Options.Button);
	if (!ButtonKey.IsValid())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("SendDrag: '%s' is not a valid mouse-button FKey name."), *Options.Button);
		return false;
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
	return true;
}

bool UEditorAutomationInputLibrary::IsMouseInputPending()
{
	return bMouseDragInProgress;
}
