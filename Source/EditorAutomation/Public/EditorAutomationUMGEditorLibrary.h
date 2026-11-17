// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EditorAutomationUMGEditorLibrary.generated.h"

class UBlueprint;
class UWidget;
class UPanelSlot;
class UWidgetBlueprint;
class UWidgetTree;

/** One text widget and its current visual state. */
USTRUCT(BlueprintType)
struct FEditorAutomationWidgetTextState
{
	GENERATED_BODY()

	/** Object path of the text widget. */
	UPROPERTY(BlueprintReadOnly) FString WidgetPath;
	/** Current text as a plain string. */
	UPROPERTY(BlueprintReadOnly) FString Text;
	/** ESlateVisibility display name, e.g. "Visible", "Collapsed", "Hidden". */
	UPROPERTY(BlueprintReadOnly) FString Visibility;
	/** Whether the widget is enabled (UWidget::GetIsEnabled). */
	UPROPERTY(BlueprintReadOnly) bool bIsEnabled = true;
	/** Whether the widget is currently visible (UWidget::IsVisible). */
	UPROPERTY(BlueprintReadOnly) bool bIsVisible = false;
};

UCLASS()
class EDITORAUTOMATION_API UEditorAutomationUMGEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static bool OpenWidgetGraph(const FString& WidgetBlueprintPath);

	/** Create a Widget Blueprint asset; returns its object path, or empty (logged) on failure. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FString CreateWidgetBlueprint(
		const FString& AssetPath,
		const FString& ParentClassPath = TEXT(""));

	/** Add the first root widget; returns the new widget's object path, or empty (logged) on failure. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FString AddRootWidget(
		const FString& WidgetBlueprintPath,
		const FString& WidgetClassPath,
		FName WidgetName);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static TArray<UWidget*> GetWidgetTree(const FString& WidgetBlueprintPath);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static UWidget* GetWidgetParent(UWidget* Widget);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static TArray<UWidget*> GetWidgetChildren(UWidget* Widget);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static UPanelSlot* GetWidgetSlot(UWidget* Widget);

	/** Add a widget under a panel/content parent; returns the new widget's object path, or empty (logged) on failure. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FString AddWidget(
		UWidget* Parent,
		const FString& WidgetClassPath,
		FName WidgetName,
		int32 ChildIndex = -1);

	/** Remove a widget; returns false (logged) when the widget is invalid. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static bool RemoveWidget(UWidget* Widget);

	/** Rename a widget; returns the new object path, or empty (logged) on failure. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FString RenameWidget(UWidget* Widget, FName NewName);

	/** Move a widget under another parent; returns false (logged) when a parent/index is unsupported. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static bool ReparentWidget(
		UWidget* Widget,
		UWidget* NewParent,
		int32 ChildIndex = -1);

	/** Write one widget property through the core property writer; returns false (logged) on failure. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static bool SetWidgetProperty(
		UWidget* Widget,
		const FString& PropertyPath,
		const FString& ValueJson);

	/** Write one widget slot property through the core property writer; returns false (logged) on failure. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static bool SetWidgetSlotProperty(
		UPanelSlot* Slot,
		const FString& PropertyPath,
		const FString& ValueJson);




	/**
	 * Whether a widget name is exposed as a variable of one Widget Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static bool IsWidgetVariable(UWidget* Widget, UBlueprint* WidgetBlueprint);

	/**
	 * The root widget of the user widget that owns this widget (walking up outers).
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static UWidget* GetUserWidgetRoot(UWidget* Widget);


	/**
	 * Collect every UTextBlock in one subtree with its text and current visual state.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static TArray<FEditorAutomationWidgetTextState> GetWidgetTextActiveStates(UWidget* Widget);
};
