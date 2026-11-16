// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationCoreLibrary.h"

#include "EditorAutomationLog.h"

#include "Reflection/EditorAutomationPropertyPath.h"
#include "Reflection/EditorAutomationText.h"
#include "Reflection/EditorAutomationTypeTree.h"
#include "Serialization/EditorAutomationValueJson.h"

#include "ScopedTransaction.h"

#include "UObject/UnrealType.h"

// ============================================================================
// Property
// ============================================================================

namespace
{
	/** Log one property failure and return an empty/invalid payload; readable through GetRecentLog. */
	template <typename ResultT>
	ResultT PropertyFailure(const FString& Message)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("%s"), *Message);
		return ResultT{};
	}
}


FEditorAutomationPropertyTypeInfo UEditorAutomationCoreLibrary::GetPropertyType(const FString& Type, const FString& PropertyPath)
{
	if (Type.IsEmpty())
	{
		return PropertyFailure<FEditorAutomationPropertyTypeInfo>(TEXT("Type is empty."));
	}

	if (PropertyPath.IsEmpty())
	{
		return PropertyFailure<FEditorAutomationPropertyTypeInfo>(TEXT("PropertyPath is empty."));
	}

	EditorAutomation::FTypeTree TypeTree;
	FString ParseError;
	if (!EditorAutomation::FTypeTree::TryParse(Type, TypeTree, ParseError))
	{
		return PropertyFailure<FEditorAutomationPropertyTypeInfo>(FString::Printf(TEXT("Type '%s' is not valid: %s"), *Type, *ParseError));
	}

	if (TypeTree.Kind != EditorAutomation::ETypeKind::Class && TypeTree.Kind != EditorAutomation::ETypeKind::Object)
	{
		return PropertyFailure<FEditorAutomationPropertyTypeInfo>(FString::Printf(TEXT("Expected class(...) or object(...) but got %s(...)."), *EditorAutomation::FTypeTree::KindName(TypeTree.Kind)));
	}

	UClass* Class = LoadObject<UClass>(nullptr, *TypeTree.Path);
	if (Class == nullptr)
	{
		return PropertyFailure<FEditorAutomationPropertyTypeInfo>(FString::Printf(TEXT("Class '%s' could not be loaded."), *TypeTree.Path));
	}

	TArray<EditorAutomation::FPropertyPath::FSegment> Segments;
	if (!EditorAutomation::FPropertyPath::Parse(PropertyPath, Segments, ParseError))
	{
		return PropertyFailure<FEditorAutomationPropertyTypeInfo>(ParseError);
	}

	FProperty* Property = nullptr;
	if (!EditorAutomation::FPropertyPath::ResolveType(Class, Segments, Property, ParseError))
	{
		return PropertyFailure<FEditorAutomationPropertyTypeInfo>(ParseError);
	}

	const FString PropertyType = EditorAutomation::FTypeTree::MakeStringFromProperty(Property);
	if (PropertyType.IsEmpty())
	{
		return PropertyFailure<FEditorAutomationPropertyTypeInfo>(FString::Printf(TEXT("Property '%s' has a type that is not supported yet."), *PropertyPath));
	}

	FEditorAutomationPropertyTypeInfo Info;
	Info.Type = PropertyType;
	Info.DisplayNameText = EditorAutomation::MakeTextRef(Property->GetDisplayNameText());
	return Info;
}

FString UEditorAutomationCoreLibrary::GetPropertyValue(const FString& ObjectPath, const FString& PropertyPath)
{
	if (ObjectPath.IsEmpty())
	{
		return PropertyFailure<FString>(TEXT("ObjectPath is empty."));
	}

	if (PropertyPath.IsEmpty())
	{
		return PropertyFailure<FString>(TEXT("PropertyPath is empty."));
	}

	UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
	if (Object == nullptr)
	{
		return PropertyFailure<FString>(FString::Printf(TEXT("Object '%s' could not be loaded."), *ObjectPath));
	}

	TArray<EditorAutomation::FPropertyPath::FSegment> Segments;
	FString PathError;
	if (!EditorAutomation::FPropertyPath::Parse(PropertyPath, Segments, PathError))
	{
		return PropertyFailure<FString>(PathError);
	}

	FProperty* Property = nullptr;
	void* ValueAddress = nullptr;
	if (!EditorAutomation::FPropertyPath::Resolve(Object, Segments, Property, ValueAddress, PathError))
	{
		return PropertyFailure<FString>(PathError);
	}

	if (EditorAutomation::FTypeTree::MakeStringFromProperty(Property).IsEmpty())
	{
		return PropertyFailure<FString>(FString::Printf(TEXT("Property '%s' has a type that is not supported yet."), *PropertyPath));
	}

	FString ValueJson;
	FString JsonError;
	if (!EditorAutomation::FValueJson::ToJsonString(Property, ValueAddress, ValueJson, JsonError))
	{
		return PropertyFailure<FString>(FString::Printf(TEXT("Property '%s' could not be serialized: %s"), *PropertyPath, *JsonError));
	}

	return MoveTemp(ValueJson);
}

