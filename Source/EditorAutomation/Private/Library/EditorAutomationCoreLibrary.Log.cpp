// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationCoreLibrary.h"

#include "EditorAutomationLogCapture.h"
#include "Logging/LogVerbosity.h"

FEditorAutomationLogPage UEditorAutomationCoreLibrary::GetRecentLog(
	const int32 MaxLines,
	const int32 MinVerbosity,
	const FString& CategoryFilter,
	const bool bClear)
{
	FEditorAutomationLogPage Page;
	const int32 ClampedVerbosity = FMath::Clamp(
		MinVerbosity,
		static_cast<int32>(ELogVerbosity::Fatal),
		static_cast<int32>(ELogVerbosity::Warning));
	EditorAutomation::LogCapture::Query(
		MaxLines,
		ClampedVerbosity,
		CategoryFilter,
		bClear,
		Page.Entries,
		Page.TotalRecorded,
		Page.bTruncated);
	return Page;
}
