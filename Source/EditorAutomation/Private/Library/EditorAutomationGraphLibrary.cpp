// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationGraphLibrary.h"

#include "EditorAutomationLog.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace
{
	template <typename ResultT>
	ResultT GraphFailure(const FString& Message)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("%s"), *Message);
		return ResultT{};
	}


	UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
	{
		if (Node == nullptr) return nullptr;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin != nullptr && Pin->Direction == Direction && Pin->PinName.ToString() == PinName) return Pin;
		}
		return nullptr;
	}

	/** Find a member property of a Blueprint class, including widget variables that only exist after a compile. */
	FProperty* FindBlueprintMemberProperty(UBlueprint* Blueprint, const FName& PropertyName)
	{
		if (Blueprint == nullptr) return nullptr;
		if (UClass* SkeletonClass = Blueprint->SkeletonGeneratedClass)
		{
			if (FProperty* Property = FindFProperty<FProperty>(SkeletonClass, PropertyName)) return Property;
		}
		if (UClass* GeneratedClass = Blueprint->GeneratedClass)
		{
			if (FProperty* Property = FindFProperty<FProperty>(GeneratedClass, PropertyName)) return Property;
		}
		return nullptr;
	}

	/** Build a variable pin type for the primitive Type-grammar kinds AddBlueprintVariable supports. */
	bool MakePinTypeFromTypeString(const FString& TypeString, FEdGraphPinType& OutPinType)
	{
		FString ElementType = TypeString;
		if (TypeString.StartsWith(TEXT("array("), ESearchCase::IgnoreCase) && TypeString.EndsWith(TEXT(")")))
		{
			ElementType = TypeString.Mid(6, TypeString.Len() - 7).TrimStartAndEnd();
			OutPinType.ContainerType = EPinContainerType::Array;
		}
		else if (TypeString.EndsWith(TEXT("[]")))
		{
			ElementType = TypeString.LeftChop(2).TrimStartAndEnd();
			OutPinType.ContainerType = EPinContainerType::Array;
		}

		if (ElementType.Equals(TEXT("double"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
		else if (ElementType.Equals(TEXT("float"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		}
		else if (ElementType.Equals(TEXT("int32"), ESearchCase::IgnoreCase) || ElementType.Equals(TEXT("int"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (ElementType.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		}
		else if (ElementType.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (ElementType.Equals(TEXT("string"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		else if (ElementType.Equals(TEXT("name"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		}
		else if (ElementType.Equals(TEXT("byte"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		}
		else
		{
			return false;
		}
		return true;
	}

	/** Text form of a variable's default value, matching the Type-grammar kind. */
	FString DefaultValueForTypeString(const FString& TypeString)
	{
		if (TypeString.Equals(TEXT("bool"), ESearchCase::IgnoreCase)) return TEXT("false");
		if (TypeString.Equals(TEXT("string"), ESearchCase::IgnoreCase) || TypeString.Equals(TEXT("name"), ESearchCase::IgnoreCase)) return FString();
		if (TypeString.StartsWith(TEXT("array("), ESearchCase::IgnoreCase) || TypeString.EndsWith(TEXT("[]"))) return FString();
		return TEXT("0");
	}

	/** Add one Blueprint member variable; the caller reports OutError. */
	bool AddBlueprintVariableInternal(
		UBlueprint* Blueprint,
		const FString& VariableName,
		const FString& TypeString,
		FString& OutError)
	{
		if (Blueprint == nullptr)
		{
			OutError = TEXT("Blueprint is null.");
			return false;
		}
		const FName Name(*VariableName);
		if (Name.IsNone())
		{
			OutError = TEXT("Variable name is empty.");
			return false;
		}
		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name) != INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("Variable '%s' already exists on '%s'."), *VariableName, *Blueprint->GetName());
			return false;
		}
		FEdGraphPinType PinType;
		if (!MakePinTypeFromTypeString(TypeString, PinType))
		{
			OutError = FString::Printf(TEXT("Unsupported variable type '%s'."), *TypeString);
			return false;
		}
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, PinType, DefaultValueForTypeString(TypeString)))
		{
			OutError = FString::Printf(TEXT("Could not add variable '%s'."), *VariableName);
			return false;
		}
		return true;
	}

	/** Create the right call-function node class for one UFunction (array functions use the wildcard-aware node). */
	UK2Node_CallFunction* CreateCallFunctionNode(UEdGraph* Graph, UFunction* Function, UClass* OwnerClass, int32 NodePosX, int32 NodePosY)
	{
		if (Function != nullptr && Function->HasMetaData(FBlueprintMetadata::MD_ArrayParam))
		{
			FGraphNodeCreator<UK2Node_CallArrayFunction> Creator(*Graph);
			UK2Node_CallArrayFunction* ArrayNode = Creator.CreateNode();
			ArrayNode->FunctionReference.SetExternalMember(Function->GetFName(), OwnerClass);
			ArrayNode->NodePosX = NodePosX;
			ArrayNode->NodePosY = NodePosY;
			Creator.Finalize();
			return ArrayNode;
		}
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* CallNode = Creator.CreateNode();
		CallNode->FunctionReference.SetExternalMember(Function != nullptr ? Function->GetFName() : NAME_None, OwnerClass);
		CallNode->NodePosX = NodePosX;
		CallNode->NodePosY = NodePosY;
		Creator.Finalize();
		return CallNode;
	}

	/** Create a variable get or set node for a Blueprint member (including widget variables). */
	UEdGraphNode* CreateVariableNode(UEdGraph* Graph, const FName& VariableName, const bool bSet, FString& OutError)
	{
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
		FProperty* VariableProperty = FindBlueprintMemberProperty(Blueprint, VariableName);
		const bool bIsBlueprintVariable =
			Blueprint != nullptr && FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName) != INDEX_NONE;
		if (VariableProperty == nullptr && !bIsBlueprintVariable)
		{
			OutError = FString::Printf(TEXT("Variable '%s' was not found on the Blueprint."), *VariableName.ToString());
			return nullptr;
		}
		UClass* OwnerClass = Blueprint != nullptr
			? (Blueprint->SkeletonGeneratedClass != nullptr ? Blueprint->SkeletonGeneratedClass : Blueprint->GeneratedClass)
			: nullptr;
		if (bSet)
		{
			FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
			UK2Node_VariableSet* SetNode = Creator.CreateNode();
			if (VariableProperty != nullptr) SetNode->SetFromProperty(VariableProperty, /*bSelfContext=*/true, OwnerClass);
			else SetNode->VariableReference.SetSelfMember(VariableName);
			Creator.Finalize();
			return SetNode;
		}
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
		UK2Node_VariableGet* GetNode = Creator.CreateNode();
		if (VariableProperty != nullptr) GetNode->SetFromProperty(VariableProperty, /*bSelfContext=*/true, OwnerClass);
		else GetNode->VariableReference.SetSelfMember(VariableName);
		Creator.Finalize();
		return GetNode;
	}
}

TArray<FEditorAutomationBlueprintGraphInfo> UEditorAutomationGraphLibrary::GetBlueprintGraphs(const TSoftObjectPtr<UBlueprint>& BlueprintRef)
{
	TArray<FEditorAutomationBlueprintGraphInfo> Graphs;
	UBlueprint* Blueprint = BlueprintRef.LoadSynchronous();
	if (Blueprint == nullptr) return Graphs;
	const auto AddGraph = [&Graphs](UEdGraph* Graph)
	{
		if (Graph == nullptr) return;
		FEditorAutomationBlueprintGraphInfo Info;
		Info.GraphName = Graph->GetName();
		Info.GraphClassPath = Graph->GetClass()->GetPathName();
		Info.GraphObjectPath = Graph->GetPathName();
		Graphs.Add(Info);
	};
	for (UEdGraph* Graph : Blueprint->UbergraphPages) AddGraph(Graph);
	for (UEdGraph* Graph : Blueprint->FunctionGraphs) AddGraph(Graph);
	for (UEdGraph* Graph : Blueprint->MacroGraphs) AddGraph(Graph);
	return Graphs;
}

TArray<FEditorAutomationGraphNodeInfo> UEditorAutomationGraphLibrary::GetGraphNodes(UEdGraph* Graph)
{
	TArray<FEditorAutomationGraphNodeInfo> Nodes;
	if (Graph == nullptr) return Nodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node == nullptr) continue;
		FEditorAutomationGraphNodeInfo Info;
		Info.NodeObjectPath = Node->GetPathName();
		Info.NodeClassPath = Node->GetClass()->GetPathName();
		Info.NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		Info.NodePosX = Node->NodePosX;
		Info.NodePosY = Node->NodePosY;
		Nodes.Add(Info);
	}
	return Nodes;
}

TArray<FEditorAutomationGraphPinInfo> UEditorAutomationGraphLibrary::GetNodePins(UEdGraphNode* Node)
{
	TArray<FEditorAutomationGraphPinInfo> Pins;
	if (Node == nullptr) return Pins;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin == nullptr) continue;
		FEditorAutomationGraphPinInfo Info;
		Info.PinId = Pin->PinId.ToString();
		Info.PinName = Pin->PinName.ToString();
		Info.Direction = Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output");
		Info.PinType = UEdGraphSchema_K2::TypeToText(Pin->PinType).ToString();
		Info.DefaultValue = Pin->DefaultValue;
		Info.bLinked = Pin->LinkedTo.Num() > 0;
		for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (LinkedPin != nullptr) Info.LinkedPinIds.Add(LinkedPin->PinId.ToString());
		}
		Pins.Add(Info);
	}
	return Pins;
}

FString UEditorAutomationGraphLibrary::AddCallFunctionNode(
	UEdGraph* Graph,
	const FString& FunctionOwnerClassPath,
	const FString& FunctionName,
	int32 NodePosX,
	int32 NodePosY)
{
	if (Graph == nullptr || FunctionName.IsEmpty())
	{
		return GraphFailure<FString>(TEXT("Graph and FunctionName are required."));
	}
	UClass* OwnerClass = FunctionOwnerClassPath.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *FunctionOwnerClassPath);
	if (OwnerClass == nullptr)
	{
		return GraphFailure<FString>(TEXT("Owner class was not found."));
	}
	UFunction* Function = OwnerClass->FindFunctionByName(FName(*FunctionName));
	if (Function == nullptr)
	{
		return GraphFailure<FString>(FString::Printf(TEXT("Function '%s' was not found on '%s'."), *FunctionName, *OwnerClass->GetName()));
	}
	FScopedTransaction Transaction(FText::FromString(TEXT("EditorAutomationAddCallFunctionNode")));
	Graph->Modify();
	UK2Node_CallFunction* Node = CreateCallFunctionNode(Graph, Function, OwnerClass, NodePosX, NodePosY);
	FBlueprintEditorUtils::MarkBlueprintAsModified(FBlueprintEditorUtils::FindBlueprintForGraphChecked(Graph));
	return Node != nullptr ? Node->GetPathName() : FString();
}

FString UEditorAutomationGraphLibrary::AddEventNode(
	UEdGraph* Graph,
	const FString& EventName,
	int32 NodePosX,
	int32 NodePosY)
{
	if (Graph == nullptr || EventName.IsEmpty())
	{
		return GraphFailure<FString>(TEXT("Graph and EventName are required."));
	}
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint == nullptr || Blueprint->ParentClass == nullptr)
	{
		return GraphFailure<FString>(TEXT("Graph does not belong to a Blueprint."));
	}
	UFunction* Function = Blueprint->ParentClass->FindFunctionByName(FName(*EventName));
	if (Function == nullptr)
	{
		return GraphFailure<FString>(FString::Printf(TEXT("Event function '%s' was not found."), *EventName));
	}
	FScopedTransaction Transaction(FText::FromString(TEXT("EditorAutomationAddEventNode")));
	Graph->Modify();
	FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
	UK2Node_Event* Node = Creator.CreateNode();
	Node->EventReference.SetExternalMember(FName(*EventName), Blueprint->ParentClass);
	Node->bOverrideFunction = true;
	Node->NodePosX = NodePosX;
	Node->NodePosY = NodePosY;
	Creator.Finalize();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return Node->GetPathName();
}

FString UEditorAutomationGraphLibrary::AddComponentBoundEventNode(
	UEdGraph* Graph,
	const FString& WidgetName,
	const FString& DelegateName)
{
	if (Graph == nullptr || WidgetName.IsEmpty() || DelegateName.IsEmpty())
	{
		return GraphFailure<FString>(TEXT("Graph, WidgetName and DelegateName are required."));
	}
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint == nullptr)
	{
		return GraphFailure<FString>(TEXT("Graph does not belong to a Blueprint."));
	}

	// Widgets/components become Blueprint variables (object properties) only when bIsVariable is set and the
	// Blueprint has been compiled, which is why the property is looked up on the skeleton/generated class.
	FObjectProperty* ComponentProperty = CastField<FObjectProperty>(FindBlueprintMemberProperty(Blueprint, FName(*WidgetName)));
	if (ComponentProperty == nullptr)
	{
		return GraphFailure<FString>(FString::Printf(
				TEXT("Component/widget variable '%s' was not found on Blueprint '%s'. Set bIsVariable=true on the widget and compile the Blueprint first."),
				*WidgetName,
				*Blueprint->GetName()));
	}
	UClass* DelegateOwnerClass = ComponentProperty->PropertyClass != nullptr
		? ComponentProperty->PropertyClass->GetAuthoritativeClass()
		: nullptr;
	if (DelegateOwnerClass == nullptr)
	{
		return GraphFailure<FString>(FString::Printf(TEXT("Component/widget variable '%s' has no object class."), *WidgetName));
	}
	FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(DelegateOwnerClass, FName(*DelegateName));
	if (DelegateProperty == nullptr)
	{
		return GraphFailure<FString>(FString::Printf(TEXT("Delegate '%s' was not found on class '%s'."), *DelegateName, *DelegateOwnerClass->GetName()));
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("EditorAutomationAddComponentBoundEventNode")));
	Graph->Modify();
	Blueprint->Modify();
	UK2Node_ComponentBoundEvent* Node = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_ComponentBoundEvent>(
		Graph,
		Graph->GetGoodPlaceForNewNode(),
		EK2NewNodeFlags::None,
		[ComponentProperty, DelegateProperty](UK2Node_ComponentBoundEvent* NewNode)
		{
			NewNode->InitializeComponentBoundEventParams(ComponentProperty, DelegateProperty);
		});
	if (Node == nullptr)
	{
		return GraphFailure<FString>(TEXT("Could not create the component bound event node."));
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return Node->GetPathName();
}

FString UEditorAutomationGraphLibrary::AddBranchNode(
	UEdGraph* Graph,
	int32 NodePosX,
	int32 NodePosY)
{
	if (Graph == nullptr)
	{
		return GraphFailure<FString>(TEXT("Graph is required."));
	}
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint == nullptr)
	{
		return GraphFailure<FString>(TEXT("Graph does not belong to a Blueprint."));
	}
	FScopedTransaction Transaction(FText::FromString(TEXT("EditorAutomationAddBranchNode")));
	Graph->Modify();
	FGraphNodeCreator<UK2Node_IfThenElse> Creator(*Graph);
	UK2Node_IfThenElse* Node = Creator.CreateNode();
	Node->NodePosX = NodePosX;
	Node->NodePosY = NodePosY;
	Creator.Finalize();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return Node->GetPathName();
}

