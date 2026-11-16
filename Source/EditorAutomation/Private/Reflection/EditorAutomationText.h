// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FEditorAutomationTextRef;

namespace EditorAutomation
{
	/** Build the wire identity of one FText. */
	FEditorAutomationTextRef MakeTextRef(const FText& Text);
}
