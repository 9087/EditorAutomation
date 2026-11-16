// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "Serialization/EditorAutomationValueJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "UObject/UnrealType.h"

namespace EditorAutomation
{
namespace
{
	/** Make a JSON string value for a path-like reference; empty becomes JSON null. */
	TSharedPtr<FJsonValue> MakeJsonStringOrNull(const FString& Text)
	{
		if (Text.IsEmpty())
		{
			return MakeShared<FJsonValueNull>();
		}
		return MakeShared<FJsonValueString>(Text);
	}

	/** Make the canonical text of a float/double, handling non-finite values. */
	FString MakeFloatText(const double Value, const bool bSingle)
	{
		if (FMath::IsNaN(Value))
		{
			return TEXT("NaN");
		}
		if (!FMath::IsFinite(Value))
		{
			return Value > 0.0 ? TEXT("Infinity") : TEXT("-Infinity");
		}
		return bSingle
			? FString::Printf(TEXT("%.9g"), Value)
			: FString::Printf(TEXT("%.17g"), Value);
	}

	/** Build one property value as nested JSON. */
	bool BuildJsonValue(FProperty* Property, void* ValueAddress, TSharedPtr<FJsonValue>& OutValue, FString& OutError)
	{
		if (Property == nullptr || ValueAddress == nullptr)
		{
			OutError = TEXT("Value address is invalid.");
			return false;
		}

		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(BoolProperty->GetPropertyValue(ValueAddress) ? TEXT("true") : TEXT("false"));
			return true;
		}
		if (FInt8Property* Property8 = CastField<FInt8Property>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(FString::Printf(TEXT("%lld"), static_cast<int64>(Property8->GetPropertyValue(ValueAddress))));
			return true;
		}
		if (FInt16Property* Property16 = CastField<FInt16Property>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(FString::Printf(TEXT("%lld"), static_cast<int64>(Property16->GetPropertyValue(ValueAddress))));
			return true;
		}
		if (FIntProperty* Property32 = CastField<FIntProperty>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(FString::Printf(TEXT("%lld"), static_cast<int64>(Property32->GetPropertyValue(ValueAddress))));
			return true;
		}
		if (FInt64Property* Property64 = CastField<FInt64Property>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(FString::Printf(TEXT("%lld"), Property64->GetPropertyValue(ValueAddress)));
			return true;
		}
		if (FUInt16Property* Unsigned16 = CastField<FUInt16Property>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(FString::Printf(TEXT("%llu"), static_cast<uint64>(Unsigned16->GetPropertyValue(ValueAddress))));
			return true;
		}
		if (FUInt32Property* Unsigned32 = CastField<FUInt32Property>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(FString::Printf(TEXT("%llu"), static_cast<uint64>(Unsigned32->GetPropertyValue(ValueAddress))));
			return true;
		}
		if (FUInt64Property* Unsigned64 = CastField<FUInt64Property>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(FString::Printf(TEXT("%llu"), Unsigned64->GetPropertyValue(ValueAddress)));
			return true;
		}
		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(MakeFloatText(static_cast<double>(FloatProperty->GetPropertyValue(ValueAddress)), true));
			return true;
		}
		if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(MakeFloatText(DoubleProperty->GetPropertyValue(ValueAddress), false));
			return true;
		}
		if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(StringProperty->GetPropertyValue(ValueAddress));
			return true;
		}
		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(NameProperty->GetPropertyValue(ValueAddress).ToString());
			return true;
		}
		if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			OutValue = MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(ValueAddress).ToString());
			return true;
		}
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			FNumericProperty* Underlying = CastField<FNumericProperty>(EnumProperty->GetUnderlyingProperty());
			const int64 EnumValue = Underlying != nullptr ? Underlying->GetSignedIntPropertyValue(ValueAddress) : 0;
			OutValue = MakeShared<FJsonValueString>(FString::Printf(TEXT("%lld"), EnumValue));
			return true;
		}
		if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			if (ByteProperty->Enum != nullptr)
			{
				OutValue = MakeShared<FJsonValueString>(
					FString::Printf(TEXT("%u"), static_cast<uint32>(ByteProperty->GetPropertyValue(ValueAddress))));
			}
			else
			{
				OutValue = MakeShared<FJsonValueString>(
					FString::Printf(TEXT("%u"), static_cast<uint32>(ByteProperty->GetPropertyValue(ValueAddress))));
			}
			return true;
		}
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == nullptr)
			{
				OutError = FString::Printf(TEXT("Struct property '%s' has no type."), *Property->GetName());
				return false;
			}

			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> MemberIt(StructProperty->Struct); MemberIt; ++MemberIt)
			{
				FProperty* Member = *MemberIt;
				if (Member == nullptr || Member->HasAnyPropertyFlags(CPF_Deprecated))
				{
					continue;
				}

				TSharedPtr<FJsonValue> MemberValue;
				if (!BuildJsonValue(Member, Member->ContainerPtrToValuePtr<void>(ValueAddress), MemberValue, OutError))
				{
					return false;
				}
				Object->SetField(Member->GetName(), MemberValue);
			}
			OutValue = MakeShared<FJsonValueObject>(Object);
			return true;
		}
		if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
		{
			OutValue = MakeJsonStringOrNull(SoftClassProperty->GetPropertyValue(ValueAddress).ToSoftObjectPath().ToString());
			return true;
		}
		if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			OutValue = MakeJsonStringOrNull(SoftObjectProperty->GetPropertyValue(ValueAddress).ToSoftObjectPath().ToString());
			return true;
		}
		if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			UClass* Class = Cast<UClass>(ClassProperty->GetObjectPropertyValue(ValueAddress));
			OutValue = MakeJsonStringOrNull(Class != nullptr ? Class->GetPathName() : FString());
			return true;
		}
		if (FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property))
		{
			UObject* Object = WeakObjectProperty->GetPropertyValue(ValueAddress).Get();
			OutValue = MakeJsonStringOrNull(Object != nullptr ? Object->GetPathName() : FString());
			return true;
		}
		if (FLazyObjectProperty* LazyObjectProperty = CastField<FLazyObjectProperty>(Property))
		{
			UObject* Object = LazyObjectProperty->GetPropertyValue(ValueAddress).Get();
			OutValue = MakeJsonStringOrNull(Object != nullptr ? Object->GetPathName() : FString());
			return true;
		}
		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			UObject* Object = ObjectProperty->GetObjectPropertyValue(ValueAddress);
			OutValue = MakeJsonStringOrNull(Object != nullptr ? Object->GetPathName() : FString());
			return true;
		}
		if (FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(Property))
		{
			UObject* Object = InterfaceProperty->GetPropertyValue(ValueAddress).GetObject();
			OutValue = MakeJsonStringOrNull(Object != nullptr ? Object->GetPathName() : FString());
			return true;
		}
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, ValueAddress);
			TArray<TSharedPtr<FJsonValue>> Values;
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				TSharedPtr<FJsonValue> Element;
				if (!BuildJsonValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), Element, OutError))
				{
					return false;
				}
				Values.Add(Element);
			}
			OutValue = MakeShared<FJsonValueArray>(Values);
			return true;
		}
		if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(SetProperty, ValueAddress);
			TArray<TSharedPtr<FJsonValue>> Values;
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}

				TSharedPtr<FJsonValue> Element;
				if (!BuildJsonValue(SetProperty->ElementProp, Helper.GetElementPtr(Index), Element, OutError))
				{
					return false;
				}
				Values.Add(Element);
			}
			OutValue = MakeShared<FJsonValueArray>(Values);
			return true;
		}
		if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(MapProperty, ValueAddress);
			TArray<TSharedPtr<FJsonValue>> Values;
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}

				TSharedPtr<FJsonValue> KeyValue;
				TSharedPtr<FJsonValue> ValueValue;
				if (!BuildJsonValue(MapProperty->KeyProp, Helper.GetKeyPtr(Index), KeyValue, OutError))
				{
					return false;
				}
				if (!BuildJsonValue(MapProperty->ValueProp, Helper.GetValuePtr(Index), ValueValue, OutError))
				{
					return false;
				}

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetField(TEXT("Key"), KeyValue);
				Entry->SetField(TEXT("Value"), ValueValue);
				Values.Add(MakeShared<FJsonValueObject>(Entry));
			}
			OutValue = MakeShared<FJsonValueArray>(Values);
			return true;
		}

		OutValue = MakeShared<FJsonValueNull>();
		return true;
	}

	/** Serialize one arbitrary JSON value to text. */
	bool SerializeJsonValue(const TSharedPtr<FJsonValue>& Value, FString& OutText, FString& OutError)
	{
		OutText.Reset();
		if (!Value.IsValid())
		{
			OutError = TEXT("JSON value is invalid.");
			return false;
		}

		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutText);

		if (Value->Type == EJson::Object)
		{
			if (!FJsonSerializer::Serialize(Value->AsObject().ToSharedRef(), Writer))
			{
				OutError = TEXT("JSON serialization failed.");
				return false;
			}
			return true;
		}

		if (Value->Type == EJson::Array)
		{
			if (!FJsonSerializer::Serialize(Value->AsArray(), Writer))
			{
				OutError = TEXT("JSON serialization failed.");
				return false;
			}
			return true;
		}

		TArray<TSharedPtr<FJsonValue>> Wrapper;
		Wrapper.Add(Value);
		if (!FJsonSerializer::Serialize(Wrapper, Writer))
		{
			OutError = TEXT("JSON serialization failed.");
			return false;
		}
		if (OutText.Len() < 2 || !OutText.StartsWith(TEXT("[")) || !OutText.EndsWith(TEXT("]")))
		{
			OutError = TEXT("JSON serialization failed.");
			return false;
		}
		OutText = OutText.Mid(1, OutText.Len() - 2);
		return true;
	}

	/** Extract scalar text from a JSON value (strings, numbers and booleans are accepted). */
	bool JsonValueAsText(const TSharedPtr<FJsonValue>& JsonValue, FString& OutText)
	{
		OutText.Reset();
		if (!JsonValue.IsValid())
		{
			return false;
		}

		switch (JsonValue->Type)
		{
		case EJson::String:
			OutText = JsonValue->AsString();
			return true;
		case EJson::Number:
			OutText = FString::Printf(TEXT("%.17g"), JsonValue->AsNumber());
			return true;
		case EJson::Boolean:
			OutText = JsonValue->AsBool() ? TEXT("true") : TEXT("false");
			return true;
		case EJson::Null:
			return true;
		default:
			return false;
		}
	}

	/** Write one JSON value into an existing property value. */
	bool BuildPropertyValue(FProperty* Property, void* ValueAddress, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
	{
		if (Property == nullptr || ValueAddress == nullptr || !JsonValue.IsValid())
		{
			OutError = TEXT("JSON value or property is invalid.");
			return false;
		}

		FString Text;
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text))
			{
				OutError = FString::Printf(TEXT("Expected a boolean for property '%s'."), *Property->GetName());
				return false;
			}
			if (Text.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Text == TEXT("1"))
			{
				BoolProperty->SetPropertyValue(ValueAddress, true);
				return true;
			}
			if (Text.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Text == TEXT("0"))
			{
				BoolProperty->SetPropertyValue(ValueAddress, false);
				return true;
			}
			OutError = FString::Printf(TEXT("'%s' is not a boolean for property '%s'."), *Text, *Property->GetName());
			return false;
		}

		if (FInt8Property* Property8 = CastField<FInt8Property>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text) || !FCString::IsNumeric(*Text))
			{
				OutError = FString::Printf(TEXT("Expected an integer for property '%s'."), *Property->GetName());
				return false;
			}
			Property8->SetPropertyValue(ValueAddress, static_cast<int8>(FCString::Atoi64(*Text)));
			return true;
		}
		if (FInt16Property* Property16 = CastField<FInt16Property>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text) || !FCString::IsNumeric(*Text))
			{
				OutError = FString::Printf(TEXT("Expected an integer for property '%s'."), *Property->GetName());
				return false;
			}
			Property16->SetPropertyValue(ValueAddress, static_cast<int16>(FCString::Atoi64(*Text)));
			return true;
		}
		if (FIntProperty* Property32 = CastField<FIntProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text) || !FCString::IsNumeric(*Text))
			{
				OutError = FString::Printf(TEXT("Expected an integer for property '%s'."), *Property->GetName());
				return false;
			}
			Property32->SetPropertyValue(ValueAddress, static_cast<int32>(FCString::Atoi64(*Text)));
			return true;
		}
		if (FInt64Property* Property64 = CastField<FInt64Property>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text) || !FCString::IsNumeric(*Text))
			{
				OutError = FString::Printf(TEXT("Expected an integer for property '%s'."), *Property->GetName());
				return false;
			}
			Property64->SetPropertyValue(ValueAddress, FCString::Atoi64(*Text));
			return true;
		}
		if (FUInt16Property* Unsigned16 = CastField<FUInt16Property>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text) || !FCString::IsNumeric(*Text))
			{
				OutError = FString::Printf(TEXT("Expected an unsigned integer for property '%s'."), *Property->GetName());
				return false;
			}
			Unsigned16->SetPropertyValue(ValueAddress, static_cast<uint16>(FCString::Atoi64(*Text)));
			return true;
		}
		if (FUInt32Property* Unsigned32 = CastField<FUInt32Property>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text) || !FCString::IsNumeric(*Text))
			{
				OutError = FString::Printf(TEXT("Expected an unsigned integer for property '%s'."), *Property->GetName());
				return false;
			}
			Unsigned32->SetPropertyValue(ValueAddress, static_cast<uint32>(FCString::Atoi64(*Text)));
			return true;
		}
		if (FUInt64Property* Unsigned64 = CastField<FUInt64Property>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text) || !FCString::IsNumeric(*Text))
			{
				OutError = FString::Printf(TEXT("Expected an unsigned integer for property '%s'."), *Property->GetName());
				return false;
			}
			Unsigned64->SetPropertyValue(ValueAddress, static_cast<uint64>(FCString::Atoi64(*Text)));
			return true;
		}
		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text) || !FCString::IsNumeric(*Text))
			{
				OutError = FString::Printf(TEXT("Expected a number for property '%s'."), *Property->GetName());
				return false;
			}
			FloatProperty->SetPropertyValue(ValueAddress, FCString::Atof(*Text));
			return true;
		}
		if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text) || !FCString::IsNumeric(*Text))
			{
				OutError = FString::Printf(TEXT("Expected a number for property '%s'."), *Property->GetName());
				return false;
			}
			DoubleProperty->SetPropertyValue(ValueAddress, FCString::Atod(*Text));
			return true;
		}
		if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text))
			{
				OutError = FString::Printf(TEXT("Expected a string for property '%s'."), *Property->GetName());
				return false;
			}
			StringProperty->SetPropertyValue(ValueAddress, Text);
			return true;
		}
		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text))
			{
				OutError = FString::Printf(TEXT("Expected a name for property '%s'."), *Property->GetName());
				return false;
			}
			NameProperty->SetPropertyValue(ValueAddress, FName(*Text));
			return true;
		}
		if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text))
			{
				OutError = FString::Printf(TEXT("Expected text for property '%s'."), *Property->GetName());
				return false;
			}
			TextProperty->SetPropertyValue(ValueAddress, FText::FromString(Text));
			return true;
		}
		if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text))
			{
				OutError = FString::Printf(TEXT("Expected a value for enum property '%s'."), *Property->GetName());
				return false;
			}

			UEnum* Enum = EnumProperty->GetEnum();
			FNumericProperty* Underlying = CastField<FNumericProperty>(EnumProperty->GetUnderlyingProperty());
			if (Enum == nullptr || Underlying == nullptr)
			{
				OutError = FString::Printf(TEXT("Enum property '%s' has no enum type."), *Property->GetName());
				return false;
			}

			int64 Value = 0;
			if (FCString::IsNumeric(*Text))
			{
				Value = FCString::Atoi64(*Text);
			}
			else
			{
				Value = Enum->GetValueByName(FName(*Text));
				if (Value == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("'%s' is not a value of enum '%s'."), *Text, *Enum->GetName());
					return false;
				}
			}

			Underlying->SetIntPropertyValue(ValueAddress, Value);
			return true;
		}
		if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text))
			{
				OutError = FString::Printf(TEXT("Expected a value for property '%s'."), *Property->GetName());
				return false;
			}

			int64 Value = 0;
			if (FCString::IsNumeric(*Text))
			{
				Value = FCString::Atoi64(*Text);
			}
			else if (ByteProperty->Enum != nullptr)
			{
				Value = ByteProperty->Enum->GetValueByName(FName(*Text));
				if (Value == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("'%s' is not a value of enum '%s'."), *Text, *ByteProperty->Enum->GetName());
					return false;
				}
			}
			else
			{
				OutError = FString::Printf(TEXT("Expected an integer for property '%s'."), *Property->GetName());
				return false;
			}

			ByteProperty->SetPropertyValue(ValueAddress, static_cast<uint8>(Value));
			return true;
		}
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == nullptr)
			{
				OutError = FString::Printf(TEXT("Struct property '%s' has no type."), *Property->GetName());
				return false;
			}

			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!JsonValue->TryGetObject(Object) || Object == nullptr || !Object->IsValid())
			{
				OutError = FString::Printf(TEXT("Expected an object for struct property '%s'."), *Property->GetName());
				return false;
			}

			for (TFieldIterator<FProperty> MemberIt(StructProperty->Struct); MemberIt; ++MemberIt)
			{
				FProperty* Member = *MemberIt;
				if (Member == nullptr || Member->HasAnyPropertyFlags(CPF_Deprecated))
				{
					continue;
				}

				const TSharedPtr<FJsonValue> MemberJson = (*Object)->TryGetField(Member->GetName());
				if (!MemberJson.IsValid())
				{
					continue;
				}

				if (!BuildPropertyValue(Member, Member->ContainerPtrToValuePtr<void>(ValueAddress), MemberJson, OutError))
				{
					return false;
				}
			}
			return true;
		}
		if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text))
			{
				OutError = FString::Printf(TEXT("Expected a class path for property '%s'."), *Property->GetName());
				return false;
			}
			SoftClassProperty->SetPropertyValue(ValueAddress, FSoftObjectPtr(FSoftObjectPath(Text)));
			return true;
		}
		if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text))
			{
				OutError = FString::Printf(TEXT("Expected an object path for property '%s'."), *Property->GetName());
				return false;
			}
			SoftObjectProperty->SetPropertyValue(ValueAddress, FSoftObjectPtr(FSoftObjectPath(Text)));
			return true;
		}
		if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text))
			{
				OutError = FString::Printf(TEXT("Expected a class path for property '%s'."), *Property->GetName());
				return false;
			}
			UClass* Class = Text.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *Text);
			if (!Text.IsEmpty() && Class == nullptr)
			{
				OutError = FString::Printf(TEXT("Class '%s' could not be loaded for property '%s'."), *Text, *Property->GetName());
				return false;
			}
			ClassProperty->SetObjectPropertyValue(ValueAddress, Class);
			return true;
		}
		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			if (!JsonValueAsText(JsonValue, Text))
			{
				OutError = FString::Printf(TEXT("Expected an object path for property '%s'."), *Property->GetName());
				return false;
			}
			UObject* Object = Text.IsEmpty() ? nullptr : LoadObject<UObject>(ObjectProperty->PropertyClass, *Text);
			if (!Text.IsEmpty() && Object == nullptr)
			{
				OutError = FString::Printf(TEXT("Object '%s' could not be loaded for property '%s'."), *Text, *Property->GetName());
				return false;
			}
			ObjectProperty->SetObjectPropertyValue(ValueAddress, Object);
			return true;
		}
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!JsonValue->TryGetArray(Values) || Values == nullptr)
			{
				OutError = FString::Printf(TEXT("Expected an array for property '%s'."), *Property->GetName());
				return false;
			}

			FScriptArrayHelper Helper(ArrayProperty, ValueAddress);
			Helper.EmptyValues();
			for (const TSharedPtr<FJsonValue>& Element : *Values)
			{
				const int32 Index = Helper.AddValue();
				if (!BuildPropertyValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), Element, OutError))
				{
					return false;
				}
			}
			return true;
		}
		if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!JsonValue->TryGetArray(Values) || Values == nullptr)
			{
				OutError = FString::Printf(TEXT("Expected an array for set property '%s'."), *Property->GetName());
				return false;
			}

			FScriptSetHelper Helper(SetProperty, ValueAddress);
			Helper.EmptyElements();
			for (const TSharedPtr<FJsonValue>& Element : *Values)
			{
				const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
				if (!BuildPropertyValue(SetProperty->ElementProp, Helper.GetElementPtr(Index), Element, OutError))
				{
					return false;
				}
			}
			Helper.Rehash();
			return true;
		}
		if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!JsonValue->TryGetArray(Values) || Values == nullptr)
			{
				OutError = FString::Printf(TEXT("Expected an array of entries for map property '%s'."), *Property->GetName());
				return false;
			}

			FScriptMapHelper Helper(MapProperty, ValueAddress);
			Helper.EmptyValues();
			for (const TSharedPtr<FJsonValue>& Element : *Values)
			{
				const TSharedPtr<FJsonObject>* EntryObject = nullptr;
				if (!Element->TryGetObject(EntryObject) || EntryObject == nullptr || !EntryObject->IsValid())
				{
					OutError = FString::Printf(TEXT("Each map entry of '%s' must be an object with Key and Value."), *Property->GetName());
					return false;
				}

				const TSharedPtr<FJsonValue> KeyJson = (*EntryObject)->TryGetField(TEXT("Key"));
				const TSharedPtr<FJsonValue> ValueJson = (*EntryObject)->TryGetField(TEXT("Value"));
				if (!KeyJson.IsValid() || !ValueJson.IsValid())
				{
					OutError = FString::Printf(TEXT("Each map entry of '%s' must have Key and Value."), *Property->GetName());
					return false;
				}

				const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
				if (!BuildPropertyValue(MapProperty->KeyProp, Helper.GetKeyPtr(Index), KeyJson, OutError))
				{
					return false;
				}
				if (!BuildPropertyValue(MapProperty->ValueProp, Helper.GetValuePtr(Index), ValueJson, OutError))
				{
					return false;
				}
			}
			Helper.Rehash();
			return true;
		}

		OutError = FString::Printf(TEXT("Property '%s' has a type that cannot be written yet."), *Property->GetName());
		return false;
	}

}

