// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "Reflection/EditorAutomationTypeTree.h"

#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace EditorAutomation
{

namespace
{
	/** Convert a canonical kind name to its enum; Unknown when the name is not recognised. */
	ETypeKind KindFromName(const FString& Name)
	{
		struct FKindName
		{
			const TCHAR* Name;
			ETypeKind Kind;
		};

		static const FKindName Names[] = {
			{ TEXT("bool"), ETypeKind::Boolean },
			{ TEXT("byte"), ETypeKind::Byte },
			{ TEXT("int8"), ETypeKind::Integer8 },
			{ TEXT("int16"), ETypeKind::Integer16 },
			{ TEXT("int32"), ETypeKind::Integer32 },
			{ TEXT("int64"), ETypeKind::Integer64 },
			{ TEXT("uint16"), ETypeKind::UnsignedInteger16 },
			{ TEXT("uint32"), ETypeKind::UnsignedInteger32 },
			{ TEXT("uint64"), ETypeKind::UnsignedInteger64 },
			{ TEXT("float"), ETypeKind::Float },
			{ TEXT("double"), ETypeKind::Double },
			{ TEXT("string"), ETypeKind::String },
			{ TEXT("name"), ETypeKind::Name },
			{ TEXT("text"), ETypeKind::Text },
			{ TEXT("enum"), ETypeKind::Enum },
			{ TEXT("struct"), ETypeKind::Struct },
			{ TEXT("object"), ETypeKind::Object },
			{ TEXT("class"), ETypeKind::Class },
			{ TEXT("softobject"), ETypeKind::SoftObject },
			{ TEXT("softclass"), ETypeKind::SoftClass },
			{ TEXT("interface"), ETypeKind::Interface },
			{ TEXT("weakobject"), ETypeKind::WeakObject },
			{ TEXT("lazyobject"), ETypeKind::LazyObject },
			{ TEXT("array"), ETypeKind::Array },
			{ TEXT("set"), ETypeKind::Set },
			{ TEXT("map"), ETypeKind::Map },
			{ TEXT("delegate"), ETypeKind::Delegate },
			{ TEXT("multicastdelegate"), ETypeKind::MulticastDelegate },
		};

		for (const FKindName& Entry : Names)
		{
			if (Name.Equals(Entry.Name, ESearchCase::CaseSensitive))
			{
				return Entry.Kind;
			}
		}
		return ETypeKind::Unknown;
	}

	/** True for the kinds whose payload is a single object path. */
	bool IsNamedKind(const ETypeKind Kind)
	{
		switch (Kind)
		{
		case ETypeKind::Enum:
		case ETypeKind::Struct:
		case ETypeKind::Object:
		case ETypeKind::Class:
		case ETypeKind::SoftObject:
		case ETypeKind::SoftClass:
		case ETypeKind::Interface:
		case ETypeKind::WeakObject:
		case ETypeKind::LazyObject:
		case ETypeKind::Delegate:
		case ETypeKind::MulticastDelegate:
			return true;
		default:
			return false;
		}
	}

	/** A kind name: a letter or underscore followed by letters, digits or underscores. */
	bool IsIdentifier(const FString& Text)
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

	/** Split a call's argument list on commas that are not nested inside another call. */
	bool SplitTopLevelArguments(const FString& Inner, TArray<FString>& OutArguments, FString& OutError)
	{
		TArray<FString> Parts;
		FString Current;
		int32 Depth = 0;

		for (int32 Index = 0; Index < Inner.Len(); ++Index)
		{
			const TCHAR Character = Inner[Index];
			if (Character == TEXT('('))
			{
				++Depth;
				Current.AppendChar(Character);
			}
			else if (Character == TEXT(')'))
			{
				--Depth;
				if (Depth < 0)
				{
					OutError = TEXT("Unbalanced ')' in Type.");
					return false;
				}
				Current.AppendChar(Character);
			}
			else if (Character == TEXT(',') && Depth == 0)
			{
				Parts.Add(Current);
				Current.Reset();
			}
			else
			{
				Current.AppendChar(Character);
			}
		}

		if (Depth != 0)
		{
			OutError = TEXT("Unbalanced '(' in Type.");
			return false;
		}

		Parts.Add(Current);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
			if (Part.IsEmpty())
			{
				OutError = TEXT("Type has an empty argument.");
				return false;
			}
			OutArguments.Add(MoveTemp(Part));
		}
		return true;
	}

	/** Recursive descent over one Type string. */
	bool ParseType(const FString& Input, FTypeTree& OutType, FString& OutError)
	{
		const FString Text = Input.TrimStartAndEnd();
		if (Text.IsEmpty())
		{
			OutError = TEXT("Type is empty.");
			return false;
		}

		const int32 OpenIndex = Text.Find(TEXT("("), ESearchCase::CaseSensitive);
		if (OpenIndex == INDEX_NONE)
		{
			const ETypeKind Kind = KindFromName(Text);
			if (!IsIdentifier(Text) || Kind == ETypeKind::Unknown)
			{
				OutError = FString::Printf(TEXT("Unknown Type '%s'."), *Text);
				return false;
			}
			if (Kind == ETypeKind::Array
				|| Kind == ETypeKind::Set
				|| Kind == ETypeKind::Map
				|| IsNamedKind(Kind))
			{
				OutError = FString::Printf(TEXT("Kind '%s' requires arguments."), *Text);
				return false;
			}
			if (FTypeTree::KindName(Kind) != Text)
			{
				OutError = FString::Printf(TEXT("Type '%s' does not take arguments."), *Text);
				return false;
			}
			OutType.Kind = Kind;
			return true;
		}

		if (!Text.EndsWith(TEXT(")")))
		{
			OutError = FString::Printf(TEXT("Type '%s' has an unterminated call."), *Text);
			return false;
		}

		const FString KindName = Text.Left(OpenIndex).TrimStartAndEnd();
		const ETypeKind Kind = KindFromName(KindName);
		if (!IsIdentifier(KindName) || Kind == ETypeKind::Unknown)
		{
			OutError = FString::Printf(TEXT("Unknown Type kind '%s'."), *KindName);
			return false;
		}
		if (!FTypeTree::KindName(Kind).Equals(KindName, ESearchCase::CaseSensitive))
		{
			OutError = FString::Printf(TEXT("Kind '%s' does not take arguments."), *KindName);
			return false;
		}

		const FString Inner = Text.Mid(OpenIndex + 1, Text.Len() - OpenIndex - 2);
		TArray<FString> Arguments;
		if (!SplitTopLevelArguments(Inner, Arguments, OutError))
		{
			return false;
		}

		if (Kind == ETypeKind::Array || Kind == ETypeKind::Set)
		{
			if (Arguments.Num() != 1)
			{
				OutError = FString::Printf(TEXT("Kind '%s' takes one argument, got %d."), *KindName, Arguments.Num());
				return false;
			}

			FTypeTree ElementType;
			if (!ParseType(Arguments[0], ElementType, OutError))
			{
				return false;
			}
			OutType.Kind = Kind;
			OutType.ElementType = MakeShared<FTypeTree>(MoveTemp(ElementType));
			return true;
		}

		if (Kind == ETypeKind::Map)
		{
			if (Arguments.Num() != 2)
			{
				OutError = FString::Printf(TEXT("Kind 'map' takes two arguments, got %d."), Arguments.Num());
				return false;
			}

			FTypeTree KeyType;
			FTypeTree ElementType;
			if (!ParseType(Arguments[0], KeyType, OutError))
			{
				return false;
			}
			if (!ParseType(Arguments[1], ElementType, OutError))
			{
				return false;
			}
			OutType.Kind = Kind;
			OutType.KeyType = MakeShared<FTypeTree>(MoveTemp(KeyType));
			OutType.ElementType = MakeShared<FTypeTree>(MoveTemp(ElementType));
			return true;
		}

		if (!IsNamedKind(Kind))
		{
			OutError = FString::Printf(TEXT("Unknown Type kind '%s'."), *KindName);
			return false;
		}

		if (Arguments.Num() != 1)
		{
			OutError = FString::Printf(TEXT("Kind '%s' takes one path argument, got %d."), *KindName, Arguments.Num());
			return false;
		}

		if (Arguments[0].Contains(TEXT("(")) || Arguments[0].Contains(TEXT(")")))
		{
			OutError = FString::Printf(TEXT("Kind '%s' expects a path, not a nested Type."), *KindName);
			return false;
		}

		OutType.Kind = Kind;
		OutType.Path = Arguments[0];
		return true;
	}
}

FString FTypeTree::KindName(const ETypeKind Kind)
{
	switch (Kind)
	{
	case ETypeKind::Boolean: return TEXT("bool");
	case ETypeKind::Byte: return TEXT("byte");
	case ETypeKind::Integer8: return TEXT("int8");
	case ETypeKind::Integer16: return TEXT("int16");
	case ETypeKind::Integer32: return TEXT("int32");
	case ETypeKind::Integer64: return TEXT("int64");
	case ETypeKind::UnsignedInteger16: return TEXT("uint16");
	case ETypeKind::UnsignedInteger32: return TEXT("uint32");
	case ETypeKind::UnsignedInteger64: return TEXT("uint64");
	case ETypeKind::Float: return TEXT("float");
	case ETypeKind::Double: return TEXT("double");
	case ETypeKind::String: return TEXT("string");
	case ETypeKind::Name: return TEXT("name");
	case ETypeKind::Text: return TEXT("text");
	case ETypeKind::Enum: return TEXT("enum");
	case ETypeKind::Struct: return TEXT("struct");
	case ETypeKind::Object: return TEXT("object");
	case ETypeKind::Class: return TEXT("class");
	case ETypeKind::SoftObject: return TEXT("softobject");
	case ETypeKind::SoftClass: return TEXT("softclass");
	case ETypeKind::Interface: return TEXT("interface");
	case ETypeKind::WeakObject: return TEXT("weakobject");
	case ETypeKind::LazyObject: return TEXT("lazyobject");
	case ETypeKind::Array: return TEXT("array");
	case ETypeKind::Set: return TEXT("set");
	case ETypeKind::Map: return TEXT("map");
	case ETypeKind::Delegate: return TEXT("delegate");
	case ETypeKind::MulticastDelegate: return TEXT("multicastdelegate");
	default: return FString();
	}
}

