// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EditorAutomationCoreLibrary.h"
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
	static FEditorAutomationResult OpenWidgetGraph(const FString& WidgetBlueprintPath);

	/** Create a Widget Blueprint asset; OutWidgetBlueprintPath receives its object path. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult CreateWidgetBlueprint(
		const FString& AssetPath,
		const FString& ParentClassPath,
		FString& OutWidgetBlueprintPath);

	/** Add the first root widget; OutWidgetPath receives the new widget's object path. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult AddRootWidget(
		const FString& WidgetBlueprintPath,
		const FString& WidgetClassPath,
		FName WidgetName,
		FString& OutWidgetPath);

	/** Read the design-time widget tree of one Widget Blueprint (root first, depth-first). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult GetWidgetTree(const FString& WidgetBlueprintPath, TArray<UWidget*>& OutWidgets);

	/** Parent widget; null for a root widget (that is data, not an error). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult GetWidgetParent(UWidget* Widget, UWidget*& OutParent);

	/** Child widgets (NamedSlot content included). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult GetWidgetChildren(UWidget* Widget, TArray<UWidget*>& OutChildren);

	/** Slot widget; null for a root widget (that is data, not an error). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult GetWidgetSlot(UWidget* Widget, UPanelSlot*& OutSlot);

	/** Add a widget under a panel/content parent; OutWidgetPath receives the new widget's object path. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult AddWidget(
		UWidget* Parent,
		const FString& WidgetClassPath,
		FName WidgetName,
		int32 ChildIndex,
		FString& OutWidgetPath);

	/** Remove a widget. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult RemoveWidget(UWidget* Widget);

	/** Rename a widget; OutWidgetPath receives the new object path. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult RenameWidget(UWidget* Widget, FName NewName, FString& OutWidgetPath);

	/** Move a widget under another parent. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult ReparentWidget(UWidget* Widget, UWidget* NewParent, int32 ChildIndex);

	/** Write one widget property through the core property writer. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult SetWidgetProperty(
		UWidget* Widget,
		const FString& PropertyPath,
		const FString& ValueJson);

	/** Write one widget slot property through the core property writer. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult SetWidgetSlotProperty(
		UPanelSlot* Slot,
		const FString& PropertyPath,
		const FString& ValueJson);

	/** Whether a widget name is exposed as a variable of one Widget Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult IsWidgetVariable(
		UWidget* Widget,
		const TSoftObjectPtr<UBlueprint>& WidgetBlueprint,
		bool& OutIsVariable);

	/** The root widget of the user widget that owns this widget (design-time trees use the Widget Blueprint root). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult GetUserWidgetRoot(UWidget* Widget, UWidget*& OutRootWidget);

	/** Every UTextBlock in one subtree with its text and current visual state. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMGEditor")
	static FEditorAutomationResult GetWidgetTextActiveStates(
		UWidget* Widget,
		TArray<FEditorAutomationWidgetTextState>& OutStates);
};