// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Math/Transform.h"

#include "Reflection/EditorAutomationPropertyPath.h"
#include "Reflection/EditorAutomationTypeTree.h"
#include "Serialization/EditorAutomationValueJson.h"

#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationValueJsonTest,
	"EditorAutomation.ValueJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationValueJsonTest::RunTest(const FString& Parameters)
{
	UScriptStruct* VectorStruct = LoadObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.Vector"));
	if (!TestNotNull(TEXT("Vector struct loads"), VectorStruct) || VectorStruct == nullptr)
	{
		return true;
	}

	FProperty* XProperty = VectorStruct->FindPropertyByName(FName(TEXT("X")));
	if (!TestNotNull(TEXT("Vector.X exists"), XProperty) || XProperty == nullptr)
	{
		return true;
	}

	// Scalar round-trip through nested JSON.
	{
		FVector Value(1.5, 2.5, 3.5);
		void* XAddress = XProperty->ContainerPtrToValuePtr<void>(&Value);

		FString Json;
		FString Error;
		TestTrue(TEXT("Vector.X serializes"), EditorAutomation::FValueJson::ToJsonString(XProperty, XAddress, Json, Error));
		TestEqual(TEXT("Vector.X JSON"), Json, FString(TEXT("\"1.5\"")));

		const bool bDeserialized = EditorAutomation::FValueJson::FromJsonString(TEXT("\"4.25\""), XProperty, XAddress, Error);
		TestTrue(*FString::Printf(TEXT("Vector.X deserializes (%s)"), *Error), bDeserialized);
		TestEqual(TEXT("Vector.X updated"), Value.X, 4.25);
	}

	// Struct round-trip through nested JSON.
	{
		UScriptStruct* TransformStruct = LoadObject<UScriptStruct>(nullptr, TEXT("/Script/CoreUObject.Transform"));
		FProperty* TranslationProperty = TransformStruct != nullptr
			? TransformStruct->FindPropertyByName(FName(TEXT("Translation")))
			: nullptr;

		if (TestNotNull(TEXT("Transform.Translation exists"), TranslationProperty) && TranslationProperty != nullptr)
		{
			FTransform Value = FTransform::Identity;
			void* TranslationAddress = TranslationProperty->ContainerPtrToValuePtr<void>(&Value);

			FString Json;
			FString Error;
			TestTrue(TEXT("Transform.Translation serializes"), EditorAutomation::FValueJson::ToJsonString(TranslationProperty, TranslationAddress, Json, Error));

			TestTrue(
				TEXT("Transform.Translation deserializes"),
				EditorAutomation::FValueJson::FromJsonString(TEXT("{\"X\":\"1\",\"Y\":\"2\",\"Z\":\"3\"}"), TranslationProperty, TranslationAddress, Error));
			const FVector Translation = Value.GetTranslation();
			TestEqual(TEXT("Translation.X"), Translation.X, 1.0);
			TestEqual(TEXT("Translation.Y"), Translation.Y, 2.0);
			TestEqual(TEXT("Translation.Z"), Translation.Z, 3.0);
		}
	}

	// Property path parsing and schema type resolution.
	{
		TArray<EditorAutomation::FPropertyPath::FSegment> Segments;
		FString Error;
		TestTrue(TEXT("Path parses"), EditorAutomation::FPropertyPath::Parse(TEXT("RelativeLocation.X"), Segments, Error));
		TestEqual(TEXT("Path segment count"), Segments.Num(), 2);

		TArray<EditorAutomation::FPropertyPath::FSegment> BadSegments;
		TestFalse(TEXT("Trailing dot is rejected"), EditorAutomation::FPropertyPath::Parse(TEXT("RelativeLocation."), BadSegments, Error));

		UClass* Class = LoadObject<UClass>(nullptr, TEXT("/Script/Engine.StaticMeshComponent"));
		if (TestNotNull(TEXT("StaticMeshComponent class loads"), Class) && Class != nullptr)
		{
			FProperty* Property = nullptr;
			TestTrue(TEXT("Schema path resolves"), EditorAutomation::FPropertyPath::ResolveType(Class, Segments, Property, Error));
			if (Property != nullptr)
			{
				TestEqual(
					TEXT("RelativeLocation.X type"),
					EditorAutomation::FTypeTree::MakeStringFromProperty(Property),
					FString(TEXT("double")));
			}
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
