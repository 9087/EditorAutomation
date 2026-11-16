// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FProperty;
class UObject;
class UClass;

namespace EditorAutomation
{

/** The kind of a reflected or parsed Type; it says which payload accessor is meaningful. */
enum class ETypeKind : uint8
{
	Unknown,
	Boolean,
	Byte,
	Integer8,
	Integer16,
	Integer32,
	Integer64,
	UnsignedInteger16,
	UnsignedInteger32,
	UnsignedInteger64,
	Float,
	Double,
	String,
	Name,
	Text,
	Enum,
	Struct,
	Object,
	Class,
	SoftObject,
	SoftClass,
	Interface,
	WeakObject,
	LazyObject,
	Array,
	Set,
	Map,
	Delegate,
	MulticastDelegate
};

/**
 * One Type tree, in C++-only form: Kind plus the payload that Kind implies.
 *
 * Named kinds (Enum, Struct, Object, Class, 鈥? carry Path; containers carry ElementType (Array/Set element or
 * Map value) and KeyType (Map key); primitives carry neither. Guards and typed accessors say which payload is
 * present instead of exposing a raw union.
 *
 * This type never crosses the wire: RC-facing results carry the canonical Type string, produced by ToString().
 */
struct FTypeTree
{
	/** Which payload the rest of this value carries. */
	ETypeKind Kind = ETypeKind::Unknown;

	/** Object path for named kinds. */
	FString Path;

	/** Array/Set element, or Map value. */
	TSharedPtr<FTypeTree> ElementType;

	/** Map key. */
	TSharedPtr<FTypeTree> KeyType;

	/** Describe the declared type of one property. */
	static FTypeTree MakeFromProperty(FProperty* Property);

	/** Describe the declared type of one property directly as its canonical wire string. */
	static FString MakeStringFromProperty(FProperty* Property);

	/** Describe one object's runtime class as object(<class path>). */
	static FTypeTree MakeFromObject(const UObject* Object);

	/** Describe one object's runtime class directly as its canonical wire string. */
	static FString MakeStringFromObject(const UObject* Object);

	/** Describe one class as class(<class path>). */
	static FTypeTree MakeFromClass(const UClass* Class);

	/** Describe one class directly as its canonical wire string. */
	static FString MakeStringFromClass(const UClass* Class);

	/** Parse a canonical Type string. */
	static bool TryParse(const FString& TypeString, FTypeTree& OutType, FString& OutError);

	/** Parse a Type string and require a specific outermost kind. */
	static bool TryGetPath(
		const FString& TypeString,
		const ETypeKind ExpectedKind,
		FString& OutPath,
		FString& OutError);

	/** The canonical name of one kind, as used in Type strings ("array", "struct", "double", 鈥?. */
	static FString KindName(const ETypeKind Kind);

	/** True for Enum/Struct/Object/Class/SoftObject/SoftClass/Interface/WeakObject/LazyObject/Delegate kinds. */
	bool IsNamed() const;

	/** True for Array/Set/Map. */
	bool IsContainer() const;

	/** The path of a named type; empty for primitives and containers. */
	const FString& GetPath() const;

	/** The element of an Array/Set or the value of a Map; null otherwise. */
	const FTypeTree* GetElementType() const;

	/** The key of a Map; null otherwise. */
	const FTypeTree* GetKeyType() const;

	/** The canonical wire string of this Type. */
	FString ToString() const;
};

} // namespace EditorAutomation