FString UEditorAutomationGraphLibrary::AddVariableGetNode(
	UEdGraph* Graph,
	const FString& VariableName)
{
	if (Graph == nullptr || VariableName.IsEmpty())
	{
		return GraphFailure<FString>(TEXT("Graph and VariableName are required."));
	}
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint == nullptr)
	{
		return GraphFailure<FString>(TEXT("Graph does not belong to a Blueprint."));
	}
	const FName VariableFName(*VariableName);
	FProperty* VariableProperty = FindBlueprintMemberProperty(Blueprint, VariableFName);
	bool bIsBlueprintVariable = false;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == VariableFName)
		{
			bIsBlueprintVariable = true;
			break;
		}
	}
	if (VariableProperty == nullptr && !bIsBlueprintVariable)
	{
		return GraphFailure<FString>(FString::Printf(TEXT("Variable '%s' was not found on Blueprint '%s'."), *VariableName, *Blueprint->GetName()));
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("EditorAutomationAddVariableGetNode")));
	Graph->Modify();
	FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
	UK2Node_VariableGet* Node = Creator.CreateNode();
	if (VariableProperty != nullptr)
	{
		UClass* OwnerClass = Blueprint->SkeletonGeneratedClass != nullptr
			? Blueprint->SkeletonGeneratedClass
			: Blueprint->GeneratedClass;
		Node->SetFromProperty(VariableProperty, /*bSelfContext=*/true, OwnerClass);
	}
	else
	{
		Node->VariableReference.SetSelfMember(VariableFName);
	}
	const FVector2D Place = Graph->GetGoodPlaceForNewNode();
	Node->NodePosX = FMath::RoundToInt(Place.X);
	Node->NodePosY = FMath::RoundToInt(Place.Y);
	Creator.Finalize();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return Node->GetPathName();
}

