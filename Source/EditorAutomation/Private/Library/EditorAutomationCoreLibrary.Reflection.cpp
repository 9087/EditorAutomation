// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationCoreLibrary.h"

#include "Reflection/EditorAutomationText.h"
#include "Reflection/EditorAutomationTypeTree.h"

#include "UObject/UnrealType.h"

namespace
{
	/** Append one property definition to a property list. */
	void AddPropertyInfo(TArray<FEditorAutomationPropertyInfo>& Properties, FProperty* Property)
	{
		if (Property == nullptr || Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			return;
		}

		FEditorAutomationPropertyInfo Info;
		Info.Name = Property->GetName();
		Info.Type = EditorAutomation::FTypeTree::MakeStringFromProperty(Property);
		Info.DisplayNameText = EditorAutomation::MakeTextRef(Property->GetDisplayNameText());
		Properties.Add(MoveTemp(Info));
	}

	/** Fill one function's signature, regardless of whether it is BlueprintCallable. */
	void BuildFunctionInfo(FEditorAutomationFunctionInfo& Info, UFunction* Function)
	{
		Info.Name = Function->GetName();
		Info.bBlueprintCallable = Function->HasAnyFunctionFlags(FUNC_BlueprintCallable);
		Info.bBlueprintPure = Function->HasAnyFunctionFlags(FUNC_BlueprintPure);
		Info.bStatic = Function->HasAnyFunctionFlags(FUNC_Static);
		Info.ToolTip = Function->GetToolTipText().ToString();

		for (TFieldIterator<FProperty> ParameterIt(Function); ParameterIt; ++ParameterIt)
		{
			FProperty* Parameter = *ParameterIt;
			if (Parameter == nullptr)
			{
				continue;
			}

			if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				Info.ReturnType = EditorAutomation::FTypeTree::MakeStringFromProperty(Parameter);
				continue;
			}

			FEditorAutomationParameterInfo ParameterInfo;
			ParameterInfo.Name = Parameter->GetName();
			ParameterInfo.Type = EditorAutomation::FTypeTree::MakeStringFromProperty(Parameter);
			ParameterInfo.DisplayNameText = EditorAutomation::MakeTextRef(Parameter->GetDisplayNameText());

			// UHT marks every non-const reference as OutParm; UPARAM(ref) additionally sets ReferenceParm,
			// while const references are ConstParm. That gives the three directions below.
			if (Parameter->HasAnyPropertyFlags(CPF_ConstParm))
			{
				ParameterInfo.Direction = static_cast<int32>(EEditorAutomationParameterDirection::Input);
			}
			else if (!Parameter->HasAnyPropertyFlags(CPF_OutParm))
			{
				ParameterInfo.Direction = static_cast<int32>(EEditorAutomationParameterDirection::Input);
			}
			else if (Parameter->HasAnyPropertyFlags(CPF_ReferenceParm))
			{
				ParameterInfo.Direction = static_cast<int32>(EEditorAutomationParameterDirection::InputOutput);
			}
			else
			{
				ParameterInfo.Direction = static_cast<int32>(EEditorAutomationParameterDirection::Output);
			}

			Info.Parameters.Add(MoveTemp(ParameterInfo));
		}
	}

	/** Append one BlueprintCallable function's info to the list. */
	void AddFunctionInfo(TArray<FEditorAutomationFunctionInfo>& Functions, UFunction* Function)
	{
		if (Function == nullptr || !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
		{
			return;
		}

		FEditorAutomationFunctionInfo Info;
		BuildFunctionInfo(Info, Function);
		Functions.Add(MoveTemp(Info));
	}
}

// ============================================================================
// Reflection
// ============================================================================

