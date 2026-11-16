// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "EditorAutomationLogCapture.h"

/**
 * The single EditorAutomation module.
 *
 * All editor-facing libraries (core reflection, Blueprint/level/asset/UMG helpers and the
 * Remote Control entry points) live in this one module; the log capture device is attached
 * as soon as the module loads so diagnostics are available to callers immediately.
 */
class FEditorAutomationModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		EditorAutomation::LogCapture::Register();
	}

	virtual void ShutdownModule() override
	{
		EditorAutomation::LogCapture::Unregister();
	}
};

IMPLEMENT_MODULE(FEditorAutomationModule, EditorAutomation);
