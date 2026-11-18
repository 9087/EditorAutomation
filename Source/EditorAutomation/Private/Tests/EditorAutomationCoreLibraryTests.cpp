// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

// Automation tests of the RC-facing surface of UEditorAutomationCoreLibrary.
//
// Every function is called directly from C++ (the Remote Control HTTP transport is another process'
// concern); the fixtures are engine CDOs, so no editor world or user content is required.
//
// Every RC-facing function returns FEditorAutomationResult (success/error/hint) and reports its data
// through a trailing out parameter. CallFunction/CallFunctions keep their own result envelopes.

#include "Misc/AutomationTest.h"

#include "EditorAutomationCoreLibrary.h"

#include "HAL/PlatformProcess.h"
#include "Logging/LogVerbosity.h"
#include "Misc/App.h"
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
	FString Value;
	const FEditorAutomationResult Result = UEditorAutomationCoreLibrary::Ping(Value);
	TestTrue(TEXT("Ping succeeds"), Result.bSuccess);
	TestEqual(TEXT("Ping answers pong"), Value, FString(TEXT("pong")));

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
	FEditorAutomationEditorInfo Info;
	const FEditorAutomationResult Result = UEditorAutomationCoreLibrary::GetEditorInfo(Info);
	TestTrue(TEXT("GetEditorInfo succeeds"), Result.bSuccess);
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
	FEditorAutomationEnumInfo Info;
	const FEditorAutomationResult Result =
		UEditorAutomationCoreLibrary::GetEnumInfo(TEXT("enum(/Script/Engine.ECollisionEnabled)"), Info);

	TestTrue(TEXT("The enum resolved"), Result.bSuccess);
	TestTrue(TEXT("The enum object is reported"), Info.Enum != nullptr);
	const FEditorAutomationEnumEntry* QueryAndPhysics = FindByName(Info.Entries, TEXT("QueryAndPhysics"));
	TestNotNull(TEXT("QueryAndPhysics entry exists"), QueryAndPhysics);
	if (QueryAndPhysics != nullptr)
	{
		TestFalse(TEXT("The entry has a display name"), QueryAndPhysics->DisplayName.IsEmpty());
	}

	FEditorAutomationEnumInfo InvalidInfo;
	const FEditorAutomationResult Invalid =
		UEditorAutomationCoreLibrary::GetEnumInfo(TEXT("not-a-type"), InvalidInfo);
	TestFalse(TEXT("An invalid Type fails"), Invalid.bSuccess);
	TestTrue(TEXT("The failure names the argument problem"), Invalid.ErrorCode == EEditorAutomationResultCode::InvalidArgument);
	TestFalse(TEXT("The failure explains itself"), Invalid.ErrorMessage.IsEmpty());
	TestFalse(TEXT("The failure suggests a fix"), Invalid.Hint.IsEmpty());
	TestTrue(TEXT("A failed call leaves the payload empty"), InvalidInfo.Enum == nullptr && InvalidInfo.Entries.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetStructInfoTest,
	"EditorAutomation.Core.GetStructInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetStructInfoTest::RunTest(const FString& Parameters)
{
	FEditorAutomationStructInfo Info;
	const FEditorAutomationResult Result =
		UEditorAutomationCoreLibrary::GetStructInfo(TEXT("struct(/Script/CoreUObject.Vector)"), Info);
	TestTrue(TEXT("The struct resolved"), Result.bSuccess);
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
	FEditorAutomationClassInfo Info;
	const FEditorAutomationResult Result =
		UEditorAutomationCoreLibrary::GetClassInfo(StaticMeshComponentType, Info);
	TestTrue(TEXT("The class resolved"), Result.bSuccess);
	TestTrue(TEXT("The class reports properties"), Info.Properties.Num() > 0);
	TestTrue(TEXT("The class reports BlueprintCallable functions"), Info.Functions.Num() > 0);
	TestNotNull(TEXT("GetClosestPointOnCollision is listed"), FindByName(Info.Functions, TEXT("GetClosestPointOnCollision")));

	FEditorAutomationClassInfo InvalidInfo;
	const FEditorAutomationResult Invalid =
		UEditorAutomationCoreLibrary::GetClassInfo(TEXT("/Script/Engine.NotAClass"), InvalidInfo);
	TestFalse(TEXT("An invalid Type fails"), Invalid.bSuccess);
	TestFalse(TEXT("The failure explains itself"), Invalid.ErrorMessage.IsEmpty());
	TestTrue(TEXT("A failed call leaves the payload empty"), InvalidInfo.Class == nullptr && InvalidInfo.Functions.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetFunctionInfoTest,
	"EditorAutomation.Core.GetFunctionInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetFunctionInfoTest::RunTest(const FString& Parameters)
{
	FEditorAutomationFunctionInfo Info;
	const FEditorAutomationResult Result = UEditorAutomationCoreLibrary::GetFunctionInfo(
		StaticMeshComponentType, TEXT("GetClosestPointOnCollision"), Info);
	TestTrue(TEXT("The function resolved"), Result.bSuccess);
	TestEqual(TEXT("The function name matches"), Info.Name, FString(TEXT("GetClosestPointOnCollision")));
	TestTrue(TEXT("The function is BlueprintCallable"), Info.bBlueprintCallable);
	TestEqual(TEXT("The function has three parameters"), Info.Parameters.Num(), 3);

	FEditorAutomationFunctionInfo MissingInfo;
	const FEditorAutomationResult Missing = UEditorAutomationCoreLibrary::GetFunctionInfo(
		StaticMeshComponentType, TEXT("NotARealFunction"), MissingInfo);
	TestFalse(TEXT("A missing function fails"), Missing.bSuccess);
	TestTrue(TEXT("The missing function is reported as not found"), Missing.ErrorCode == EEditorAutomationResultCode::NotFound);
	TestTrue(TEXT("A failed call leaves the payload empty"), MissingInfo.Name.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetObjectClassTest,
	"EditorAutomation.Core.GetObjectClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetObjectClassTest::RunTest(const FString& Parameters)
{
	FString ClassType;
	const FEditorAutomationResult Result =
		UEditorAutomationCoreLibrary::GetObjectClass(StaticMeshComponentCdoPath, ClassType);
	TestTrue(TEXT("GetObjectClass succeeds"), Result.bSuccess);
	TestEqual(TEXT("The CDO's class Type is canonical"), ClassType, FString(StaticMeshComponentType));

	FString MissingClassType;
	const FEditorAutomationResult Missing =
		UEditorAutomationCoreLibrary::GetObjectClass(TEXT("/Script/Engine.Nope"), MissingClassType);
	TestFalse(TEXT("An invalid object path fails"), Missing.bSuccess);
	TestTrue(TEXT("The failure explains itself"), !Missing.ErrorMessage.IsEmpty());
	TestTrue(TEXT("A failed call leaves the payload empty"), MissingClassType.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetPropertyTypeTest,
	"EditorAutomation.Core.GetPropertyType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetPropertyTypeTest::RunTest(const FString& Parameters)
{
	FEditorAutomationPropertyTypeInfo PropertyType;
	const FEditorAutomationResult Result =
		UEditorAutomationCoreLibrary::GetPropertyType(StaticMeshComponentType, TEXT("RelativeLocation.X"), PropertyType);
	TestTrue(TEXT("GetPropertyType succeeds"), Result.bSuccess);
	TestEqual(TEXT("RelativeLocation.X is a double"), PropertyType.Type, FString(TEXT("double")));
	TestFalse(TEXT("The resolved property carries its display name"), PropertyType.DisplayNameText.NativeText.IsEmpty());

	FEditorAutomationPropertyTypeInfo MissingType;
	const FEditorAutomationResult Missing =
		UEditorAutomationCoreLibrary::GetPropertyType(StaticMeshComponentType, TEXT("NoSuchProperty"), MissingType);
	TestFalse(TEXT("An unknown property fails"), Missing.bSuccess);
	TestTrue(TEXT("The failure explains itself"), !Missing.ErrorMessage.IsEmpty());
	TestTrue(TEXT("A failed call leaves the payload empty"), MissingType.Type.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationGetPropertyValueTest,
	"EditorAutomation.Core.GetPropertyValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationGetPropertyValueTest::RunTest(const FString& Parameters)
{
	FString ValueJson;
	const FEditorAutomationResult Result =
		UEditorAutomationCoreLibrary::GetPropertyValue(StaticMeshComponentCdoPath, TEXT("RelativeLocation"), ValueJson);
	TestTrue(TEXT("GetPropertyValue succeeds"), Result.bSuccess);
	TestFalse(TEXT("RelativeLocation serializes to nested JSON text"), ValueJson.IsEmpty());
	TestTrue(TEXT("The JSON text is an object"), ValueJson.StartsWith(TEXT("{")));

	FString MissingValue;
	const FEditorAutomationResult Missing =
		UEditorAutomationCoreLibrary::GetPropertyValue(StaticMeshComponentCdoPath, TEXT("NoSuchProperty"), MissingValue);
	TestFalse(TEXT("An invalid property fails"), Missing.bSuccess);
	TestTrue(TEXT("A failed call leaves the payload empty"), MissingValue.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorAutomationSetPropertyValueTest,
	"EditorAutomation.Core.SetPropertyValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorAutomationSetPropertyValueTest::RunTest(const FString& Parameters)
{
	FString Before;
	TestTrue(
		TEXT("The property reads before the write"),
		UEditorAutomationCoreLibrary::GetPropertyValue(StaticMeshComponentCdoPath, TEXT("bCastDynamicShadow"), Before).bSuccess);

	FEditorAutomationPropertyWrite Write;
	const FEditorAutomationResult WriteResult = UEditorAutomationCoreLibrary::SetPropertyValue(
		StaticMeshComponentCdoPath, TEXT("bCastDynamicShadow"), TEXT("true"), false, Write);
	TestTrue(TEXT("The write succeeded"), WriteResult.bSuccess);
	TestEqual(TEXT("The write reports the old value"), Write.OldValueJson, Before);
	TestEqual(TEXT("The write reports the new value"), Write.NewValueJson, FString(TEXT("\"true\"")));

	FEditorAutomationPropertyWrite Restore;
	const FEditorAutomationResult RestoreResult = UEditorAutomationCoreLibrary::SetPropertyValue(
		StaticMeshComponentCdoPath, TEXT("bCastDynamicShadow"), Before, false, Restore);
	TestTrue(TEXT("The restore succeeded"), RestoreResult.bSuccess);

	FString Restored;
	TestTrue(
		TEXT("The property reads back after the restore"),
		UEditorAutomationCoreLibrary::GetPropertyValue(StaticMeshComponentCdoPath, TEXT("bCastDynamicShadow"), Restored).bSuccess);
	TestEqual(TEXT("The restored value matches the original"), Restored, Before);

	FEditorAutomationPropertyWrite BadWrite;
	const FEditorAutomationResult BadResult = UEditorAutomationCoreLibrary::SetPropertyValue(
		StaticMeshComponentCdoPath, TEXT("RelativeLocation.X"), TEXT("not-json"), false, BadWrite);
	TestFalse(TEXT("An invalid value fails the write"), BadResult.bSuccess);
	TestTrue(TEXT("The failure explains itself"), !BadResult.ErrorMessage.IsEmpty());
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
	TestTrue(TEXT("The return value is the envelope"), Result.ReturnValueJson.Contains(TEXT("\"bSuccess\":\"true\"")));
	TestTrue(TEXT("The out parameter carries data"), Result.OutParametersJson.Contains(TEXT("\"OutValue\":\"pong\"")));
	TestTrue(TEXT("The return Type is the envelope struct"), Result.ReturnType.Contains(TEXT("EditorAutomationResult")));

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
		TestTrue(TEXT("The first call returns an envelope"), Result.Results[0].ReturnValueJson.Contains(TEXT("\"bSuccess\":\"true\"")));
		TestTrue(TEXT("The first call reports its out value"), Result.Results[0].OutParametersJson.Contains(TEXT("\"OutValue\":\"pong\"")));
		TestTrue(TEXT("The second call returns an envelope"), Result.Results[1].ReturnValueJson.Contains(TEXT("\"bSuccess\":\"true\"")));
		TestTrue(TEXT("The second call reports editor info"), Result.Results[1].OutParametersJson.Contains(TEXT("ProcessId")));
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
	FEditorAutomationLogPage Page;
	const FEditorAutomationResult Result =
		UEditorAutomationCoreLibrary::GetRecentLog(10, 3, TEXT(""), false, Page);
	TestTrue(TEXT("GetRecentLog succeeds"), Result.bSuccess);
	TestTrue(TEXT("The capture reports a total"), Page.TotalRecorded >= 0);
	TestTrue(TEXT("The page respects MaxLines"), Page.Entries.Num() <= 10);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
