// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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

	/** False when the item has no row widget yet (virtualized out of view) or the input is invalid. */
	UPROPERTY(BlueprintReadOnly) bool bSuccess = true;
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

	/** False when the widget is not a UListView. */
	UPROPERTY(BlueprintReadOnly) bool bSuccess = true;
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
	static TArray<FString> GetWidgetRoots();

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FString GetWidgetBlueprintPath(UWidget* Widget);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FString GetWidgetClassPath(UWidget* Widget);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationWidgetTreeDiff GetWidgetTreeDiff(UUserWidget* RootWidget);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static void ResetWidgetTreeCache(UUserWidget* RootWidget);

	/** Resolved widget geometry; an empty/zero struct means the widget is missing or not laid out yet (logged). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationWidgetGeometry GetWidgetGeometry(UWidget* Widget);

	/**
	 * Read the items held by a UMG UListView.
	 *
	 * @param Widget   The list widget; must be (or derive from) UListView.
	 * @param MaxItems Keep at most this many items; 0 means all.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationListViewInfo GetListViewInfo(UWidget* Widget, int32 MaxItems = 0);

	/**
	 * Read the row widget of one list item, when that item is on screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationListEntryInfo GetListEntryInfo(UWidget* Widget, int32 Index);

	/**
	 * Scroll a list so the requested item's row exists, then return that row.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FEditorAutomationListEntryInfo ScrollToListEntry(UWidget* Widget, int32 Index);

	/**
	 * Scroll a list to one item and click its row.
	 *
	 * A row created by this call has not been drawn yet, so the click is not
	 * dispatched in the same call; the function logs that and returns false —
	 * call it again to click the now-drawn row.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static bool ScrollToListEntryAndSendClick(UWidget* Widget, int32 Index, const FEditorAutomationSendClickOptions& Options);

	/**
	 * Render one widget off-screen into a PNG file (UI included, no viewport needed).
	 *
	 * @param Widget     The widget to render.
	 * @param OutputPath Absolute PNG path; empty generates <Project>/Saved/Screenshots/EditorAutomation_<stamp>.png.
	 * @param Width      Render width; 0 uses the widget's current/desired width.
	 * @param Height     Render height; 0 uses the widget's current/desired height.
	 * @returns the written PNG path, or an empty string on failure (the reason is logged).
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationUMG")
	static FString CaptureWidgetScreenshot(UWidget* Widget, const FString& OutputPath = TEXT(""), int32 Width = 0, int32 Height = 0);
};
