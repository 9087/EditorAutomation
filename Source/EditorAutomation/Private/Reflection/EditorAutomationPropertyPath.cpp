// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "Reflection/EditorAutomationPropertyPath.h"

#include "UObject/UnrealType.h"

namespace EditorAutomation
{
namespace
{
	/** A property name: a letter or underscore followed by letters, digits or underscores. */
	bool IsPathIdentifier(const FString& Text)
	{
		if (Text.IsEmpty())
		{
			return false;
		}

		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR Character = Text[Index];
			const bool bValid = Index == 0
				? (FChar::IsAlpha(Character) || Character == TEXT('_'))
				: (FChar::IsAlpha(Character) || FChar::IsDigit(Character) || Character == TEXT('_'));
			if (!bValid)
			{
				return false;
			}
		}
		return true;
	}
}

bool FPropertyPath::Parse(const FString& Path, TArray<FSegment>& OutSegments, FString& OutError)
{
	OutSegments.Reset();
	OutError.Reset();

	FString Remaining = Path.TrimStartAndEnd();
	if (Remaining.IsEmpty())
	{
		OutError = TEXT("PropertyPath is empty.");
		return false;
	}

	while (!Remaining.IsEmpty())
	{
		int32 DotIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Remaining.Len(); ++Index)
		{
			if (Remaining[Index] == TEXT('.'))
			{
				DotIndex = Index;
				break;
			}
		}

		FString Head = DotIndex == INDEX_NONE ? Remaining : Remaining.Left(DotIndex);
		Head.TrimStartAndEndInline();

		FSegment Segment;
		const int32 BracketIndex = Head.Find(TEXT("["), ESearchCase::CaseSensitive);
		if (BracketIndex != INDEX_NONE)
		{
			if (!Head.EndsWith(TEXT("]")))
			{
				OutError = FString::Printf(TEXT("Property path segment '%s' has an unterminated index."), *Head);
				return false;
			}

			Segment.Name = Head.Left(BracketIndex).TrimStartAndEnd();
			const FString IndexText = Head.Mid(BracketIndex + 1, Head.Len() - BracketIndex - 2).TrimStartAndEnd();
			if (!IndexText.IsNumeric())
			{
				OutError = FString::Printf(TEXT("Property path index '%s' is not a number."), *IndexText);
				return false;
			}
			Segment.Index = FCString::Atoi(*IndexText);
		}
		else
		{
			Segment.Name = Head;
		}

		if (!IsPathIdentifier(Segment.Name))
		{
			OutError = FString::Printf(TEXT("Property path segment '%s' is not a property name."), *Segment.Name);
			return false;
		}

		OutSegments.Add(MoveTemp(Segment));

		if (DotIndex == INDEX_NONE)
		{
			break;
		}

		Remaining = Remaining.Mid(DotIndex + 1).TrimStartAndEnd();
		if (Remaining.IsEmpty())
		{
			OutError = TEXT("PropertyPath ends with '.'.");
			return false;
		}
	}

	return true;
}

