// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
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
};
