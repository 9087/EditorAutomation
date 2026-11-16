// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationCoreLibrary.h"

#include "EditorAutomationLog.h"

#include "Reflection/EditorAutomationText.h"
#include "Reflection/EditorAutomationTypeTree.h"

#include "UObject/UnrealType.h"

namespace
{
	/** Log one reflection failure and return an empty result; the log is readable through GetRecentLog. */
	template <typename ResultT>
	ResultT ReflectionFailure(const FString& Message)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("%s"), *Message);
		return ResultT{};
	}

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
		Properties.Add(MoveTemp(Info));
	}

	/** Fill one function's signature, regardless of whether it is BlueprintCallable. */
	void BuildFunctionInfo(FEditorAutomationFunctionInfo& Info, UFunction* Function)
	{
		Info.Name = Function->GetName();
		Info.bBlueprintCallable = Function->HasAnyFunctionFlags(FUNC_BlueprintCallable);
		Info.bBlueprintPure = Function->HasAnyFunctionFlags(FUNC_BlueprintPure);
		Info.bStatic = Function->HasAnyFunctionFlags(FUNC_Static);

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

FEditorAutomationEnumInfo UEditorAutomationCoreLibrary::GetEnumInfo(const FString& Type)
{
	if (Type.IsEmpty())
	{
		return ReflectionFailure<FEditorAutomationEnumInfo>(TEXT("Type is empty."));
	}

	EditorAutomation::FTypeTree TypeTree;
	FString ParseError;
	if (!EditorAutomation::FTypeTree::TryParse(Type, TypeTree, ParseError))
	{
		return ReflectionFailure<FEditorAutomationEnumInfo>(FString::Printf(TEXT("Type '%s' is not valid: %s"), *Type, *ParseError));
	}

	if (TypeTree.Kind != EditorAutomation::ETypeKind::Enum)
	{
		return ReflectionFailure<FEditorAutomationEnumInfo>(FString::Printf(TEXT("Expected enum(...) but got %s(...)."), *EditorAutomation::FTypeTree::KindName(TypeTree.Kind)));
	}

	UEnum* Enum = LoadObject<UEnum>(nullptr, *TypeTree.Path);
	if (Enum == nullptr)
	{
		return ReflectionFailure<FEditorAutomationEnumInfo>(FString::Printf(TEXT("Enum '%s' could not be loaded."), *TypeTree.Path));
	}

	FEditorAutomationEnumInfo Result;
	Result.Enum = Enum;
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
		Result.Entries.Add(Entry);
	}

	return Result;
}

FEditorAutomationStructInfo UEditorAutomationCoreLibrary::GetStructInfo(const FString& Type)
{
	if (Type.IsEmpty())
	{
		return ReflectionFailure<FEditorAutomationStructInfo>(TEXT("Type is empty."));
	}

	EditorAutomation::FTypeTree TypeTree;
	FString ParseError;
	if (!EditorAutomation::FTypeTree::TryParse(Type, TypeTree, ParseError))
	{
		return ReflectionFailure<FEditorAutomationStructInfo>(FString::Printf(TEXT("Type '%s' is not valid: %s"), *Type, *ParseError));
	}

	if (TypeTree.Kind != EditorAutomation::ETypeKind::Struct)
	{
		return ReflectionFailure<FEditorAutomationStructInfo>(FString::Printf(TEXT("Expected struct(...) but got %s(...)."), *EditorAutomation::FTypeTree::KindName(TypeTree.Kind)));
	}

	UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *TypeTree.Path);
	if (Struct == nullptr)
	{
		return ReflectionFailure<FEditorAutomationStructInfo>(FString::Printf(TEXT("Struct '%s' could not be loaded."), *TypeTree.Path));
	}

	FEditorAutomationStructInfo Result;
	Result.Struct = Struct;
	for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
	{
		AddPropertyInfo(Result.Properties, *PropertyIt);
	}

	return Result;
}