bool FPropertyPath::Resolve(
	UObject* Object,
	const TArray<FSegment>& Segments,
	FProperty*& OutProperty,
	void*& OutValueAddress,
	FString& OutError)
{
	OutProperty = nullptr;
	OutValueAddress = nullptr;
	OutError.Reset();

	if (Object == nullptr)
	{
		OutError = TEXT("Object is null.");
		return false;
	}

	UStruct* ScopeStruct = Object->GetClass();
	void* ScopeAddress = Object;

	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FSegment& Segment = Segments[SegmentIndex];
		FProperty* Property = FindFProperty<FProperty>(ScopeStruct, FName(*Segment.Name));
		if (Property == nullptr)
		{
			OutError = FString::Printf(TEXT("Property '%s' was not found on '%s'."), *Segment.Name, *ScopeStruct->GetName());
			return false;
		}

		FProperty* EffectiveProperty = Property;
		void* ValueAddress = Property->ContainerPtrToValuePtr<void>(ScopeAddress);

		if (Segment.Index != INDEX_NONE)
		{
			FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
			if (ArrayProperty == nullptr)
			{
				OutError = FString::Printf(TEXT("Property '%s' is not an array but an index was given."), *Segment.Name);
				return false;
			}

			FScriptArrayHelper Helper(ArrayProperty, ValueAddress);
			if (Segment.Index < 0 || Segment.Index >= Helper.Num())
			{
				OutError = FString::Printf(
					TEXT("Property '%s' index %d is out of range (size %d)."),
					*Segment.Name,
					Segment.Index,
					Helper.Num());
				return false;
			}

			ValueAddress = Helper.GetRawPtr(Segment.Index);
			EffectiveProperty = ArrayProperty->Inner;
		}

		if (SegmentIndex == Segments.Num() - 1)
		{
			OutProperty = EffectiveProperty;
			OutValueAddress = ValueAddress;
			return true;
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(EffectiveProperty))
		{
			if (StructProperty->Struct == nullptr)
			{
				OutError = FString::Printf(TEXT("Property '%s' has no struct type."), *Segment.Name);
				return false;
			}
			ScopeStruct = StructProperty->Struct;
			ScopeAddress = ValueAddress;
		}
		else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(EffectiveProperty))
		{
			UObject* InnerObject = ObjectProperty->GetObjectPropertyValue(ValueAddress);
			if (InnerObject == nullptr)
			{
				OutError = FString::Printf(TEXT("Property '%s' is null and cannot be traversed."), *Segment.Name);
				return false;
			}
			ScopeStruct = InnerObject->GetClass();
			ScopeAddress = InnerObject;
		}
		else
		{
			OutError = FString::Printf(TEXT("Property '%s' cannot be traversed into."), *Segment.Name);
			return false;
		}
	}

	return false;
}

bool FPropertyPath::ResolveType(
	UClass* Class,
	const TArray<FSegment>& Segments,
	FProperty*& OutProperty,
	FString& OutError)
{
	OutProperty = nullptr;
	OutError.Reset();

	if (Class == nullptr)
	{
		OutError = TEXT("Class is null.");
		return false;
	}

	UStruct* ScopeStruct = Class;

	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FSegment& Segment = Segments[SegmentIndex];
		FProperty* Property = FindFProperty<FProperty>(ScopeStruct, FName(*Segment.Name));
		if (Property == nullptr)
		{
			OutError = FString::Printf(TEXT("Property '%s' was not found on '%s'."), *Segment.Name, *ScopeStruct->GetName());
			return false;
		}

		FProperty* EffectiveProperty = Property;

		if (Segment.Index != INDEX_NONE)
		{
			FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
			if (ArrayProperty == nullptr)
			{
				OutError = FString::Printf(TEXT("Property '%s' is not an array but an index was given."), *Segment.Name);
				return false;
			}
			EffectiveProperty = ArrayProperty->Inner;
		}

		if (SegmentIndex == Segments.Num() - 1)
		{
			OutProperty = EffectiveProperty;
			return true;
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(EffectiveProperty))
		{
			if (StructProperty->Struct == nullptr)
			{
				OutError = FString::Printf(TEXT("Property '%s' has no struct type."), *Segment.Name);
				return false;
			}
			ScopeStruct = StructProperty->Struct;
		}
		else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(EffectiveProperty))
		{
			if (ObjectProperty->PropertyClass == nullptr)
			{
				OutError = FString::Printf(TEXT("Property '%s' has no class type."), *Segment.Name);
				return false;
			}
			ScopeStruct = ObjectProperty->PropertyClass;
		}
		else
		{
			OutError = FString::Printf(TEXT("Property '%s' cannot be traversed into."), *Segment.Name);
			return false;
		}
	}

	return false;
}

} // namespace EditorAutomation
