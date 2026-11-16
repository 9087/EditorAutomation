// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FProperty;
class UObject;
class UClass;

namespace EditorAutomation
{
/** Resolves "A.B[2].C" property paths, both against a live object and against a class schema. */
class FPropertyPath
{
public:
	/** One path element: a property name plus an optional array index. */
	struct FSegment
	{
		FString Name;
		int32 Index = INDEX_NONE;
	};

	/** Parse a property path into its segments. */
	static bool Parse(const FString& Path, TArray<FSegment>& OutSegments, FString& OutError);

	/** Resolve a path against a live object down to the leaf property and its value address. */
	static bool Resolve(
		UObject* Object,
		const TArray<FSegment>& Segments,
		FProperty*& OutProperty,
		void*& OutValueAddress,
		FString& OutError);

	/** Resolve a path against a class schema only (no instance needed), for type lookups. */
	static bool ResolveType(
		UClass* Class,
		const TArray<FSegment>& Segments,
		FProperty*& OutProperty,
		FString& OutError);
};
}
