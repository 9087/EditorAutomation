// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EditorAutomationCoreLibrary.h"

#include "EditorAutomationInputLibrary.generated.h"

class UWidget;

/** Which kind of click SendClick performs; the wire value is a plain integer. */
UENUM(BlueprintType)
enum class EEditorAutomationClickType : uint8
{
	/** One press + release. */
	Single = 0,
	/** Press + release followed by a double-click event (two clicks in quick succession). */
	Double = 1
};

/**
 * Modifier-key state for synthetic input.
 *
 * The agnostic `bControl`/`bAlt`/`bShift`/`bCommand` fields mean "this modifier is
 * down on either side"; the `bLeft*`/`bRight*` fields are optional overrides that
 * let a caller express a single side.
 */
USTRUCT(BlueprintType)
struct FEditorAutomationModifierKeys
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bControl = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bLeftControl = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bRightControl = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bAlt = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bLeftAlt = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bRightAlt = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bShift = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bLeftShift = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bRightShift = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bCommand = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bLeftCommand = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	bool bRightCommand = false;

	/**
	 * Converts this reflectable state into Slate's FModifierKeysState.
	 * The engine type stores left/right separately (no "any" slot): an agnostic
	 * `bX` field sets both sides, while `bLeftX`/`bRightX` express one side.
	 */
	FModifierKeysState ToSlate() const;
};

/** Options of one SendClick call. */
USTRUCT(BlueprintType)
struct FEditorAutomationSendClickOptions
{
	GENERATED_BODY()

	/** Mouse button FKey name to press and release (default LeftMouseButton). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	FString Button = TEXT("LeftMouseButton");

	/** Single press/release, or a double click. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	EEditorAutomationClickType ClickType = EEditorAutomationClickType::Single;

	/** Modifier keys held during the click. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	FEditorAutomationModifierKeys ModifierKeys;

	/** Point inside the widget, normalized 0..1 (0.5,0.5 = center). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	FVector2D RelativePosition = FVector2D(0.5f, 0.5f);
};

/** Options of one SendDrag call. */
USTRUCT(BlueprintType)
struct FEditorAutomationSendDragOptions
{
	GENERATED_BODY()

	/** Drag origin inside the widget, normalized 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	FVector2D FromRelativePosition = FVector2D(0.0f, 0.5f);

	/** Drag target inside the widget, normalized 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	FVector2D ToRelativePosition = FVector2D(1.0f, 0.5f);

	/** Move steps between press and release (1..200). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	int32 Steps = 10;

	/** Total drag duration in milliseconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	float DurationMs = 500.0f;

	/** Mouse button FKey name that stays pressed during the drag (default LeftMouseButton). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	FString Button = TEXT("LeftMouseButton");

	/** Modifier keys held during the drag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EditorAutomation|Input")
	FEditorAutomationModifierKeys ModifierKeys;
};

/**
 * Synthetic Slate input for live UMG widgets.
 *
 * These functions drive Slate's own event pipeline (the same one a physical mouse
 * and keyboard feed), so they work in PIE and against editor widgets alike. They
 * only accept a widget whose Slate widget has been drawn at least once, because a
 * widget's geometry and hit-test entry are written by the paint pass.
 *
 * Failures are logged to LogEditorAutomation and return false (or empty) — the
 * native-value contract of every EditorAutomation library.
 *
 * Reached from Remote Control with the CDO path
 * `/Script/EditorAutomation.Default__EditorAutomationInputLibrary`.
 */
UCLASS()
class EDITORAUTOMATION_API UEditorAutomationInputLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Click one widget at a normalized position.
	 *
	 * @param Widget          The target widget; its cached Slate widget must have been drawn.
	 * @param Options         Button, click type, modifiers and the point inside the widget.
	 * @returns true on success; a reason is logged on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation|Input")
	static FEditorAutomationResult SendClick(UWidget* Widget, const FEditorAutomationSendClickOptions& Options);

	/** Move keyboard focus to one widget (and bring its window to the front). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation|Input")
	static FEditorAutomationResult Focus(UWidget* Widget);

	/** Send one key press (down + up) with optional modifier keys. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation|Input")
	static FEditorAutomationResult SendKey(const FString& KeyName, const FEditorAutomationModifierKeys& Modifiers);

	/** Send a string as character events to the focused widget. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation|Input")
	static FEditorAutomationResult SendText(const FString& Text);

	/** Move the synthetic mouse pointer over one widget at a normalized position. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation|Input")
	static FEditorAutomationResult SendMouseMove(
		UWidget* Widget,
		FVector2D RelativePosition,
		const FEditorAutomationModifierKeys& ModifierKeys);

	/** Send one mouse-wheel event at the current pointer position. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation|Input")
	static FEditorAutomationResult SendMouseWheel(UWidget* Widget, float Delta);

	/** Drag from one normalized point to another inside the same widget (ticker-driven across frames). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation|Input")
	static FEditorAutomationResult SendDrag(UWidget* Widget, const FEditorAutomationSendDragOptions& Options);

	/** Whether an asynchronous mouse input (a SendDrag) is still running. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomation|Input")
	static FEditorAutomationResult IsMouseInputPending(bool& OutPending);
};