// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EditorAutomationGraphLibrary.generated.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/** One graph of a Blueprint asset. */
USTRUCT(BlueprintType)
struct FEditorAutomationBlueprintGraphInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString GraphName;
	UPROPERTY(BlueprintReadOnly) FString GraphClassPath;
	UPROPERTY(BlueprintReadOnly) FString GraphObjectPath;
};

/** One node of an editor graph. */
USTRUCT(BlueprintType)
struct FEditorAutomationGraphNodeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString NodeObjectPath;
	UPROPERTY(BlueprintReadOnly) FString NodeClassPath;
	UPROPERTY(BlueprintReadOnly) FString NodeTitle;
	UPROPERTY(BlueprintReadOnly) int32 NodePosX = 0;
	UPROPERTY(BlueprintReadOnly) int32 NodePosY = 0;
};

/** One pin of an editor graph node. */
USTRUCT(BlueprintType)
struct FEditorAutomationGraphPinInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString PinId;
	UPROPERTY(BlueprintReadOnly) FString PinName;
	UPROPERTY(BlueprintReadOnly) FString Direction;
	UPROPERTY(BlueprintReadOnly) FString PinType;
	UPROPERTY(BlueprintReadOnly) FString DefaultValue;
	UPROPERTY(BlueprintReadOnly) bool bLinked = false;
	UPROPERTY(BlueprintReadOnly) TArray<FString> LinkedPinIds;
};

UCLASS()
class EDITORAUTOMATION_API UEditorAutomationGraphLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static TArray<FEditorAutomationBlueprintGraphInfo> GetBlueprintGraphs(const TSoftObjectPtr<UBlueprint>& Blueprint);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static TArray<FEditorAutomationGraphNodeInfo> GetGraphNodes(UEdGraph* Graph);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static TArray<FEditorAutomationGraphPinInfo> GetNodePins(UEdGraphNode* Node);

	/** Add a function-call node; returns the new node's object path, or empty (logged) on failure. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FString AddCallFunctionNode(
		UEdGraph* Graph,
		const FString& FunctionOwnerClassPath,
		const FString& FunctionName,
		int32 NodePosX,
		int32 NodePosY);

	/** Add an event node; returns the new node's object path, or empty (logged) on failure. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FString AddEventNode(
		UEdGraph* Graph,
		const FString& EventName,
		int32 NodePosX,
		int32 NodePosY);

	/**
	 * Add a component-bound event node (for example a widget Button's OnClicked) to a graph.
	 *
	 * WidgetName must name a component/widget variable of the owning Blueprint class (widgets need
	 * bIsVariable=true and a compile before the variable exists). The delegate owner class (for example
	 * UButton) is derived from that variable's object property.
	 *
	 * Returns the new node's object path, or empty (logged) on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FString AddComponentBoundEventNode(
		UEdGraph* Graph,
		const FString& WidgetName,
		const FString& DelegateName);

	/**
	 * Add a Branch (if/then/else) node to a graph.
	 *
	 * Pins: condition (bool input), execute (exec input), true / false (exec outputs).
	 * Returns the new node's object path, or empty (logged) on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FString AddBranchNode(
		UEdGraph* Graph,
		int32 NodePosX,
		int32 NodePosY);

	/** Add a variable-get node; returns the new node's object path, or empty (logged) on failure. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FString AddVariableGetNode(
		UEdGraph* Graph,
		const FString& VariableName);

	/** Add a variable-set node; returns the new node's object path, or empty (logged) on failure. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FString AddVariableSetNode(
		UEdGraph* Graph,
		const FString& VariableName);

	/**
	 * Add one Blueprint member variable.
	 *
	 * TypeString uses the Type grammar's primitive kinds ("double", "float", "int32", "int64", "bool",
	 * "string", "name", "byte"). Call this before adding variable get/set nodes, then compile the Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static bool AddBlueprintVariable(
		const TSoftObjectPtr<UBlueprint>& Blueprint,
		const FString& VariableName,
		const FString& TypeString);

	/** Connect two pins; returns false (logged) when a pin is missing or the schema rejects the connection. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static bool ConnectPins(
		UEdGraphNode* FromNode,
		const FString& FromPinName,
		UEdGraphNode* ToNode,
		const FString& ToPinName);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static bool SetPinDefaultValue(
		UEdGraphNode* Node,
		const FString& PinName,
		const FString& DefaultValue);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static bool SetNodePosition(UEdGraphNode* Node, int32 NodePosX, int32 NodePosY);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static bool SetPinDefaultObject(UEdGraphNode* Node, const FString& PinName, const FString& ObjectPath);

	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static bool RemoveGraphNode(UEdGraphNode* Node);

	/** Remove every node of one class from a graph; returns the number of removed nodes (logged on failure). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static int32 RemoveGraphNodesByClass(UEdGraph* Graph, const FString& NodeClassPath);
};
