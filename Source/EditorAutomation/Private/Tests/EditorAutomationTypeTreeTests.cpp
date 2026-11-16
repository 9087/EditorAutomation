// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Reflection/EditorAutomationTypeTree.h"

#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Parse one Type string and report why it failed. */
	bool ParseType(FAutomationTestBase& Test, const FString& TypeString, EditorAutomation::FTypeTree& OutTree)
	{
		FString Error;
		if (!EditorAutomation::FTypeTree::TryParse(TypeString, OutTree, Error))
		{
			Test.AddError(FString::Printf(TEXT("Failed to parse '%s': %s"), *TypeString, *Error));
			return false;
		}
		return true;
	}

	/** Parse one Type string and require it to round-trip through ToString unchanged. */
	void TestRoundTrip(FAutomationTestBase& Test, const FString& TypeString)
	{
		EditorAutomation::FTypeTree Tree;
		if (!ParseType(Test, TypeString, Tree))
		{
			return;
		}
		Test.TestEqual(
			*FString::Printf(TEXT("'%s' round-trips"), *TypeString),
			Tree.ToString(),
			TypeString);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationTypeTreeParsingTest,
	"EditorAutomation.TypeTree.Parsing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationTypeTreeParsingTest::RunTest(const FString& Parameters)
{
	// Every canonical form must parse and round-trip.
	const TArray<FString> ValidTypes = {
		TEXT("bool"),
		TEXT("byte"),
		TEXT("int8"),
		TEXT("int16"),
		TEXT("int32"),
		TEXT("int64"),
		TEXT("uint16"),
		TEXT("uint32"),
		TEXT("uint64"),
		TEXT("float"),
		TEXT("double"),
		TEXT("string"),
		TEXT("name"),
		TEXT("text"),
		TEXT("enum(/Script/Engine.ECollisionEnabled)"),
		TEXT("struct(/Script/CoreUObject.Vector)"),
		TEXT("object(/Script/Engine.StaticMesh)"),
		TEXT("class(/Script/Engine.Actor)"),
		TEXT("softobject(/Script/Engine.StaticMesh)"),
		TEXT("softclass(/Script/Engine.Actor)"),
		TEXT("interface(/Script/Engine.BlueprintFunctionLibrary)"),
		TEXT("weakobject(/Script/Engine.Actor)"),
		TEXT("lazyobject(/Script/Engine.Actor)"),
		TEXT("delegate(/Script/Engine.Actor.OnActorHit)"),
		TEXT("multicastdelegate(/Script/Engine.Actor.OnActorHit)"),
		TEXT("array(float)"),
		TEXT("set(name)"),
		TEXT("map(string,int32)"),
		TEXT("array(array(int32))"),
		TEXT("map(string,array(struct(/Script/CoreUObject.Vector)))"),
	};

	for (const FString& TypeString : ValidTypes)
	{
		TestRoundTrip(*this, TypeString);
	}

	// Nested containers expose their children through the typed accessors.
	{
		EditorAutomation::FTypeTree Tree;
		if (ParseType(*this, TEXT("map(string,array(struct(/Script/CoreUObject.Vector)))"), Tree))
		{
			TestTrue(TEXT("map is a container"), Tree.IsContainer());
			TestTrue(TEXT("map key is a string"), Tree.GetKeyType() != nullptr && Tree.GetKeyType()->Kind == EditorAutomation::ETypeKind::String);
			const EditorAutomation::FTypeTree* Element = Tree.GetElementType();
			TestTrue(TEXT("map value is an array"), Element != nullptr && Element->Kind == EditorAutomation::ETypeKind::Array);
			if (Element != nullptr)
			{
				const EditorAutomation::FTypeTree* Struct = Element->GetElementType();
				TestTrue(TEXT("array element is a struct"), Struct != nullptr && Struct->Kind == EditorAutomation::ETypeKind::Struct);
				if (Struct != nullptr)
				{
					TestEqual(TEXT("struct path"), Struct->GetPath(), FString(TEXT("/Script/CoreUObject.Vector")));
				}
			}
		}
	}

	// Malformed or unknown forms must fail with a non-empty reason.
	const TArray<FString> InvalidTypes = {
		TEXT(""),
		TEXT("array"),
		TEXT("array(float"),
		TEXT("array(float,double)"),
		TEXT("map(float)"),
		TEXT("map(float,int32,string)"),
		TEXT("struct()"),
		TEXT("unknown(type)"),
		TEXT("Bool"),
		TEXT("struct(/Script/CoreUObject.Vector))"),
	};

	for (const FString& TypeString : InvalidTypes)
	{
		EditorAutomation::FTypeTree Tree;
		FString Error;
		const bool bParsed = EditorAutomation::FTypeTree::TryParse(TypeString, Tree, Error);
		TestFalse(*FString::Printf(TEXT("'%s' is rejected"), *TypeString), bParsed);
		TestTrue(*FString::Printf(TEXT("'%s' explains why"), *TypeString), !Error.IsEmpty());
	}

	// TryGetPath enforces the expected outermost kind.
	{
		FString Path;
		FString Error;
		TestTrue(
			TEXT("TryGetPath accepts a struct Type"),
			EditorAutomation::FTypeTree::TryGetPath(TEXT("struct(/Script/CoreUObject.Vector)"), EditorAutomation::ETypeKind::Struct, Path, Error));
		TestEqual(TEXT("TryGetPath returns the path"), Path, FString(TEXT("/Script/CoreUObject.Vector")));

		Path.Reset();
		Error.Reset();
		TestFalse(
			TEXT("TryGetPath rejects a non-struct Type"),
			EditorAutomation::FTypeTree::TryGetPath(TEXT("class(/Script/Engine.Actor)"), EditorAutomation::ETypeKind::Struct, Path, Error));
		TestTrue(TEXT("TryGetPath explains the mismatch"), !Error.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationTypeTreeFormattingTest,
	"EditorAutomation.TypeTree.Formatting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationTypeTreeFormattingTest::RunTest(const FString& Parameters)
{
	// Reflected struct properties format from the declared type.
	{
		UScriptStruct* Vector = LoadObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.Vector"));
		if (TestNotNull(TEXT("Vector struct loads"), Vector) && Vector != nullptr)
		{
			FProperty* X = Vector->FindPropertyByName(FName(TEXT("X")));
			if (TestNotNull(TEXT("Vector.X exists"), X) && X != nullptr)
			{
				TestEqual(
					TEXT("Vector.X is double"),
					EditorAutomation::FTypeTree::MakeStringFromProperty(X),
					FString(TEXT("double")));
			}
		}

		UScriptStruct* Transform = LoadObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.Transform"));
		if (TestNotNull(TEXT("Transform struct loads"), Transform) && Transform != nullptr)
		{
			FProperty* Translation = Transform->FindPropertyByName(FName(TEXT("Translation")));
			if (TestNotNull(TEXT("Transform.Translation exists"), Translation) && Translation != nullptr)
			{
				TestEqual(
					TEXT("Transform.Translation is a Vector"),
					EditorAutomation::FTypeTree::MakeStringFromProperty(Translation),
					FString(TEXT("struct(/Script/CoreUObject.Vector)")));
			}
		}
	}

	// UObject/UClass entry points.
	TestEqual(
		TEXT("UObject class formats as class(...)"),
		EditorAutomation::FTypeTree::MakeStringFromClass(UObject::StaticClass()),
		FString(TEXT("class(/Script/CoreUObject.Object)")));

	TestEqual(
		TEXT("UObject default object formats as object(...)"),
		EditorAutomation::FTypeTree::MakeStringFromObject(GetDefault<UObject>()),
		FString(TEXT("object(/Script/CoreUObject.Object)")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS


