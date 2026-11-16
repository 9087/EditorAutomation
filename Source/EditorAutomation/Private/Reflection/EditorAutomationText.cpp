// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "Reflection/EditorAutomationText.h"

#include "EditorAutomationCoreLibrary.h"
#include "Internationalization/TextLocalizationResource.h"

FEditorAutomationTextRef EditorAutomation::MakeTextRef(const FText& Text)
{
	FEditorAutomationTextRef Ref;
	Ref.NativeText = FTextInspector::GetDisplayString(Text);
	if (const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text); Namespace.IsSet())
	{
		Ref.Namespace = Namespace.GetValue();
	}
	if (const TOptional<FString> Key = FTextInspector::GetKey(Text); Key.IsSet())
	{
		Ref.Key = Key.GetValue();
	}
	if (const FString* Source = FTextInspector::GetSourceString(Text))
	{
		Ref.SourceString = *Source;
		Ref.SourceHash = static_cast<int64>(FTextLocalizationResource::HashString(*Source));
	}
	FName TableId;
	FString TableKey;
	if (FTextInspector::GetTableIdAndKey(Text, TableId, TableKey))
	{
		Ref.TableId = TableId.ToString();
		if (Ref.Key.IsEmpty()) Ref.Key = TableKey;
	}
	return Ref;
}
