// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorAutomationLogCapture.generated.h"

/** One captured editor log line. */
USTRUCT(BlueprintType)
struct FEditorAutomationLogEntry
{
	GENERATED_BODY()

	/** Local wall-clock time of the line, "HH:MM:SS". */
	UPROPERTY(BlueprintReadOnly) FString Time;
	/** ELogVerbosity::Type numeric value; resolve names through GetEnumInfo (Fatal=1, Error=2, Warning=3). */
	UPROPERTY(BlueprintReadOnly) int32 Verbosity = 0;
	UPROPERTY(BlueprintReadOnly) FString Category;
	UPROPERTY(BlueprintReadOnly) FString Message;
};

/** A page of captured editor log lines, oldest first. */
USTRUCT(BlueprintType)
struct FEditorAutomationLogPage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) TArray<FEditorAutomationLogEntry> Entries;
	/** Total lines captured since the editor started, before filtering. */
	UPROPERTY(BlueprintReadOnly) int32 TotalRecorded = 0;
	/** True when more matching lines existed than the returned page holds. */
	UPROPERTY(BlueprintReadOnly) bool bTruncated = false;
};

/**
 * In-memory capture of the editor output log.
 *
 * EditorAutomationCore registers a bounded FOutputDevice with GLog at module startup: every
 * warning/error line is recorded into a ring buffer (4096 lines), so a harness can read
 * recent diagnostics without touching log files or their rotation. Verbose/Log traffic
 * is not captured; the buffer only holds Warning and above.
 */
namespace EditorAutomation
{
	namespace LogCapture
	{
		/** Attach the capture device to GLog. Called from EditorAutomationCoreModule::StartupModule. */
		EDITORAUTOMATION_API void Register();

		/** Detach the capture device and release the buffer. Called from ShutdownModule. */
		EDITORAUTOMATION_API void Unregister();

		/**
		 * Copy the newest matching entries into OutEntries, oldest first.
		 *
		 * @param MaxLines        Maximum entries to return (1..2000).
		 * @param MinVerbosity    ELogVerbosity numeric ceiling; only lines at or above this severity are returned.
		 * @param CategoryFilter  Case-insensitive substring match on the log category; empty accepts all.
		 * @param bClear          Clear the buffer after the copy.
		 * @param OutEntries      Matching entries, oldest first.
		 * @param OutTotalRecorded Total lines captured since the editor started (unfiltered).
		 * @param bOutTruncated   True when more matching lines existed than MaxLines.
		 */
		EDITORAUTOMATION_API void Query(
			int32 MaxLines,
			int32 MinVerbosity,
			const FString& CategoryFilter,
			bool bClear,
			TArray<FEditorAutomationLogEntry>& OutEntries,
			int32& OutTotalRecorded,
			bool& bOutTruncated);
	}
}