FString UEditorAutomationGraphLibrary::AddVariableSetNode(
	UEdGraph* Graph,
	const FString& VariableName)
{
	if (Graph == nullptr || VariableName.IsEmpty())
	{
		return GraphFailure<FString>(TEXT("Graph and VariableName are required."));
	}
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint == nullptr)
	{
		return GraphFailure<FString>(TEXT("Graph does not belong to a Blueprint."));
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("EditorAutomationAddVariableSetNode")));
	Graph->Modify();
	Blueprint->Modify();
	FString CreateError;
	UEdGraphNode* Node = CreateVariableNode(Graph, FName(*VariableName), /*bSet=*/true, CreateError);
	if (Node == nullptr)
	{
		return GraphFailure<FString>(CreateError);
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return Node->GetPathName();
}

bool UEditorAutomationGraphLibrary::AddBlueprintVariable(
	const TSoftObjectPtr<UBlueprint>& BlueprintRef,
	const FString& VariableName,
	const FString& TypeString)
{
	UBlueprint* Blueprint = BlueprintRef.IsNull() ? nullptr : BlueprintRef.LoadSynchronous();
	if (Blueprint == nullptr)
	{
		return GraphFailure<bool>(TEXT("Blueprint could not be loaded."));
	}
	FScopedTransaction Transaction(FText::FromString(TEXT("EditorAutomationAddBlueprintVariable")));
	Blueprint->Modify();
	FString AddError;
	if (!AddBlueprintVariableInternal(Blueprint, VariableName, TypeString, AddError))
	{
		return GraphFailure<bool>(AddError);
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UEditorAutomationGraphLibrary::ConnectPins(
	UEdGraphNode* FromNode,
	const FString& FromPinName,
	UEdGraphNode* ToNode,
	const FString& ToPinName)
{
	UEdGraphPin* OutputPin = FindPin(FromNode, FromPinName, EGPD_Output);
	UEdGraphPin* InputPin = FindPin(ToNode, ToPinName, EGPD_Input);
	if (OutputPin == nullptr || InputPin == nullptr)
	{
		return GraphFailure<bool>(TEXT("Output or input pin was not found."));
	}
	if (!FromNode->GetGraph()->GetSchema()->TryCreateConnection(OutputPin, InputPin))
	{
		return GraphFailure<bool>(TEXT("Schema rejected the connection."));
	}
	return true;
}

bool UEditorAutomationGraphLibrary::SetPinDefaultValue(
	UEdGraphNode* Node,
	const FString& PinName,
	const FString& DefaultValue)
{
	UEdGraphPin* Pin = FindPin(Node, PinName, EGPD_Input);
	if (Pin == nullptr)
	{
		return GraphFailure<bool>(TEXT("Input pin was not found."));
	}
	Pin->DefaultValue = DefaultValue;
	return true;
}

bool UEditorAutomationGraphLibrary::SetNodePosition(UEdGraphNode* Node, int32 NodePosX, int32 NodePosY)
{
	if (Node == nullptr) return GraphFailure<bool>(TEXT("Node is null."));
	Node->NodePosX = NodePosX;
	Node->NodePosY = NodePosY;
	return true;
}

bool UEditorAutomationGraphLibrary::SetPinDefaultObject(
	UEdGraphNode* Node,
	const FString& PinName,
	const FString& ObjectPath)
{
	UEdGraphPin* Pin = FindPin(Node, PinName, EGPD_Input);
	if (Pin == nullptr)
	{
		return GraphFailure<bool>(TEXT("Input pin was not found."));
	}
	UObject* Object = FindObject<UObject>(nullptr, *ObjectPath);
	if (Object == nullptr)
	{
		Object = LoadObject<UObject>(nullptr, *ObjectPath);
	}
	if (Object == nullptr)
	{
		return GraphFailure<bool>(TEXT("Default object was not found."));
	}
	Pin->DefaultObject = Object;
	return true;
}

bool UEditorAutomationGraphLibrary::RemoveGraphNode(UEdGraphNode* Node)
{
	if (Node == nullptr)
	{
		return GraphFailure<bool>(TEXT("Node is null."));
	}
	UEdGraph* Graph = Node->GetGraph();
	UBlueprint* Blueprint = Graph != nullptr ? FBlueprintEditorUtils::FindBlueprintForGraph(Graph) : nullptr;
	if (Graph == nullptr || Blueprint == nullptr)
	{
		return GraphFailure<bool>(TEXT("Node does not belong to a Blueprint graph."));
	}
	FScopedTransaction Transaction(FText::FromString(TEXT("EditorAutomationRemoveGraphNode")));
	Blueprint->Modify();
	Graph->Modify();
	Graph->RemoveNode(Node);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

int32 UEditorAutomationGraphLibrary::RemoveGraphNodesByClass(
	UEdGraph* Graph,
	const FString& NodeClassPath)
{
	if (Graph == nullptr || NodeClassPath.IsEmpty())
	{
		return GraphFailure<int32>(TEXT("Graph and NodeClassPath are required."));
	}
	UClass* NodeClass = LoadObject<UClass>(nullptr, *NodeClassPath);
	if (NodeClass == nullptr || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
	{
		return GraphFailure<int32>(FString::Printf(TEXT("'%s' is not a graph node class."), *NodeClassPath));
	}
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);

	TArray<UEdGraphNode*> NodesToRemove;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node != nullptr && Node->IsA(NodeClass))
		{
			NodesToRemove.Add(Node);
		}
	}
	if (NodesToRemove.IsEmpty())
	{
		return 0;
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("EditorAutomationRemoveGraphNodesByClass")));
	if (Blueprint != nullptr) Blueprint->Modify();
	Graph->Modify();
	for (UEdGraphNode* Node : NodesToRemove)
	{
		Graph->RemoveNode(Node);
	}
	if (Blueprint != nullptr)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
	return NodesToRemove.Num();
}
