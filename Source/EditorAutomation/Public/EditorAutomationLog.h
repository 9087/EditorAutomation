// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/** Diagnostics from EditorAutomation itself; failures are logged here so GetRecentLog can surface them. */
DECLARE_LOG_CATEGORY_EXTERN(LogEditorAutomation, Log, All);