FEditorAutomationPropertyWrite UEditorAutomationCoreLibrary::SetPropertyValue(
	const FString& ObjectPath,
	const FString& PropertyPath,
	const FString& ValueJson,
	const bool bGenerateTransaction)
{
	if (ObjectPath.IsEmpty())
	{
		return PropertyFailure<FEditorAutomationPropertyWrite>(TEXT("ObjectPath is empty."));
	}

	if (PropertyPath.IsEmpty())
	{
		return PropertyFailure<FEditorAutomationPropertyWrite>(TEXT("PropertyPath is empty."));
	}

	if (ValueJson.IsEmpty())
	{
		return PropertyFailure<FEditorAutomationPropertyWrite>(TEXT("ValueJson is empty."));
	}

	UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
	if (Object == nullptr)
	{
		return PropertyFailure<FEditorAutomationPropertyWrite>(FString::Printf(TEXT("Object '%s' could not be loaded."), *ObjectPath));
	}

	TArray<EditorAutomation::FPropertyPath::FSegment> Segments;
	FString PathError;
	if (!EditorAutomation::FPropertyPath::Parse(PropertyPath, Segments, PathError))
	{
		return PropertyFailure<FEditorAutomationPropertyWrite>(PathError);
	}

	FProperty* Property = nullptr;
	void* ValueAddress = nullptr;
	if (!EditorAutomation::FPropertyPath::Resolve(Object, Segments, Property, ValueAddress, PathError))
	{
		return PropertyFailure<FEditorAutomationPropertyWrite>(PathError);
	}

	if (EditorAutomation::FTypeTree::MakeStringFromProperty(Property).IsEmpty())
	{
		return PropertyFailure<FEditorAutomationPropertyWrite>(FString::Printf(TEXT("Property '%s' has a type that is not supported yet."), *PropertyPath));
	}

	FEditorAutomationPropertyWrite Result;
	Result.DisplayNameText = EditorAutomation::MakeTextRef(Property->GetDisplayNameText());

	FString JsonError;
	if (!EditorAutomation::FValueJson::ToJsonString(Property, ValueAddress, Result.OldValueJson, JsonError))
	{
		return PropertyFailure<FEditorAutomationPropertyWrite>(FString::Printf(TEXT("Property '%s' could not be read before writing: %s"), *PropertyPath, *JsonError));
	}

	TUniquePtr<FScopedTransaction> Transaction;
	if (bGenerateTransaction)
	{
		Transaction = MakeUnique<FScopedTransaction>(INVTEXT("Set EditorAutomation Property"));
	}

	Object->Modify();
	if (!EditorAutomation::FValueJson::FromJsonString(ValueJson, Property, ValueAddress, JsonError))
	{
		const FString WriteError = JsonError;

		// Aggregate values are written member by member, so a failed write may already have applied some
		// members. Compare the live value with the snapshot and roll back, so a failed write never leaves
		// a partially modified value behind.
		FString CurrentValueJson;
		FString SnapshotError;
		const bool bCurrentValueRead = EditorAutomation::FValueJson::ToJsonString(
			Property,
			ValueAddress,
			CurrentValueJson,
			SnapshotError);
		if (bCurrentValueRead && CurrentValueJson == Result.OldValueJson)
		{
			// Nothing was changed (for example a single scalar was rejected before it was written, or the
			// property type has no writable encoding such as a delegate). Report the original failure.
			return PropertyFailure<FEditorAutomationPropertyWrite>(FString::Printf(TEXT("Property '%s' could not be written: %s"), *PropertyPath, *WriteError));
		}

		FString RollbackError;
		const bool bRolledBack = EditorAutomation::FValueJson::FromJsonString(
			Result.OldValueJson,
			Property,
			ValueAddress,
			RollbackError);
		if (!bRolledBack)
		{
			return PropertyFailure<FEditorAutomationPropertyWrite>(FString::Printf(
					TEXT("Property '%s' could not be written: %s. Rolling back to the previous value failed: %s"),
					*PropertyPath,
					*WriteError,
					*RollbackError));
		}

		// The value is back to what it was; notify listeners and mark the package dirty so derived editor
		// state (render state, details views, ...) agrees with the restored value.
		FPropertyChangedEvent RollbackEvent(Property);
		Object->PostEditChangeProperty(RollbackEvent);
		Object->MarkPackageDirty();

		return PropertyFailure<FEditorAutomationPropertyWrite>(FString::Printf(
				TEXT("Property '%s' could not be written: %s. The previous value was restored."),
				*PropertyPath,
				*WriteError));
	}

	FPropertyChangedEvent PropertyChangedEvent(Property);
	Object->PostEditChangeProperty(PropertyChangedEvent);
	Object->MarkPackageDirty();

	if (!EditorAutomation::FValueJson::ToJsonString(Property, ValueAddress, Result.NewValueJson, JsonError))
	{
		return PropertyFailure<FEditorAutomationPropertyWrite>(FString::Printf(TEXT("Property '%s' was written but could not be read back: %s"), *PropertyPath, *JsonError));
	}

	Result.bSuccess = true;
	return Result;
}
