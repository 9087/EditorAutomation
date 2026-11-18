// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorAutomationCoreLibrary.h"
#include "EditorAutomationInputLibrary.h"
#include "EditorAutomationUMGLibrary.generated.h"

class UUserWidget;
class UWidget;

UENUM(BlueprintType)
enum class EEditorAutomationWidgetDiffKind : uint8
{
	None UMETA(DisplayName = "None"),
	Add UMETA(DisplayName = "Add"),
	Change UMETA(DisplayName = "Change"),
	Delete UMETA(DisplayName = "Delete"),
};

/** One delegate property of a widget and whether it has runtime bindings. */
USTRUCT(BlueprintType)
struct FEditorAutomationDelegateInfo
{
	GENERATED_BODY()

	/** Property name (C++ name), e.g. "OnClicked". */
	UPROPERTY(BlueprintReadOnly) FString PropertyName;
	/** True when at least one function is bound at runtime. */
	UPROPERTY(BlueprintReadOnly) bool bHasBindings = false;
	/** C++ type name, e.g. "FOnPointerEvent". */
	UPROPERTY(BlueprintReadOnly) FString TypeName;
};

/** One data item held by a UMG UListView (the item object, not its row widget). */
USTRUCT(BlueprintType)
struct FEditorAutomationListItemInfo
{
	GENERATED_BODY()

	/** Item index in the list. */
	UPROPERTY(BlueprintReadOnly) int32 Index = 0;
	/** The item object held in ListItems. */
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UObject> Item = nullptr;
	/** The item object's class. */
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UClass> ItemClass = nullptr;
};

/** The row widget of one list item (only on-screen items have one). */
USTRUCT(BlueprintType)
struct FEditorAutomationListEntryInfo
{
	GENERATED_BODY()

	/** Item index this row belongs to. */
	UPROPERTY(BlueprintReadOnly) int32 Index = 0;
	/** The item's row widget, or null when no row exists yet. */
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UUserWidget> EntryWidget = nullptr;
	/** The row widget's actual class (may vary per item). */
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UClass> EntryWidgetClass = nullptr;
};

/** Items held by one UMG UListView. */
USTRUCT(BlueprintType)
struct FEditorAutomationListViewInfo
{
	GENERATED_BODY()

	/** Number of items in the list. */
	UPROPERTY(BlueprintReadOnly) int32 ItemCount = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationListItemInfo> Items;
};

/** One readable property row of a widget-tree diff node. */
USTRUCT(BlueprintType)
struct FEditorAutomationWidgetPropertyInfo
{
	GENERATED_BODY()

	/** Property name (C++ name). */
	UPROPERTY(BlueprintReadOnly) FString PropertyName;
	/** C++ type, e.g. "FText", "bool", "FLinearColor". */
	UPROPERTY(BlueprintReadOnly) FString Type;
	/** Value before the change; empty on the first read (Add). */
	UPROPERTY(BlueprintReadOnly) FString OldValue;
	/** Current value. */
	UPROPERTY(BlueprintReadOnly) FString NewValue;
};

USTRUCT(BlueprintType)
struct FEditorAutomationWidgetNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString ObjectPath;
	UPROPERTY(BlueprintReadOnly) FString TreePath;
	UPROPERTY(BlueprintReadOnly) FString ParentObjectPath;
	UPROPERTY(BlueprintReadOnly) FString ParentTreePath;
	UPROPERTY(BlueprintReadOnly) FString ClassPath;
	UPROPERTY(BlueprintReadOnly) EEditorAutomationWidgetDiffKind Kind = EEditorAutomationWidgetDiffKind::None;
	/** Changed readable properties: all of them on Add, only changed ones on Change. */
	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationWidgetPropertyInfo> Properties;
	/** Delegate properties and binding state (Add/Change nodes only). */
	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationDelegateInfo> Delegates;
};

USTRUCT(BlueprintType)
struct FEditorAutomationWidgetTreeDiff
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString RootObjectPath;
	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationWidgetNode> Nodes;
};

/** Resolved Slate geometry of one widget. */
USTRUCT(BlueprintType)
struct FEditorAutomationWidgetGeometry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FVector2D AbsolutePosition = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector2D AbsoluteSize = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector2D LocalSize = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float LayoutScale = 1.0f;
};

UCLASS()
class EDITORAUTOMATION_API UEditorAutomationUMGLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult GetWidgetRoots(TArray<FString>& OutRoots);

	/** Asset path of the WidgetBlueprint that owns this widget. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult GetWidgetBlueprintPath(UWidget* Widget, FString& OutBlueprintPath);

	/** Class path of this widget. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult GetWidgetClassPath(UWidget* Widget, FString& OutClassPath);

	/** Diff the live widget tree against the previous read (first read reports every node as Add). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult GetWidgetTreeDiff(UUserWidget* RootWidget, FEditorAutomationWidgetTreeDiff& OutDiff);

	/** Drop the cached widget-tree snapshot for one root. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult ResetWidgetTreeCache(UUserWidget* RootWidget);

	/** Resolved widget geometry. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult GetWidgetGeometry(UWidget* Widget, FEditorAutomationWidgetGeometry& OutGeometry);

	/** Read the items held by a UMG UListView. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult GetListViewInfo(UWidget* Widget, int32 MaxItems, FEditorAutomationListViewInfo& OutInfo);

	/** Read the row widget of one list item, when that item is on screen. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult GetListEntryInfo(UWidget* Widget, int32 Index, FEditorAutomationListEntryInfo& OutEntry);

	/** Scroll a list so the requested item's row exists, then return that row. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult ScrollToListEntry(UWidget* Widget, int32 Index, FEditorAutomationListEntryInfo& OutEntry);

	/**
	 * Scroll a list to one item and click its row.
	 *
	 * A row created by this call is not drawn yet, so the click is deferred: the call fails
	 * with NotReady and the next call clicks the now-drawn row.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult ScrollToListEntryAndSendClick(UWidget* Widget, int32 Index, const FEditorAutomationSendClickOptions& Options);

	/** Render one widget off-screen into a PNG file; OutFilePath receives the written path. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationResult CaptureWidgetScreenshot(
		UWidget* Widget,
		const FString& OutputPath,
		int32 Width,
		int32 Height,
		FString& OutFilePath);
};