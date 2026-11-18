// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationCoreLibrary.h"

#include "Reflection/EditorAutomationPropertyPath.h"
#include "Reflection/EditorAutomationText.h"
#include "Reflection/EditorAutomationTypeTree.h"
#include "Serialization/EditorAutomationValueJson.h"

#include "ScopedTransaction.h"

#include "UObject/UnrealType.h"

// ============================================================================
// Property
// ============================================================================

FEditorAutomationResult UEditorAutomationCoreLibrary::GetPropertyType(
	const FString& Type,
	const FString& PropertyPath,
	FEditorAutomationPropertyTypeInfo& OutInfo)
{
	if (Type.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Type is empty."),
			TEXT("Pass the class Type, e.g. class(/Script/Engine.StaticMeshComponent)."));
	}
	if (PropertyPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("PropertyPath is empty."),
			TEXT("Pass a property path, e.g. RelativeLocation.X."));
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
			TEXT("GetPropertyType accepts class(...) and object(...) Types."));
	}

	UClass* Class = LoadObject<UClass>(nullptr, *TypeTree.Path);
	if (Class == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Class '%s' could not be loaded."), *TypeTree.Path),
			TEXT("Check the class path; it must name a loaded UClass object."));
	}

	TArray<EditorAutomation::FPropertyPath::FSegment> Segments;
	if (!EditorAutomation::FPropertyPath::Parse(PropertyPath, Segments, ParseError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			ParseError,
			TEXT("Pass a dotted property path, e.g. RelativeLocation.X or OverrideMaterials[0]."));
	}

	FProperty* Property = nullptr;
	if (!EditorAutomation::FPropertyPath::ResolveType(Class, Segments, Property, ParseError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			ParseError,
			TEXT("Call GetClassInfo on the same Type to inspect its property names."));
	}

	const FString PropertyType = EditorAutomation::FTypeTree::MakeStringFromProperty(Property);
	if (PropertyType.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::WrongType,
			FString::Printf(TEXT("Property '%s' has a type that is not supported yet."), *PropertyPath),
			TEXT("The property exists but its Type has no canonical form; report the property if you need it."));
	}

	OutInfo.Type = PropertyType;
	OutInfo.DisplayNameText = EditorAutomation::MakeTextRef(Property->GetDisplayNameText());
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::GetPropertyValue(
	const FString& ObjectPath,
	const FString& PropertyPath,
	FString& OutValueJson)
{
	if (ObjectPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("ObjectPath is empty."),
			TEXT("Pass a full object path, e.g. /Script/Engine.Default__StaticMeshComponent."));
	}
	if (PropertyPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("PropertyPath is empty."),
			TEXT("Pass a property path, e.g. RelativeLocation.X."));
	}

	UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
	if (Object == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Object '%s' could not be loaded."), *ObjectPath),
			TEXT("Check the object path; it must name a loaded object."));
	}

	TArray<EditorAutomation::FPropertyPath::FSegment> Segments;
	FString PathError;
	if (!EditorAutomation::FPropertyPath::Parse(PropertyPath, Segments, PathError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			PathError,
			TEXT("Pass a dotted property path, e.g. RelativeLocation.X or OverrideMaterials[0]."));
	}

	FProperty* Property = nullptr;
	void* ValueAddress = nullptr;
	if (!EditorAutomation::FPropertyPath::Resolve(Object, Segments, Property, ValueAddress, PathError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			PathError,
			TEXT("Call GetClassInfo on the object's class to inspect its property names."));
	}

	if (EditorAutomation::FTypeTree::MakeStringFromProperty(Property).IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::WrongType,
			FString::Printf(TEXT("Property '%s' has a type that is not supported yet."), *PropertyPath),
			TEXT("The property exists but its Type has no canonical form; report the property if you need it."));
	}

	FString JsonError;
	if (!EditorAutomation::FValueJson::ToJsonString(Property, ValueAddress, OutValueJson, JsonError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::SerializationFailed,
			FString::Printf(TEXT("Property '%s' could not be serialized: %s"), *PropertyPath, *JsonError),
			TEXT("This is an EditorAutomation bug; report the property and its Type."));
	}

	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationCoreLibrary::SetPropertyValue(
	const FString& ObjectPath,
	const FString& PropertyPath,
	const FString& ValueJson,
	const bool bGenerateTransaction,
	FEditorAutomationPropertyWrite& OutWrite)
{
	if (ObjectPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("ObjectPath is empty."),
			TEXT("Pass a full object path, e.g. /Script/Engine.Default__StaticMeshComponent."));
	}
	if (PropertyPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("PropertyPath is empty."),
			TEXT("Pass a property path, e.g. RelativeLocation.X."));
	}
	if (ValueJson.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("ValueJson is empty."),
			TEXT("Pass the new value as nested JSON text, e.g. \"12.5\" or {\"X\":\"0\",\"Y\":\"0\",\"Z\":\"1\"}."));
	}

	UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
	if (Object == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Object '%s' could not be loaded."), *ObjectPath),
			TEXT("Check the object path; it must name a loaded object."));
	}

	TArray<EditorAutomation::FPropertyPath::FSegment> Segments;
	FString PathError;
	if (!EditorAutomation::FPropertyPath::Parse(PropertyPath, Segments, PathError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			PathError,
			TEXT("Pass a dotted property path, e.g. RelativeLocation.X or OverrideMaterials[0]."));
	}

	FProperty* Property = nullptr;
	void* ValueAddress = nullptr;
	if (!EditorAutomation::FPropertyPath::Resolve(Object, Segments, Property, ValueAddress, PathError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			PathError,
			TEXT("Call GetClassInfo on the object's class to inspect its property names."));
	}

	if (EditorAutomation::FTypeTree::MakeStringFromProperty(Property).IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::WrongType,
			FString::Printf(TEXT("Property '%s' has a type that is not supported yet."), *PropertyPath),
			TEXT("The property exists but its Type has no canonical form; report the property if you need it."));
	}

	OutWrite.DisplayNameText = EditorAutomation::MakeTextRef(Property->GetDisplayNameText());

	FString JsonError;
	if (!EditorAutomation::FValueJson::ToJsonString(Property, ValueAddress, OutWrite.OldValueJson, JsonError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::SerializationFailed,
			FString::Printf(TEXT("Property '%s' could not be read before writing: %s"), *PropertyPath, *JsonError),
			TEXT("This is an EditorAutomation bug; report the property and its Type."));
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
		if (bCurrentValueRead && CurrentValueJson == OutWrite.OldValueJson)
		{
			// Nothing was changed (for example a single scalar was rejected before it was written, or the
			// property type has no writable encoding such as a delegate).
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::InvalidArgument,
				FString::Printf(TEXT("Property '%s' could not be written: %s"), *PropertyPath, *WriteError),
				TEXT("Check the property Type with GetPropertyType and encode the value accordingly."));
		}

		FString RollbackError;
		const bool bRolledBack = EditorAutomation::FValueJson::FromJsonString(
			OutWrite.OldValueJson,
			Property,
			ValueAddress,
			RollbackError);
		if (!bRolledBack)
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::InternalError,
				FString::Printf(
					TEXT("Property '%s' could not be written: %s. Rolling back to the previous value failed: %s"),
					*PropertyPath,
					*WriteError,
					*RollbackError),
				TEXT("The property may now be partially modified; report the property and its Type."));
		}

		// The value is back to what it was; notify listeners and mark the package dirty so derived editor
		// state (render state, details views, ...) agrees with the restored value.
		FPropertyChangedEvent RollbackEvent(Property);
		Object->PostEditChangeProperty(RollbackEvent);
		Object->MarkPackageDirty();

		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(
				TEXT("Property '%s' could not be written: %s. The previous value was restored."),
				*PropertyPath,
				*WriteError),
			TEXT("Check the property Type with GetPropertyType and encode the value accordingly."));
	}

	FPropertyChangedEvent PropertyChangedEvent(Property);
	Object->PostEditChangeProperty(PropertyChangedEvent);
	Object->MarkPackageDirty();

	if (!EditorAutomation::FValueJson::ToJsonString(Property, ValueAddress, OutWrite.NewValueJson, JsonError))
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::SerializationFailed,
			FString::Printf(TEXT("Property '%s' was written but could not be read back: %s"), *PropertyPath, *JsonError),
			TEXT("This is an EditorAutomation bug; report the property and its Type."));
	}

	return FEditorAutomationResult::Success();
}