FEditorAutomationClassInfo UEditorAutomationCoreLibrary::GetClassInfo(const FString& Type)
{
	if (Type.IsEmpty())
	{
		return ReflectionFailure<FEditorAutomationClassInfo>(TEXT("Type is empty."));
	}

	EditorAutomation::FTypeTree TypeTree;
	FString ParseError;
	if (!EditorAutomation::FTypeTree::TryParse(Type, TypeTree, ParseError))
	{
		return ReflectionFailure<FEditorAutomationClassInfo>(FString::Printf(TEXT("Type '%s' is not valid: %s"), *Type, *ParseError));
	}

	if (TypeTree.Kind != EditorAutomation::ETypeKind::Class && TypeTree.Kind != EditorAutomation::ETypeKind::Object)
	{
		return ReflectionFailure<FEditorAutomationClassInfo>(FString::Printf(TEXT("Expected class(...) or object(...) but got %s(...)."), *EditorAutomation::FTypeTree::KindName(TypeTree.Kind)));
	}

	UClass* Class = LoadObject<UClass>(nullptr, *TypeTree.Path);
	if (Class == nullptr)
	{
		return ReflectionFailure<FEditorAutomationClassInfo>(FString::Printf(TEXT("Class '%s' could not be loaded."), *TypeTree.Path));
	}

	FEditorAutomationClassInfo Result;
	Result.Class = Class;

	for (TFieldIterator<FProperty> PropertyIt(Class); PropertyIt; ++PropertyIt)
	{
		AddPropertyInfo(Result.Properties, *PropertyIt);
	}
	for (TFieldIterator<UFunction> FunctionIt(Class); FunctionIt; ++FunctionIt)
	{
		AddFunctionInfo(Result.Functions, *FunctionIt);
	}

	return Result;
}

FEditorAutomationFunctionInfo UEditorAutomationCoreLibrary::GetFunctionInfo(const FString& Type, const FString& FunctionName)
{
	if (Type.IsEmpty())
	{
		return ReflectionFailure<FEditorAutomationFunctionInfo>(TEXT("Type is empty."));
	}

	if (FunctionName.IsEmpty())
	{
		return ReflectionFailure<FEditorAutomationFunctionInfo>(TEXT("FunctionName is empty."));
	}

	EditorAutomation::FTypeTree TypeTree;
	FString ParseError;
	if (!EditorAutomation::FTypeTree::TryParse(Type, TypeTree, ParseError))
	{
		return ReflectionFailure<FEditorAutomationFunctionInfo>(FString::Printf(TEXT("Type '%s' is not valid: %s"), *Type, *ParseError));
	}

	if (TypeTree.Kind != EditorAutomation::ETypeKind::Class && TypeTree.Kind != EditorAutomation::ETypeKind::Object)
	{
		return ReflectionFailure<FEditorAutomationFunctionInfo>(FString::Printf(TEXT("Expected class(...) or object(...) but got %s(...)."), *EditorAutomation::FTypeTree::KindName(TypeTree.Kind)));
	}

	UClass* Class = LoadObject<UClass>(nullptr, *TypeTree.Path);
	if (Class == nullptr)
	{
		return ReflectionFailure<FEditorAutomationFunctionInfo>(FString::Printf(TEXT("Class '%s' could not be loaded."), *TypeTree.Path));
	}

	UFunction* Function = Class->FindFunctionByName(FName(*FunctionName));
	if (Function == nullptr)
	{
		return ReflectionFailure<FEditorAutomationFunctionInfo>(FString::Printf(TEXT("Function '%s' was not found on '%s'."), *FunctionName, *Class->GetName()));
	}

	FEditorAutomationFunctionInfo Result;
	BuildFunctionInfo(Result, Function);
	return Result;
}

// ============================================================================
// Object
// ============================================================================

FString UEditorAutomationCoreLibrary::GetObjectClass(const FString& ObjectPath)
{
	if (ObjectPath.IsEmpty())
	{
		return ReflectionFailure<FString>(TEXT("GetObjectClass: ObjectPath is empty."));
	}

	UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
	if (Object == nullptr)
	{
		return ReflectionFailure<FString>(FString::Printf(TEXT("GetObjectClass: object '%s' could not be loaded."), *ObjectPath));
	}

	// MakeStringFromClass returns the canonical class(...) Type that GetClassInfo/GetFunctionInfo accept.
	return EditorAutomation::FTypeTree::MakeStringFromClass(Object->GetClass());
}
