// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationCoreLibrary.h"

#include "Reflection/EditorAutomationTypeTree.h"
#include "Serialization/EditorAutomationValueJson.h"

#include "ScopedTransaction.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "UObject/UnrealType.h"

namespace
{
	/** Destroys an initialized function parameter block when the call scope ends. */
	struct FParameterBlockScope
	{
		UFunction* Function = nullptr;
		uint8* Data = nullptr;

		~FParameterBlockScope()
		{
			if (Function != nullptr && Data != nullptr)
			{
				Function->DestroyStruct(Data);
			}
		}
	};

	/** Serialize one JSON object to condensed text. */
	bool SerializeJsonObject(const TSharedPtr<FJsonObject>& Object, FString& OutText, FString& OutError)
	{
		OutText.Reset();
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutText);
		if (!FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
		{
			OutError = TEXT("JSON serialization failed.");
			return false;
		}
		return true;
	}
}

// ============================================================================
// Function call
// ============================================================================

FEditorAutomationCallFunctionResult UEditorAutomationCoreLibrary::CallFunction(
	const FString& ObjectPath,
	const FString& FunctionName,
	const FString& ParametersJson,
	const bool bGenerateTransaction)
{
	if (ObjectPath.IsEmpty())
	{
		return FEditorAutomationCallFunctionResult::Failed(
			EEditorAutomationCallFunctionErrorCode::InvalidArgument,
			TEXT("ObjectPath is empty."),
			TEXT("Pass a loadable object path, e.g. /Script/Engine.Default__StaticMeshComponent."));
	}

	if (FunctionName.IsEmpty())
	{
		return FEditorAutomationCallFunctionResult::Failed(
			EEditorAutomationCallFunctionErrorCode::InvalidArgument,
			TEXT("FunctionName is empty."),
			TEXT("Pass a UFunction name, e.g. SetStaticMesh."));
	}

	UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
	if (Object == nullptr)
	{
		return FEditorAutomationCallFunctionResult::Failed(
			EEditorAutomationCallFunctionErrorCode::ObjectNotFound,
			FString::Printf(TEXT("Object '%s' could not be loaded."), *ObjectPath),
			TEXT("Pass a loadable object path, e.g. /Script/Engine.Default__StaticMeshComponent."));
	}

	UFunction* Function = Object->FindFunction(FName(*FunctionName));
	if (Function == nullptr)
	{
		return FEditorAutomationCallFunctionResult::Failed(
			EEditorAutomationCallFunctionErrorCode::FunctionNotFound,
			FString::Printf(TEXT("Function '%s' was not found on '%s'."), *FunctionName, *Object->GetClass()->GetName()),
			TEXT("Pass a UFunction name exposed by the object's class."));
	}

	FString ParseError;
	TSharedPtr<FJsonObject> Parameters = MakeShared<FJsonObject>();
	if (!ParametersJson.IsEmpty())
	{
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(ParametersJson);
		if (!FJsonSerializer::Deserialize(Reader, Parameters) || !Parameters.IsValid())
		{
			return FEditorAutomationCallFunctionResult::Failed(
				EEditorAutomationCallFunctionErrorCode::InvalidJson,
				TEXT("ParametersJson could not be parsed as a JSON object."),
				TEXT("Pass a JSON object keyed by parameter name, or \"{}\"."));
		}
	}

	TArray<uint8> ParameterBuffer;
	ParameterBuffer.SetNumZeroed(FMath::Max<int32>(Function->ParmsSize, 1));
	uint8* ParameterData = ParameterBuffer.GetData();
	Function->InitializeStruct(ParameterData);
	FParameterBlockScope ParameterScope{Function, ParameterData};

	// Feed the provided JSON fields into the input parameters; omitted fields keep their defaults.
	for (TFieldIterator<FProperty> ParameterIt(Function); ParameterIt; ++ParameterIt)
	{
		FProperty* Parameter = *ParameterIt;
		if (Parameter == nullptr || Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			continue;
		}

		const bool bOutOnly = Parameter->HasAnyPropertyFlags(CPF_OutParm) && !Parameter->HasAnyPropertyFlags(CPF_ReferenceParm);
		if (bOutOnly)
		{
			continue;
		}

		const TSharedPtr<FJsonValue> ParameterJson = Parameters->TryGetField(Parameter->GetName());
		if (!ParameterJson.IsValid())
		{
			continue;
		}

		FString ParameterError;
		if (!EditorAutomation::FValueJson::FromJson(ParameterJson, Parameter, Parameter->ContainerPtrToValuePtr<void>(ParameterData), ParameterError))
		{
			return FEditorAutomationCallFunctionResult::Failed(
				EEditorAutomationCallFunctionErrorCode::ParameterError,
				FString::Printf(TEXT("Parameter '%s' could not be written: %s"), *Parameter->GetName(), *ParameterError),
				TEXT("Pass a value matching the parameter's Type."));
		}
	}

	TUniquePtr<FScopedTransaction> Transaction;
	if (bGenerateTransaction)
	{
		Transaction = MakeUnique<FScopedTransaction>(INVTEXT("Call EditorAutomation Function"));
	}

	Object->Modify();
	Object->ProcessEvent(Function, ParameterData);

	FEditorAutomationCallFunctionResult Result;

	TSharedPtr<FJsonObject> OutParameters = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> ParameterIt(Function); ParameterIt; ++ParameterIt)
	{
		FProperty* Parameter = *ParameterIt;
		if (Parameter == nullptr)
		{
			continue;
		}

		if (Parameter->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			Result.ReturnType = EditorAutomation::FTypeTree::MakeStringFromProperty(Parameter);
			if (!EditorAutomation::FValueJson::ToJsonString(Parameter, Parameter->ContainerPtrToValuePtr<void>(ParameterData), Result.ReturnValueJson, ParseError))
			{
				return FEditorAutomationCallFunctionResult::Failed(
					EEditorAutomationCallFunctionErrorCode::SerializationFailed,
					FString::Printf(TEXT("Return value could not be serialized: %s"), *ParseError),
					TEXT("This is a bug; please report the function and return type."));
			}
			continue;
		}

		if (Parameter->HasAnyPropertyFlags(CPF_OutParm) && !Parameter->HasAnyPropertyFlags(CPF_ConstParm))
		{
			TSharedPtr<FJsonValue> OutValue;
			if (!EditorAutomation::FValueJson::ToJson(Parameter, Parameter->ContainerPtrToValuePtr<void>(ParameterData), OutValue, ParseError))
			{
				return FEditorAutomationCallFunctionResult::Failed(
					EEditorAutomationCallFunctionErrorCode::SerializationFailed,
					FString::Printf(TEXT("Out parameter '%s' could not be serialized: %s"), *Parameter->GetName(), *ParseError),
					TEXT("This is a bug; please report the function and out parameter type."));
			}
			OutParameters->SetField(Parameter->GetName(), OutValue);
		}
	}

	if (!SerializeJsonObject(OutParameters, Result.OutParametersJson, ParseError))
	{
		return FEditorAutomationCallFunctionResult::Failed(
			EEditorAutomationCallFunctionErrorCode::SerializationFailed,
			FString::Printf(TEXT("Out parameters could not be serialized: %s"), *ParseError),
			TEXT("This is a bug; please report the function and out parameter types."));
	}

	return Result;
}