bool FTypeTree::IsNamed() const
{
	return IsNamedKind(Kind);
}

bool FTypeTree::IsContainer() const
{
	return Kind == ETypeKind::Array
		|| Kind == ETypeKind::Set
		|| Kind == ETypeKind::Map;
}

const FString& FTypeTree::GetPath() const
{
	return Path;
}

const FTypeTree* FTypeTree::GetElementType() const
{
	return ElementType.Get();
}

const FTypeTree* FTypeTree::GetKeyType() const
{
	return KeyType.Get();
}

FString FTypeTree::ToString() const
{
	switch (Kind)
	{
	case ETypeKind::Array:
		return FString::Printf(TEXT("array(%s)"), ElementType.IsValid() ? *ElementType->ToString() : TEXT(""));
	case ETypeKind::Set:
		return FString::Printf(TEXT("set(%s)"), ElementType.IsValid() ? *ElementType->ToString() : TEXT(""));
	case ETypeKind::Map:
		return FString::Printf(
			TEXT("map(%s,%s)"),
			KeyType.IsValid() ? *KeyType->ToString() : TEXT(""),
			ElementType.IsValid() ? *ElementType->ToString() : TEXT(""));
	default:
		if (IsNamed())
		{
			return FString::Printf(TEXT("%s(%s)"), *KindName(Kind), *Path);
		}
		return KindName(Kind);
	}
}

