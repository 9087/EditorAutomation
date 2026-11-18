// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "EditorAutomationCoreLibrary.h"

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
	/** Member/variable/event/function name the node refers to (empty when not applicable). */
	UPROPERTY(BlueprintReadOnly) FString MemberName;
	/** Graph this node belongs to (useful when the listing recurses into sub-graphs). */
	UPROPERTY(BlueprintReadOnly) FString GraphObjectPath;
	/** Owning class of the referenced function/variable (empty when unknown). */
	UPROPERTY(BlueprintReadOnly) FString TargetClassPath;
	/** Delegate property name for component-bound events (OnClicked etc.). */
	UPROPERTY(BlueprintReadOnly) FString DelegateName;
	/** Sub-graph (macro instance / collapsed composite) object path. */
	UPROPERTY(BlueprintReadOnly) FString SubGraphObjectPath;
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
	/** Pin category, e.g. "exec", "bool", "object". */
	UPROPERTY(BlueprintReadOnly) FString Category;
	/** Concrete Type name, e.g. "FVector" or an object path; empty for plain categories. */
	UPROPERTY(BlueprintReadOnly) FString TypeName;
	/** Default object path, when the pin default is an object reference. */
	UPROPERTY(BlueprintReadOnly) FString DefaultObjectPath;
	/** Linked pins as "NodeId::PinName" strings. */
	UPROPERTY(BlueprintReadOnly) TArray<FString> Connections;
	UPROPERTY(BlueprintReadOnly) TArray<FString> LinkedPinIds;
};

UCLASS()
class EDITORAUTOMATION_API UEditorAutomationGraphLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** List the graphs of a Blueprint (event graphs, function graphs, macros). */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult GetBlueprintGraphs(
		const TSoftObjectPtr<UBlueprint>& Blueprint,
		TArray<FEditorAutomationBlueprintGraphInfo>& OutGraphs);

	/** List the nodes of one graph; bRecursive also walks macro and collapsed sub-graphs. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult GetGraphNodes(
		UEdGraph* Graph,
		bool bRecursive,
		TArray<FEditorAutomationGraphNodeInfo>& OutNodes);

	/** List the pins of one node. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult GetNodePins(
		UEdGraphNode* Node,
		TArray<FEditorAutomationGraphPinInfo>& OutPins);

	/** Add a function-call node; returns the new node path through OutNodeObjectPath. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult AddCallFunctionNode(
		UEdGraph* Graph,
		const FString& FunctionOwnerClassPath,
		const FString& FunctionName,
		int32 NodePosX,
		int32 NodePosY,
		FString& OutNodeObjectPath);

	/** Add an override-event node; returns the new node path through OutNodeObjectPath. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult AddEventNode(
		UEdGraph* Graph,
		const FString& EventName,
		int32 NodePosX,
		int32 NodePosY,
		FString& OutNodeObjectPath);

	/**
	 * Add a component-bound event node (for example a widget Button's OnClicked) to a graph.
	 *
	 * WidgetName must name a component/widget variable of the owning Blueprint class (widgets need
	 * bIsVariable=true and a compile before the variable exists). The delegate owner class (for example
	 * UButton) is derived from that variable's object property.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult AddComponentBoundEventNode(
		UEdGraph* Graph,
		const FString& WidgetName,
		const FString& DelegateName,
		FString& OutNodeObjectPath);

	/** Add a Branch (if/then/else) node; pins: condition, execute, true, false. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult AddBranchNode(
		UEdGraph* Graph,
		int32 NodePosX,
		int32 NodePosY,
		FString& OutNodeObjectPath);

	/** Add a variable-get node for one Blueprint member. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult AddVariableGetNode(
		UEdGraph* Graph,
		const FString& VariableName,
		FString& OutNodeObjectPath);

	/** Add a variable-set node for one Blueprint member. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult AddVariableSetNode(
		UEdGraph* Graph,
		const FString& VariableName,
		FString& OutNodeObjectPath);

	/**
	 * Add one Blueprint member variable.
	 *
	 * TypeString uses the Type grammar's primitive kinds ("double", "float", "int32", "int64", "bool",
	 * "string", "name", "byte"). Call this before adding variable get/set nodes, then compile the Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult AddBlueprintVariable(
		const TSoftObjectPtr<UBlueprint>& Blueprint,
		const FString& VariableName,
		const FString& TypeString);

	/** Connect two pins. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult ConnectPins(
		UEdGraphNode* FromNode,
		const FString& FromPinName,
		UEdGraphNode* ToNode,
		const FString& ToPinName);

	/** Set one pin's default value text. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult SetPinDefaultValue(
		UEdGraphNode* Node,
		const FString& PinName,
		const FString& DefaultValue);

	/** Move one node. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult SetNodePosition(UEdGraphNode* Node, int32 NodePosX, int32 NodePosY);

	/** Set one pin's default object reference. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult SetPinDefaultObject(UEdGraphNode* Node, const FString& PinName, const FString& ObjectPath);

	/** Remove one graph node. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult RemoveGraphNode(UEdGraphNode* Node);

	/** Remove every node of one class from a graph; returns the count through OutRemovedCount. */
	UFUNCTION(BlueprintCallable, Category = "EditorAutomationBlueprintGraph")
	static FEditorAutomationResult RemoveGraphNodesByClass(
		UEdGraph* Graph,
		const FString& NodeClassPath,
		int32& OutRemovedCount);
};