FEditorAutomationBatchCallResult UEditorAutomationCoreLibrary::CallFunctions(const FString& CallsJson)
{
	FEditorAutomationBatchCallResult Result;

	/** Fail the whole batch before any call runs. */
	auto FailBatch = [&Result](const FString& Message) -> FEditorAutomationBatchCallResult&
	{
		Result.bSuccess = false;
		Result.ErrorMessage = Message;
		return Result;
	};

	if (!IsInGameThread())
	{
		return FailBatch(TEXT("CallFunctions must be called on the game thread."));
	}
	if (CallsJson.IsEmpty())
	{
		return FailBatch(TEXT("CallsJson is empty."));
	}

	TArray<TSharedPtr<FJsonValue>> Calls;
	{
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(CallsJson);
		if (!FJsonSerializer::Deserialize(Reader, Calls))
		{
			return FailBatch(TEXT("CallsJson could not be parsed as a JSON array."));
		}
	}
	if (Calls.Num() == 0)
	{
		return FailBatch(TEXT("CallsJson is an empty array."));
	}
	if (Calls.Num() > 256)
	{
		return FailBatch(TEXT("At most 256 calls are allowed per batch."));
	}

	Result.Results.Reserve(Calls.Num());
	for (int32 Index = 0; Index < Calls.Num(); ++Index)
	{
		FEditorAutomationBatchCallEntry Entry;
		Entry.Index = Index;

		const TSharedPtr<FJsonObject> Call = Calls[Index].IsValid() ? Calls[Index]->AsObject() : nullptr;
		if (!Call.IsValid())
		{
			Entry.ErrorCode = static_cast<int32>(EEditorAutomationCallFunctionErrorCode::InvalidArgument);
			Entry.ErrorMessage = TEXT("Each batch entry must be a JSON object.");
			Entry.Hint = TEXT("Use {\"objectPath\": ..., \"functionName\": ..., \"parametersJson\": ...}.");
			Result.bSuccess = false;
			Result.Results.Add(MoveTemp(Entry));
			continue;
		}

		Call->TryGetStringField(TEXT("objectPath"), Entry.ObjectPath);
		Call->TryGetStringField(TEXT("functionName"), Entry.FunctionName);
		FString ParametersJson;
		if (!Call->TryGetStringField(TEXT("parametersJson"), ParametersJson) || ParametersJson.IsEmpty())
		{
			ParametersJson = TEXT("{}");
		}
		bool bGenerateTransaction = false;
		Call->TryGetBoolField(TEXT("generateTransaction"), bGenerateTransaction);

		const FEditorAutomationCallFunctionResult CallResult =
			CallFunction(Entry.ObjectPath, Entry.FunctionName, ParametersJson, bGenerateTransaction);

		Entry.ErrorCode = CallResult.ErrorCode;
		Entry.ErrorMessage = CallResult.ErrorMessage;
		Entry.Hint = CallResult.Hint;
		Entry.ReturnValueJson = CallResult.ReturnValueJson;
		Entry.OutParametersJson = CallResult.OutParametersJson;
		Entry.ReturnType = CallResult.ReturnType;
		if (Entry.ErrorCode != 0)
		{
			Result.bSuccess = false;
		}
		Result.Results.Add(MoveTemp(Entry));
	}
	return Result;
}


