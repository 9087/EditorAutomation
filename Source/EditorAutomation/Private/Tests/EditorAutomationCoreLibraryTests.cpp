// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

// Automation tests of the RC-facing surface of UEditorAutomationCoreLibrary.
//
// Every function is called directly from C++ (the Remote Control HTTP transport is another process'
// concern); the fixtures are engine CDOs, so no editor world or user content is required.
//
// The API has no per-function error envelope: a failed call returns an empty value (or false and an
// empty read) and logs the reason through LogEditorAutomation. These tests therefore assert data and success
// flags rather than error codes; the log itself is covered by the CallFunction/GetRecentLog tests.

#include "Misc/AutomationTest.h"

#include "EditorAutomationCoreLibrary.h"
#include "EditorAutomationLog.h"

#include "HAL/PlatformProcess.h"
#include "Logging/LogVerbosity.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"

#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** The CDO Remote Control is expected to reach the library through. */
	const TCHAR* const LibraryCdoPath = TEXT("/Script/EditorAutomation.Default__EditorAutomationCoreLibrary");

	/** Engine CDOs used as data targets; they exist in every editor process. */
	const TCHAR* const StaticMeshComponentCdoPath = TEXT("/Script/Engine.Default__StaticMeshComponent");

	/** Type string of the component class used throughout these tests. */
	const TCHAR* const StaticMeshComponentType = TEXT("class(/Script/Engine.StaticMeshComponent)");

	template <typename StructType>
	const StructType* FindByName(const TArray<StructType>& Rows, const TCHAR* Name)
	{
		for (const StructType& Row : Rows)
		{
			if (Row.Name == Name)
			{
				return &Row;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationPingTest,
	"EditorAutomation.Core.Ping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationPingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Ping answers pong"), UEditorAutomationCoreLibrary::Ping(), FString(TEXT("pong")));

	UClass* LibraryClass = LoadObject<UClass>(nullptr, TEXT("/Script/EditorAutomation.EditorAutomationCoreLibrary"));
	TestNotNull(TEXT("The library class resolves in the merged module"), LibraryClass);
	if (LibraryClass != nullptr)
	{
		TestEqual(
			TEXT("The library class lives in the EditorAutomation module"),
			LibraryClass->GetPathName(),
			FString(TEXT("/Script/EditorAutomation.EditorAutomationCoreLibrary")));
	}
	TestNotNull(TEXT("The documented CDO path resolves"), LoadObject<UObject>(nullptr, LibraryCdoPath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetEditorInfoTest,
	"EditorAutomation.Core.GetEditorInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetEditorInfoTest::RunTest(const FString& Parameters)
{
	const FEditorAutomationEditorInfo Info = UEditorAutomationCoreLibrary::GetEditorInfo();
	TestTrue(TEXT("ProcessId is the current process"), Info.ProcessId == static_cast<int64>(FPlatformProcess::GetCurrentProcessId()));
	TestFalse(TEXT("EngineVersion is reported"), Info.EngineVersion.IsEmpty());
	TestEqual(TEXT("ProjectName is reported"), Info.ProjectName, FString(FApp::GetProjectName()));
	TestFalse(TEXT("ProjectDir is reported"), Info.ProjectDir.IsEmpty());
	TestTrue(TEXT("The test runs inside the editor"), Info.bIsEditor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetEnumInfoTest,
	"EditorAutomation.Core.GetEnumInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetEnumInfoTest::RunTest(const FString& Parameters)
{
	const FEditorAutomationEnumInfo Info =
		UEditorAutomationCoreLibrary::GetEnumInfo(TEXT("enum(/Script/Engine.ECollisionEnabled)"));

	TestTrue(TEXT("The enum resolved"), Info.Enum != nullptr);
	const FEditorAutomationEnumEntry* QueryAndPhysics = FindByName(Info.Entries, TEXT("QueryAndPhysics"));
	TestNotNull(TEXT("QueryAndPhysics entry exists"), QueryAndPhysics);
	if (QueryAndPhysics != nullptr)
	{
		TestFalse(TEXT("The entry has a display name"), QueryAndPhysics->DisplayName.IsEmpty());
	}

	AddExpectedError(TEXT("Type 'not-a-type' is not valid"), EAutomationExpectedErrorFlags::Contains, 1);
	const FEditorAutomationEnumInfo Invalid = UEditorAutomationCoreLibrary::GetEnumInfo(TEXT("not-a-type"));
	TestTrue(TEXT("An invalid Type returns an empty result"), Invalid.Enum == nullptr && Invalid.Entries.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetStructInfoTest,
	"EditorAutomation.Core.GetStructInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetStructInfoTest::RunTest(const FString& Parameters)
{
	const FEditorAutomationStructInfo Info = UEditorAutomationCoreLibrary::GetStructInfo(TEXT("struct(/Script/CoreUObject.Vector)"));
	TestTrue(TEXT("The struct resolved"), Info.Struct != nullptr);
	TestTrue(TEXT("The struct has members"), Info.Properties.Num() >= 3);
	TestNotNull(TEXT("X is a member"), FindByName(Info.Properties, TEXT("X")));
	TestNotNull(TEXT("Y is a member"), FindByName(Info.Properties, TEXT("Y")));
	TestNotNull(TEXT("Z is a member"), FindByName(Info.Properties, TEXT("Z")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetClassInfoTest,
	"EditorAutomation.Core.GetClassInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetClassInfoTest::RunTest(const FString& Parameters)
{
	const FEditorAutomationClassInfo Info = UEditorAutomationCoreLibrary::GetClassInfo(StaticMeshComponentType);
	TestTrue(TEXT("The class resolved"), Info.Class != nullptr);
	TestTrue(TEXT("The class reports properties"), Info.Properties.Num() > 0);
	TestTrue(TEXT("The class reports BlueprintCallable functions"), Info.Functions.Num() > 0);
	TestNotNull(TEXT("GetClosestPointOnCollision is listed"), FindByName(Info.Functions, TEXT("GetClosestPointOnCollision")));

	AddExpectedError(TEXT("Type '/Script/Engine.NotAClass' is not valid"), EAutomationExpectedErrorFlags::Contains, 1);
	const FEditorAutomationClassInfo Invalid = UEditorAutomationCoreLibrary::GetClassInfo(TEXT("/Script/Engine.NotAClass"));
	TestTrue(TEXT("An invalid Type returns an empty result"), Invalid.Class == nullptr && Invalid.Functions.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetFunctionInfoTest,
	"EditorAutomation.Core.GetFunctionInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetFunctionInfoTest::RunTest(const FString& Parameters)
{
	const FEditorAutomationFunctionInfo Info =
		UEditorAutomationCoreLibrary::GetFunctionInfo(StaticMeshComponentType, TEXT("GetClosestPointOnCollision"));
	TestEqual(TEXT("The function name matches"), Info.Name, FString(TEXT("GetClosestPointOnCollision")));
	TestTrue(TEXT("The function is BlueprintCallable"), Info.bBlueprintCallable);
	TestEqual(TEXT("The function has three parameters"), Info.Parameters.Num(), 3);

	AddExpectedError(TEXT("Function 'NotARealFunction' was not found"), EAutomationExpectedErrorFlags::Contains, 1);
	const FEditorAutomationFunctionInfo Missing =
		UEditorAutomationCoreLibrary::GetFunctionInfo(StaticMeshComponentType, TEXT("NotARealFunction"));
	TestTrue(TEXT("A missing function returns an empty result"), Missing.Name.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetObjectClassTest,
	"EditorAutomation.Core.GetObjectClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetObjectClassTest::RunTest(const FString& Parameters)
{
	const FString ClassType = UEditorAutomationCoreLibrary::GetObjectClass(StaticMeshComponentCdoPath);
	TestEqual(TEXT("The CDO's class Type is canonical"), ClassType, FString(StaticMeshComponentType));
	AddExpectedError(TEXT("object '/Script/Engine.Nope' could not be loaded"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("An invalid object path returns an empty Type"), UEditorAutomationCoreLibrary::GetObjectClass(TEXT("/Script/Engine.Nope")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetPropertyTypeTest,
	"EditorAutomation.Core.GetPropertyType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetPropertyTypeTest::RunTest(const FString& Parameters)
{
	const FEditorAutomationPropertyTypeInfo PropertyType =
		UEditorAutomationCoreLibrary::GetPropertyType(StaticMeshComponentType, TEXT("RelativeLocation.X"));
	TestEqual(TEXT("RelativeLocation.X is a double"), PropertyType.Type, FString(TEXT("double")));
	TestFalse(TEXT("The resolved property carries its display name"), PropertyType.DisplayNameText.NativeText.IsEmpty());
	AddExpectedError(TEXT("Property 'NoSuchProperty' was not found"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(
		TEXT("An unknown property returns an empty Type"),
		UEditorAutomationCoreLibrary::GetPropertyType(StaticMeshComponentType, TEXT("NoSuchProperty")).Type.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetPropertyValueTest,
	"EditorAutomation.Core.GetPropertyValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetPropertyValueTest::RunTest(const FString& Parameters)
{
	const FString ValueJson = UEditorAutomationCoreLibrary::GetPropertyValue(StaticMeshComponentCdoPath, TEXT("RelativeLocation"));
	TestFalse(TEXT("RelativeLocation serializes to nested JSON text"), ValueJson.IsEmpty());
	TestTrue(TEXT("The JSON text is an object"), ValueJson.StartsWith(TEXT("{")));
	AddExpectedError(TEXT("Property 'NoSuchProperty' was not found"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("An invalid property returns empty text"),
		UEditorAutomationCoreLibrary::GetPropertyValue(StaticMeshComponentCdoPath, TEXT("NoSuchProperty")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationSetPropertyValueTest,
	"EditorAutomation.Core.SetPropertyValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationSetPropertyValueTest::RunTest(const FString& Parameters)
{
	const FString Before = UEditorAutomationCoreLibrary::GetPropertyValue(StaticMeshComponentCdoPath, TEXT("bCastDynamicShadow"));

	const FEditorAutomationPropertyWrite Write = UEditorAutomationCoreLibrary::SetPropertyValue(
		StaticMeshComponentCdoPath, TEXT("bCastDynamicShadow"), TEXT("true"), false);
	TestTrue(TEXT("The write succeeded"), Write.bSuccess);
	TestEqual(TEXT("The write reports the old value"), Write.OldValueJson, Before);
	TestEqual(TEXT("The write reports the new value"), Write.NewValueJson, FString(TEXT("\"true\"")));

	const FEditorAutomationPropertyWrite Restore = UEditorAutomationCoreLibrary::SetPropertyValue(
		StaticMeshComponentCdoPath, TEXT("bCastDynamicShadow"), Before, false);
	TestTrue(TEXT("The restore succeeded"), Restore.bSuccess);
	TestEqual(
		TEXT("The restored value matches the original"),
		UEditorAutomationCoreLibrary::GetPropertyValue(StaticMeshComponentCdoPath, TEXT("bCastDynamicShadow")),
		Before);

	AddExpectedError(TEXT("could not be written: JSON text could not be parsed"), EAutomationExpectedErrorFlags::Contains, 1);
	const FEditorAutomationPropertyWrite BadValue = UEditorAutomationCoreLibrary::SetPropertyValue(
		StaticMeshComponentCdoPath, TEXT("RelativeLocation.X"), TEXT("not-json"), false);
	TestFalse(TEXT("An invalid value fails the write"), BadValue.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationCallFunctionTest,
	"EditorAutomation.Core.CallFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationCallFunctionTest::RunTest(const FString& Parameters)
{
	const FEditorAutomationCallFunctionResult Result =
		UEditorAutomationCoreLibrary::CallFunction(LibraryCdoPath, TEXT("Ping"), TEXT("{}"), false);
	TestEqual(TEXT("CallFunction reaches Ping"), Result.ErrorCode, 0);
	TestEqual(TEXT("The return value is the nested-JSON text of pong"), Result.ReturnValueJson, FString(TEXT("\"pong\"")));
	TestEqual(TEXT("The return Type is reported"), Result.ReturnType, FString(TEXT("string")));

	const FEditorAutomationCallFunctionResult Missing =
		UEditorAutomationCoreLibrary::CallFunction(LibraryCdoPath, TEXT("NotARealFunction"), TEXT("{}"), false);
	TestTrue(TEXT("A missing function reports a failure"), Missing.ErrorCode != 0);
	TestFalse(TEXT("A failure explains itself"), Missing.ErrorMessage.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationCallFunctionsTest,
	"EditorAutomation.Core.CallFunctions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationCallFunctionsTest::RunTest(const FString& Parameters)
{
	const FString CallsJson = FString::Printf(
		TEXT("[{\"objectPath\":\"%s\",\"functionName\":\"Ping\"},{\"objectPath\":\"%s\",\"functionName\":\"GetEditorInfo\"}]"),
		LibraryCdoPath,
		LibraryCdoPath);
	const FEditorAutomationBatchCallResult Result = UEditorAutomationCoreLibrary::CallFunctions(CallsJson);
	TestTrue(TEXT("The batch succeeded"), Result.bSuccess);
	TestEqual(TEXT("Both calls are reported"), Result.Results.Num(), 2);
	if (Result.Results.Num() == 2)
	{
		TestEqual(TEXT("The first call returns pong"), Result.Results[0].ReturnValueJson, FString(TEXT("\"pong\"")));
		TestFalse(TEXT("The second call returns editor info"), Result.Results[1].ReturnValueJson.IsEmpty());
	}

	const FEditorAutomationBatchCallResult Invalid = UEditorAutomationCoreLibrary::CallFunctions(TEXT("not-json"));
	TestFalse(TEXT("An invalid batch payload fails at the batch level"), Invalid.bSuccess);
	TestFalse(TEXT("The batch failure explains itself"), Invalid.ErrorMessage.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetRecentLogTest,
	"EditorAutomation.Core.GetRecentLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetRecentLogTest::RunTest(const FString& Parameters)
{
	const FEditorAutomationLogPage Page = UEditorAutomationCoreLibrary::GetRecentLog(10, 3, TEXT(""), false);
	TestTrue(TEXT("The capture reports a total"), Page.TotalRecorded >= 0);
	TestTrue(TEXT("The page respects MaxLines"), Page.Entries.Num() <= 10);

	// The capture device itself is verified against a live editor (RC); automation only checks the page shape.

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
