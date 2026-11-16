// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationLogCapture.h"

#include "CoreGlobals.h"
#include "HAL/CriticalSection.h"
#include "Misc/DateTime.h"

#include "Logging/LogVerbosity.h"

namespace
{
	/** Ring-buffer capacity; older lines are dropped first. */
	constexpr int32 MaxCapturedEntries = 4096;
	/** One captured message is clipped to this many characters. */
	constexpr int32 MaxCapturedMessageChars = 2048;

	FCriticalSection GCaptureMutex;
	TArray<FEditorAutomationLogEntry> GCapturedEntries;
	int32 GTotalRecorded = 0;

	/** Bounded, thread-safe sink for warning/error lines from any thread. */
	class FEditorAutomationLogDevice final : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* V, const ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity == ELogVerbosity::NoLogging)
			{
				return;
			}
			// The buffer is meant for diagnostics: keep Warning and above, skip Log/Verbose traffic.
			if (static_cast<int32>(Verbosity) > static_cast<int32>(ELogVerbosity::Warning))
			{
				return;
			}

			FEditorAutomationLogEntry Entry;
			Entry.Time = FDateTime::Now().ToString(TEXT("%H:%M:%S"));
			Entry.Verbosity = static_cast<int32>(Verbosity);
			Entry.Category = Category.ToString();
			Entry.Message = V != nullptr ? FString(V) : FString();
			if (Entry.Message.Len() > MaxCapturedMessageChars)
			{
				Entry.Message = Entry.Message.Left(MaxCapturedMessageChars);
			}

			FScopeLock Lock(&GCaptureMutex);
			++GTotalRecorded;
			GCapturedEntries.Add(MoveTemp(Entry));
			const int32 Overflow = GCapturedEntries.Num() - MaxCapturedEntries;
			if (Overflow > 0)
			{
				GCapturedEntries.RemoveAt(0, Overflow, EAllowShrinking::No);
			}
		}
	};

	FEditorAutomationLogDevice* GLogDevice = nullptr;
}

void EditorAutomation::LogCapture::Register()
{
	if (GLogDevice != nullptr)
	{
		return;
	}
	GLogDevice = new FEditorAutomationLogDevice();
	if (GLog != nullptr)
	{
		GLog->AddOutputDevice(GLogDevice);
	}
}

void EditorAutomation::LogCapture::Unregister()
{
	if (GLogDevice == nullptr)
	{
		return;
	}
	if (GLog != nullptr)
	{
		GLog->RemoveOutputDevice(GLogDevice);
	}
	delete GLogDevice;
	GLogDevice = nullptr;
	FScopeLock Lock(&GCaptureMutex);
	GCapturedEntries.Reset();
}

void EditorAutomation::LogCapture::Query(
	const int32 MaxLines,
	const int32 MinVerbosity,
	const FString& CategoryFilter,
	const bool bClear,
	TArray<FEditorAutomationLogEntry>& OutEntries,
	int32& OutTotalRecorded,
	bool& bOutTruncated)
{
	FScopeLock Lock(&GCaptureMutex);

	OutEntries.Reset();
	OutTotalRecorded = GTotalRecorded;
	bOutTruncated = false;

	const int32 Capacity = FMath::Clamp(MaxLines, 1, 2000);
	TArray<FEditorAutomationLogEntry> NewestFirst;
	for (int32 Index = GCapturedEntries.Num() - 1; Index >= 0; --Index)
	{
		const FEditorAutomationLogEntry& Entry = GCapturedEntries[Index];
		if (Entry.Verbosity > MinVerbosity)
		{
			continue;
		}
		if (!CategoryFilter.IsEmpty() && !Entry.Category.Contains(CategoryFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (NewestFirst.Num() >= Capacity)
		{
			bOutTruncated = true;
			break;
		}
		NewestFirst.Add(Entry);
	}

	Algo::Reverse(NewestFirst);
	OutEntries = MoveTemp(NewestFirst);

	if (bClear)
	{
		GCapturedEntries.Reset();
	}
}