FEditorAutomationResult UEditorAutomationCoreLibrary::GetEnumInfo(const FString& Type, FEditorAutomationEnumInfo& OutInfo)
{
	if (Type.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Type is empty."),
			TEXT("Pass the enum Type, e.g. enum(/Script/Engine.ECollisionEnabled)."));
	}

	EditorAutomation::FTypeTree TypeTree;
	FString ParseError;
	if (!EditorAutomation::FTypeTree::TryParse(Type, TypeTree, ParseError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("Type '%s' is not valid: %s"), *Type, *ParseError),
			TEXT("Pass Type in canonical form: enum(/Script/Module.EnumName)."));
	}

	if (TypeTree.Kind != EditorAutomation::ETypeKind::Enum)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::WrongType,
			FString::Printf(TEXT("Expected enum(...) but got %s(...)."), *EditorAutomation::FTypeTree::KindName(TypeTree.Kind)),
			TEXT("GetEnumInfo only accepts enum(...) Types."));
	}

	UEnum* Enum = LoadObject<UEnum>(nullptr, *TypeTree.Path);
	if (Enum == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Enum '%s' could not be loaded."), *TypeTree.Path),
			TEXT("Check the enum path; it must name a loaded UEnum object."));
	}

	OutInfo.Enum = Enum;
	for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
	{
		const FString EntryName = Enum->GetNameStringByIndex(Index);

		// The generated MAX entry is bookkeeping, not a value a caller can pass.
		if (EntryName.EndsWith(TEXT("_MAX")))
		{
			continue;
		}

		FEditorAutomationEnumEntry Entry;
		Entry.Name = EntryName;
		Entry.DisplayName = Enum->GetDisplayNameTextByIndex(Index).ToString();
		Entry.Value = Enum->GetValueByIndex(Index);
		OutInfo.Entries.Add(Entry);
	}

	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::GetStructInfo(const FString& Type, FEditorAutomationStructInfo& OutInfo)
{
	if (Type.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Type is empty."),
			TEXT("Pass the struct Type, e.g. struct(/Script/CoreUObject.Vector)."));
	}

	EditorAutomation::FTypeTree TypeTree;
	FString ParseError;
	if (!EditorAutomation::FTypeTree::TryParse(Type, TypeTree, ParseError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("Type '%s' is not valid: %s"), *Type, *ParseError),
			TEXT("Pass Type in canonical form: struct(/Script/Module.StructName)."));
	}

	if (TypeTree.Kind != EditorAutomation::ETypeKind::Struct)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::WrongType,
			FString::Printf(TEXT("Expected struct(...) but got %s(...)."), *EditorAutomation::FTypeTree::KindName(TypeTree.Kind)),
			TEXT("GetStructInfo only accepts struct(...) Types."));
	}

	UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *TypeTree.Path);
	if (Struct == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Struct '%s' could not be loaded."), *TypeTree.Path),
			TEXT("Check the struct path; it must name a loaded UScriptStruct object."));
	}

	OutInfo.Struct = Struct;
	for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
	{
		AddPropertyInfo(OutInfo.Properties, *PropertyIt);
	}

	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::GetClassInfo(const FString& Type, FEditorAutomationClassInfo& OutInfo)
{
	if (Type.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Type is empty."),
			TEXT("Pass the class Type, e.g. class(/Script/Engine.Actor)."));
	}

	EditorAutomation::FTypeTree TypeTree;
	FString ParseError;
	if (!EditorAutomation::FTypeTree::TryParse(Type, TypeTree, ParseError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("Type '%s' is not valid: %s"), *Type, *ParseError),
			TEXT("Pass Type in canonical form: class(/Script/Module.ClassName) or object(/Script/Module.ObjectName)."));
	}

	if (TypeTree.Kind != EditorAutomation::ETypeKind::Class && TypeTree.Kind != EditorAutomation::ETypeKind::Object)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::WrongType,
			FString::Printf(TEXT("Expected class(...) or object(...) but got %s(...)."), *EditorAutomation::FTypeTree::KindName(TypeTree.Kind)),
			TEXT("GetClassInfo accepts class(...) and object(...) Types."));
	}

	UClass* Class = LoadObject<UClass>(nullptr, *TypeTree.Path);
	if (Class == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Class '%s' could not be loaded."), *TypeTree.Path),
			TEXT("Check the class path; it must name a loaded UClass object. Use GetObjectClass to derive it from an object path."));
	}

	OutInfo.Class = Class;
	for (TFieldIterator<FProperty> PropertyIt(Class); PropertyIt; ++PropertyIt)
	{
		AddPropertyInfo(OutInfo.Properties, *PropertyIt);
	}
	for (TFieldIterator<UFunction> FunctionIt(Class); FunctionIt; ++FunctionIt)
	{
		AddFunctionInfo(OutInfo.Functions, *FunctionIt);
	}

	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::GetFunctionInfo(
	const FString& Type,
	const FString& FunctionName,
	FEditorAutomationFunctionInfo& OutInfo)
{
	if (Type.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Type is empty."),
			TEXT("Pass the class Type, e.g. class(/Script/Engine.Actor)."));
	}
	if (FunctionName.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("FunctionName is empty."),
			TEXT("Pass the UFunction name, e.g. K2_GetActorLocation."));
	}

	EditorAutomation::FTypeTree TypeTree;
	FString ParseError;
	if (!EditorAutomation::FTypeTree::TryParse(Type, TypeTree, ParseError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("Type '%s' is not valid: %s"), *Type, *ParseError),
			TEXT("Pass Type in canonical form: class(/Script/Module.ClassName)."));
	}

	if (TypeTree.Kind != EditorAutomation::ETypeKind::Class && TypeTree.Kind != EditorAutomation::ETypeKind::Object)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::WrongType,
			FString::Printf(TEXT("Expected class(...) or object(...) but got %s(...)."), *EditorAutomation::FTypeTree::KindName(TypeTree.Kind)),
			TEXT("GetFunctionInfo accepts class(...) and object(...) Types."));
	}

	UClass* Class = LoadObject<UClass>(nullptr, *TypeTree.Path);
	if (Class == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Class '%s' could not be loaded."), *TypeTree.Path),
			TEXT("Check the class path; it must name a loaded UClass object."));
	}

	UFunction* Function = Class->FindFunctionByName(FName(*FunctionName));
	if (Function == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Function '%s' was not found on '%s'."), *FunctionName, *Class->GetName()),
			TEXT("Call GetClassInfo on the same Type and pick a function from its Functions list."));
	}

	BuildFunctionInfo(OutInfo, Function);
	return FEditorAutomationResult::Success();
}

// ============================================================================
// Object
// ============================================================================

FEditorAutomationResult UEditorAutomationCoreLibrary::GetObjectClass(const FString& ObjectPath, FString& OutClassType)
{
	if (ObjectPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("ObjectPath is empty."),
			TEXT("Pass a full object path, e.g. /Script/Engine.Default__StaticMeshComponent."));
	}

	UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
	if (Object == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Object '%s' could not be loaded."), *ObjectPath),
			TEXT("Check the object path; it must name a loaded object. Level instances need their full path with :PersistentLevel."));
	}

	// MakeStringFromClass returns the canonical class(...) Type that GetClassInfo/GetFunctionInfo accept.
	OutClassType = EditorAutomation::FTypeTree::MakeStringFromClass(Object->GetClass());
	return FEditorAutomationResult::Success();
}