FTypeTree FTypeTree::MakeFromProperty(FProperty* Property)
{
	FTypeTree Type;

	if (CastField<FBoolProperty>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::Boolean;
		return Type;
	}
	if (CastField<FInt8Property>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::Integer8;
		return Type;
	}
	if (CastField<FInt16Property>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::Integer16;
		return Type;
	}
	if (CastField<FIntProperty>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::Integer32;
		return Type;
	}
	if (CastField<FInt64Property>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::Integer64;
		return Type;
	}
	if (CastField<FUInt16Property>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::UnsignedInteger16;
		return Type;
	}
	if (CastField<FUInt32Property>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::UnsignedInteger32;
		return Type;
	}
	if (CastField<FUInt64Property>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::UnsignedInteger64;
		return Type;
	}
	if (CastField<FFloatProperty>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::Float;
		return Type;
	}
	if (CastField<FDoubleProperty>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::Double;
		return Type;
	}
	if (CastField<FStrProperty>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::String;
		return Type;
	}
	if (CastField<FNameProperty>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::Name;
		return Type;
	}
	if (CastField<FTextProperty>(Property) != nullptr)
	{
		Type.Kind = ETypeKind::Text;
		return Type;
	}
	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		if (UEnum* Enum = EnumProperty->GetEnum())
		{
			Type.Kind = ETypeKind::Enum;
			Type.Path = Enum->GetPathName();
		}
		return Type;
	}
	if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		if (UEnum* Enum = ByteProperty->Enum)
		{
			Type.Kind = ETypeKind::Enum;
			Type.Path = Enum->GetPathName();
		}
		else
		{
			Type.Kind = ETypeKind::Byte;
		}
		return Type;
	}
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct != nullptr)
		{
			Type.Kind = ETypeKind::Struct;
			Type.Path = StructProperty->Struct->GetPathName();
		}
		return Type;
	}
	if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
	{
		if (SoftClassProperty->MetaClass != nullptr)
		{
			Type.Kind = ETypeKind::SoftClass;
			Type.Path = SoftClassProperty->MetaClass->GetPathName();
		}
		return Type;
	}
	if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
	{
		if (SoftObjectProperty->PropertyClass != nullptr)
		{
			Type.Kind = ETypeKind::SoftObject;
			Type.Path = SoftObjectProperty->PropertyClass->GetPathName();
		}
		return Type;
	}
	if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
	{
		if (ClassProperty->MetaClass != nullptr)
		{
			Type.Kind = ETypeKind::Class;
			Type.Path = ClassProperty->MetaClass->GetPathName();
		}
		return Type;
	}
	if (FWeakObjectProperty* WeakObjectProperty = CastField<FWeakObjectProperty>(Property))
	{
		if (WeakObjectProperty->PropertyClass != nullptr)
		{
			Type.Kind = ETypeKind::WeakObject;
			Type.Path = WeakObjectProperty->PropertyClass->GetPathName();
		}
		return Type;
	}
	if (FLazyObjectProperty* LazyObjectProperty = CastField<FLazyObjectProperty>(Property))
	{
		if (LazyObjectProperty->PropertyClass != nullptr)
		{
			Type.Kind = ETypeKind::LazyObject;
			Type.Path = LazyObjectProperty->PropertyClass->GetPathName();
		}
		return Type;
	}
	if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
	{
		if (ObjectProperty->PropertyClass != nullptr)
		{
			Type.Kind = ETypeKind::Object;
			Type.Path = ObjectProperty->PropertyClass->GetPathName();
		}
		return Type;
	}
	if (FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(Property))
	{
		if (InterfaceProperty->InterfaceClass != nullptr)
		{
			Type.Kind = ETypeKind::Interface;
			Type.Path = InterfaceProperty->InterfaceClass->GetPathName();
		}
		return Type;
	}
	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		Type.Kind = ETypeKind::Array;
		Type.ElementType = MakeShared<FTypeTree>(MakeFromProperty(ArrayProperty->Inner));
		return Type;
	}
	if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		Type.Kind = ETypeKind::Set;
		Type.ElementType = MakeShared<FTypeTree>(MakeFromProperty(SetProperty->ElementProp));
		return Type;
	}
	if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		Type.Kind = ETypeKind::Map;
		Type.KeyType = MakeShared<FTypeTree>(MakeFromProperty(MapProperty->KeyProp));
		Type.ElementType = MakeShared<FTypeTree>(MakeFromProperty(MapProperty->ValueProp));
		return Type;
	}
	if (FMulticastDelegateProperty* MulticastDelegateProperty = CastField<FMulticastDelegateProperty>(Property))
	{
		if (MulticastDelegateProperty->SignatureFunction != nullptr)
		{
			Type.Kind = ETypeKind::MulticastDelegate;
			Type.Path = MulticastDelegateProperty->SignatureFunction->GetPathName();
		}
		return Type;
	}
	if (FDelegateProperty* DelegateProperty = CastField<FDelegateProperty>(Property))
	{
		if (DelegateProperty->SignatureFunction != nullptr)
		{
			Type.Kind = ETypeKind::Delegate;
			Type.Path = DelegateProperty->SignatureFunction->GetPathName();
		}
		return Type;
	}

	return Type;
}

FString FTypeTree::MakeStringFromProperty(FProperty* Property)
{
	return MakeFromProperty(Property).ToString();
}

FTypeTree FTypeTree::MakeFromObject(const UObject* Object)
{
	FTypeTree Type;
	if (Object != nullptr)
	{
		Type.Kind = ETypeKind::Object;
		Type.Path = Object->GetClass()->GetPathName();
	}
	return Type;
}

FTypeTree FTypeTree::MakeFromClass(const UClass* Class)
{
	FTypeTree Type;
	if (Class != nullptr)
	{
		Type.Kind = ETypeKind::Class;
		Type.Path = Class->GetPathName();
	}
	return Type;
}

FString FTypeTree::MakeStringFromObject(const UObject* Object)
{
	return MakeFromObject(Object).ToString();
}

FString FTypeTree::MakeStringFromClass(const UClass* Class)
{
	return MakeFromClass(Class).ToString();
}

bool FTypeTree::TryParse(const FString& TypeString, FTypeTree& OutType, FString& OutError)
{
	OutType = FTypeTree();
	OutError.Reset();
	return ParseType(TypeString, OutType, OutError);
}

bool FTypeTree::TryGetPath(
	const FString& TypeString,
	const ETypeKind ExpectedKind,
	FString& OutPath,
	FString& OutError)
{
	FTypeTree Type;
	if (!TryParse(TypeString, Type, OutError))
	{
		return false;
	}

	if (Type.Kind != ExpectedKind)
	{
		OutError = FString::Printf(
			TEXT("Expected %s(...) but got %s(...)."),
			*KindName(ExpectedKind),
			*KindName(Type.Kind));
		return false;
	}

	OutPath = Type.Path;
	return true;
}

} // namespace EditorAutomation