bool FValueJson::ToJson(FProperty* Property, void* ValueAddress, TSharedPtr<FJsonValue>& OutValue, FString& OutError)
{
	OutValue.Reset();
	OutError.Reset();
	return BuildJsonValue(Property, ValueAddress, OutValue, OutError);
}

bool FValueJson::ToJsonString(FProperty* Property, void* ValueAddress, FString& OutJsonText, FString& OutError)
{
	TSharedPtr<FJsonValue> Value;
	if (!ToJson(Property, ValueAddress, Value, OutError))
	{
		return false;
	}
	return SerializeJsonValue(Value, OutJsonText, OutError);
}

bool FValueJson::FromJson(const TSharedPtr<FJsonValue>& JsonValue, FProperty* Property, void* ValueAddress, FString& OutError)
{
	OutError.Reset();
	return BuildPropertyValue(Property, ValueAddress, JsonValue, OutError);
}

bool FValueJson::FromJsonString(const FString& JsonText, FProperty* Property, void* ValueAddress, FString& OutError)
{
	OutError.Reset();

	TSharedPtr<FJsonValue> JsonValue;
	{
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);
		FJsonSerializer::Deserialize(Reader, JsonValue);
	}

	if (!JsonValue.IsValid())
	{
		// FJsonSerializer only materializes objects and arrays at the root; wrap a bare scalar in an array
		// and unwrap it again so scalar values work as well.
		const TSharedRef<TJsonReader<TCHAR>> ScalarReader = TJsonReaderFactory<TCHAR>::Create(FString::Printf(TEXT("[%s]"), *JsonText));
		TArray<TSharedPtr<FJsonValue>> ScalarValues;
		if (FJsonSerializer::Deserialize(ScalarReader, ScalarValues) && ScalarValues.Num() == 1)
		{
			JsonValue = ScalarValues[0];
		}
	}

	if (!JsonValue.IsValid())
	{
		OutError = TEXT("JSON text could not be parsed.");
		return false;
	}

	return FromJson(JsonValue, Property, ValueAddress, OutError);
}

} // namespace EditorAutomation
