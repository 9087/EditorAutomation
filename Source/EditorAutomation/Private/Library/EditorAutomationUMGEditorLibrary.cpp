// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationUMGEditorLibrary.h"

#include "Blueprint/UserWidget.h"
#include "BaseWidgetBlueprint.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ContentWidget.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "EditorAutomationCoreLibrary.h"
#include "EditorAutomationBlueprintLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "WidgetBlueprintEditor.h"
#include "BlueprintModes/WidgetBlueprintApplicationModes.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

namespace
{
	FEditorAutomationResult UMGEditorFailure(const FString& Message)
	{
		return FEditorAutomationResult::Failure(EEditorAutomationResultCode::InvalidArgument, Message);
	}

	FEditorAutomationResult UMGEditorFailure(
		const EEditorAutomationResultCode Code,
		const FString& Message,
		const FString& Hint = FString())
	{
		return FEditorAutomationResult::Failure(Code, Message, Hint);
	}

	UWidgetBlueprint* LoadWidgetBlueprint(const FString& Path)
	{
		if (Path.IsEmpty()) return nullptr;
		if (UWidgetBlueprint* Found = LoadObject<UWidgetBlueprint>(nullptr, *Path)) return Found;
		if (!Path.Contains(TEXT(".")))
		{
			return LoadObject<UWidgetBlueprint>(nullptr, *(Path + TEXT(".") + FPaths::GetBaseFilename(Path)));
		}
		return nullptr;
	}

	UWidgetTree* WidgetTreeOf(UWidget* Widget)
	{
		return Widget != nullptr ? Widget->GetTypedOuter<UWidgetTree>() : nullptr;
	}

	void MarkOwningBlueprintDirty(UWidget* Widget)
	{
		if (UWidgetBlueprint* Blueprint = Widget != nullptr ? Widget->GetTypedOuter<UWidgetBlueprint>() : nullptr)
		{
			Blueprint->Modify();
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	void MarkOwningBlueprintDirty(UPanelSlot* Slot)
	{
		if (UWidgetBlueprint* Blueprint = Slot != nullptr ? Slot->GetTypedOuter<UWidgetBlueprint>() : nullptr)
		{
			Blueprint->Modify();
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	void CollectWidgets(UWidget* Widget, TArray<UWidget*>& Out)
	{
		if (Widget == nullptr) return;
		Out.Add(Widget);
		if (Widget->IsA<UUserWidget>()) return;
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				CollectWidgets(Panel->GetChildAt(Index), Out);
			}
		}
		else if (UContentWidget* Content = Cast<UContentWidget>(Widget))
		{
			CollectWidgets(Content->GetContent(), Out);
		}
	}
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::OpenWidgetGraph(const FString& WidgetBlueprintPath)
{
	UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (Blueprint == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("WidgetBlueprint '%s' could not be loaded."), *WidgetBlueprintPath),
			TEXT("Pass a WidgetBlueprint asset path (/Game/Dir/WBP) or object path (/Game/Dir/WBP.WBP)."));
	}
	if (GEditor == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::NotReady,
			TEXT("The editor is not available."),
			TEXT("Run this call inside the Unreal editor process."));
	}
	UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (Subsystem == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::NotReady,
			TEXT("The asset editor subsystem is not available."),
			TEXT("Run this call inside the Unreal editor process."));
	}
	Subsystem->OpenEditorForAsset(Blueprint);
	IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(Blueprint, true);
	if (FWidgetBlueprintEditor* WidgetEditor = static_cast<FWidgetBlueprintEditor*>(Instance))
	{
		WidgetEditor->SetCurrentMode(FWidgetBlueprintApplicationModes::GraphMode);
		return FEditorAutomationResult::Success();
	}
	return UMGEditorFailure(
		EEditorAutomationResultCode::EditorRefused,
		TEXT("The WidgetBlueprint editor did not open in graph mode."),
		TEXT("Check GetRecentLog for the editor reason."));
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::CreateWidgetBlueprint(
	const FString& AssetPath,
	const FString& ParentClassPath,
	FString& OutWidgetBlueprintPath)
{
	if (AssetPath.IsEmpty())
	{
		return UMGEditorFailure(TEXT("AssetPath is required."));
	}
	if (LoadObject<UObject>(nullptr, *AssetPath) != nullptr)
	{
		return UMGEditorFailure(FString::Printf(TEXT("Asset '%s' already exists."), *AssetPath));
	}
	UClass* ParentClass = ParentClassPath.IsEmpty() ? UUserWidget::StaticClass() : LoadObject<UClass>(nullptr, *ParentClassPath);
	if (ParentClass == nullptr || !ParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Parent class '%s' is not a UUserWidget subclass."), *ParentClassPath),
			TEXT("Pass /Script/UMG.UserWidget or a WidgetBlueprint generated class path."));
	}
	UPackage* Package = CreatePackage(*AssetPath);
	if (Package == nullptr)
	{
		return UMGEditorFailure(FString::Printf(TEXT("Could not create the target package '%s'."), *AssetPath));
	}
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->ParentClass = ParentClass;
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(
		UWidgetBlueprint::StaticClass(),
		Package,
		FName(*FPaths::GetBaseFilename(AssetPath)),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn));
	if (Blueprint == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::EditorRefused,
			TEXT("WidgetBlueprintFactory failed."),
			TEXT("Check that the path is writable and no asset already exists there."));
	}
	FAssetRegistryModule::AssetCreated(Blueprint);
	OutWidgetBlueprintPath = Blueprint->GetPathName();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::AddRootWidget(
	const FString& WidgetBlueprintPath,
	const FString& WidgetClassPath,
	const FName WidgetName,
	FString& OutWidgetPath)
{
	UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("WidgetBlueprint '%s' could not be loaded."), *WidgetBlueprintPath),
			TEXT("Pass a WidgetBlueprint asset path (/Game/Dir/WBP) or object path (/Game/Dir/WBP.WBP)."));
	}
	if (Blueprint->WidgetTree->RootWidget != nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("WidgetBlueprint already has a root widget."),
			TEXT("Use AddWidget on the existing root instead."));
	}
	UClass* WidgetClass = LoadClass<UWidget>(nullptr, *WidgetClassPath);
	if (WidgetClass == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Widget class '%s' was not found."), *WidgetClassPath),
			TEXT("Pass a UMG widget class path, e.g. /Script/UMG.VerticalBox."));
	}
	if (WidgetName.IsNone() || Blueprint->WidgetTree->FindWidget(WidgetName) != nullptr)
	{
		return UMGEditorFailure(TEXT("WidgetName is empty or already used."));
	}
	UWidget* Root = Blueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
	if (Root == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::EditorRefused,
			TEXT("Could not construct the root widget."));
	}
	Blueprint->WidgetTree->RootWidget = Root;
	Blueprint->Modify();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	OutWidgetPath = Root->GetPathName();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::GetWidgetTree(
	const FString& WidgetBlueprintPath,
	TArray<UWidget*>& OutWidgets)
{
	UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("WidgetBlueprint '%s' could not be loaded."), *WidgetBlueprintPath),
			TEXT("Pass a WidgetBlueprint asset path (/Game/Dir/WBP) or object path (/Game/Dir/WBP.WBP)."));
	}
	CollectWidgets(Blueprint->WidgetTree->RootWidget, OutWidgets);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::GetWidgetParent(UWidget* Widget, UWidget*& OutParent)
{
	if (Widget == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is null."),
			TEXT("Pass a UWidget object path from GetWidgetTree."));
	}
	OutParent = Widget->Slot != nullptr ? Widget->Slot->Parent : nullptr;
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::GetWidgetChildren(
	UWidget* Widget,
	TArray<UWidget*>& OutChildren)
{
	if (Widget == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is null."),
			TEXT("Pass a UWidget object path from GetWidgetTree."));
	}
	// A UserWidget's design-time tree hangs off WidgetTree->RootWidget; returning empty hid
	// NamedSlot content (the slot widget lives inside that tree).
	if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
	{
		if (UserWidget->WidgetTree != nullptr && UserWidget->WidgetTree->RootWidget != nullptr)
		{
			OutChildren.Add(UserWidget->WidgetTree->RootWidget);
		}
		return FEditorAutomationResult::Success();
	}
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
		{
			OutChildren.Add(Panel->GetChildAt(Index));
		}
	}
	else if (UContentWidget* Content = Cast<UContentWidget>(Widget))
	{
		if (Content->GetContent() != nullptr) OutChildren.Add(Content->GetContent());
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::GetWidgetSlot(UWidget* Widget, UPanelSlot*& OutSlot)
{
	if (Widget == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is null."),
			TEXT("Pass a UWidget object path from GetWidgetTree."));
	}
	OutSlot = Widget->Slot;
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::AddWidget(
	UWidget* Parent,
	const FString& WidgetClassPath,
	const FName WidgetName,
	int32 ChildIndex,
	FString& OutWidgetPath)
{
	UWidgetTree* Tree = WidgetTreeOf(Parent);
	if (Parent == nullptr || Tree == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Parent widget is invalid or not owned by a WidgetTree."),
			TEXT("Pass a template widget object path from GetWidgetTree."));
	}
	UClass* WidgetClass = LoadClass<UWidget>(nullptr, *WidgetClassPath);
	if (WidgetClass == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Widget class '%s' was not found."), *WidgetClassPath),
			TEXT("Pass a UMG widget class path, e.g. /Script/UMG.TextBlock."));
	}
	if (WidgetName.IsNone() || Tree->FindWidget(WidgetName) != nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("Widget name '%s' is empty or already used."), *WidgetName.ToString()),
			TEXT("Pass a unique widget name."));
	}

	UWidget* NewWidget = Tree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
	if (NewWidget == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::EditorRefused,
			TEXT("Could not construct the widget."));
	}

	if (UPanelWidget* Panel = Cast<UPanelWidget>(Parent))
	{
		if (ChildIndex >= 0 && ChildIndex <= Panel->GetChildrenCount())
		{
			Panel->InsertChildAt(ChildIndex, NewWidget);
		}
		else if (ChildIndex < 0)
		{
			Panel->AddChild(NewWidget);
		}
		else
		{
			Tree->RemoveWidget(NewWidget);
			return UMGEditorFailure(TEXT("ChildIndex is outside the parent's child range."));
		}
	}
	else if (UContentWidget* Content = Cast<UContentWidget>(Parent))
	{
		if (ChildIndex > 0)
		{
			Tree->RemoveWidget(NewWidget);
			return UMGEditorFailure(TEXT("A content parent supports only child index 0."));
		}
		Content->SetContent(NewWidget);
	}
	else
	{
		Tree->RemoveWidget(NewWidget);
		return UMGEditorFailure(TEXT("Parent must be a PanelWidget or ContentWidget."));
	}

	MarkOwningBlueprintDirty(NewWidget);
	OutWidgetPath = NewWidget->GetPathName();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::RemoveWidget(UWidget* Widget)
{
	UWidgetTree* Tree = WidgetTreeOf(Widget);
	if (Widget == nullptr || Tree == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is invalid."),
			TEXT("Pass a template widget object path from GetWidgetTree."));
	}
	Tree->RemoveWidget(Widget);
	MarkOwningBlueprintDirty(Tree->RootWidget);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::RenameWidget(
	UWidget* Widget,
	const FName NewName,
	FString& OutWidgetPath)
{
	UWidgetTree* Tree = WidgetTreeOf(Widget);
	if (Widget == nullptr || Tree == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is invalid."),
			TEXT("Pass a template widget object path from GetWidgetTree."));
	}
	if (NewName.IsNone() || Tree->FindWidget(NewName) != nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("Widget name '%s' is empty or already used."), *NewName.ToString()),
			TEXT("Pass a unique widget name."));
	}
	if (!Widget->Rename(*NewName.ToString(), Tree, REN_DoNotDirty | REN_DontCreateRedirectors))
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::EditorRefused,
			TEXT("Could not rename the widget."));
	}
	MarkOwningBlueprintDirty(Widget);
	OutWidgetPath = Widget->GetPathName();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::ReparentWidget(
	UWidget* Widget,
	UWidget* NewParent,
	const int32 ChildIndex)
{
	if (Widget == nullptr || NewParent == nullptr)
	{
		return UMGEditorFailure(TEXT("Widget and NewParent are required."));
	}
	UWidget* OldParent = Widget->Slot != nullptr ? Widget->Slot->Parent : nullptr;
	if (OldParent == nullptr)
	{
		return UMGEditorFailure(TEXT("Widget has no parent."));
	}
	if (UPanelWidget* OldPanel = Cast<UPanelWidget>(OldParent))
	{
		OldPanel->RemoveChild(Widget);
	}
	else if (UContentWidget* OldContent = Cast<UContentWidget>(OldParent))
	{
		OldContent->SetContent(nullptr);
	}
	else
	{
		return UMGEditorFailure(TEXT("Old parent is unsupported."));
	}

	if (UPanelWidget* NewPanel = Cast<UPanelWidget>(NewParent))
	{
		if (ChildIndex >= 0 && ChildIndex <= NewPanel->GetChildrenCount()) NewPanel->InsertChildAt(ChildIndex, Widget);
		else if (ChildIndex < 0) NewPanel->AddChild(Widget);
		else return UMGEditorFailure(TEXT("ChildIndex is outside the new parent's range."));
	}
	else if (UContentWidget* NewContent = Cast<UContentWidget>(NewParent))
	{
		if (ChildIndex > 0) return UMGEditorFailure(TEXT("A content parent supports only child index 0."));
		NewContent->SetContent(Widget);
	}
	else
	{
		return UMGEditorFailure(TEXT("NewParent must be a PanelWidget or ContentWidget."));
	}
	MarkOwningBlueprintDirty(Widget);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::SetWidgetProperty(
	UWidget* Widget,
	const FString& PropertyPath,
	const FString& ValueJson)
{
	if (Widget == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is invalid."),
			TEXT("Pass a template widget object path from GetWidgetTree."));
	}
	FEditorAutomationPropertyWrite Write;
	const FEditorAutomationResult WriteResult = UEditorAutomationCoreLibrary::SetPropertyValue(
		Widget->GetPathName(), PropertyPath, ValueJson, false, Write);
	if (!WriteResult.bSuccess)
	{
		return WriteResult;
	}
	MarkOwningBlueprintDirty(Widget);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::SetWidgetSlotProperty(
	UPanelSlot* Slot,
	const FString& PropertyPath,
	const FString& ValueJson)
{
	if (Slot == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Slot is invalid."),
			TEXT("Pass a UPanelSlot object path from GetWidgetSlot."));
	}
	FEditorAutomationPropertyWrite Write;
	const FEditorAutomationResult WriteResult = UEditorAutomationCoreLibrary::SetPropertyValue(
		Slot->GetPathName(), PropertyPath, ValueJson, false, Write);
	if (!WriteResult.bSuccess)
	{
		return WriteResult;
	}
	MarkOwningBlueprintDirty(Slot);
	return FEditorAutomationResult::Success();
}

namespace
{
	/** Depth-first design-time widgets, including UUserWidget trees and NamedSlot content. */
	void CollectDesignTimeWidgets(UWidget* Widget, TArray<UWidget*>& Out)
	{
		if (Widget == nullptr) return;
		Out.Add(Widget);

		TArray<UWidget*> Children;
		if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			if (UserWidget->WidgetTree != nullptr && UserWidget->WidgetTree->RootWidget != nullptr)
			{
				Children.Add(UserWidget->WidgetTree->RootWidget);
			}
		}
		else if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				Children.Add(Panel->GetChildAt(Index));
			}
		}
		else if (UContentWidget* Content = Cast<UContentWidget>(Widget))
		{
			if (Content->GetContent() != nullptr) Children.Add(Content->GetContent());
		}
		for (UWidget* Child : Children)
		{
			CollectDesignTimeWidgets(Child, Out);
		}
	}

	/** The user widget that owns a widget, walking up outers (or the widget itself). */
	UUserWidget* OwningUserWidget(UWidget* Widget)
	{
		if (Widget == nullptr) return nullptr;
		if (UUserWidget* Self = Cast<UUserWidget>(Widget)) return Self;
		return Widget->GetTypedOuter<UUserWidget>();
	}
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::IsWidgetVariable(
	UWidget* Widget,
	const TSoftObjectPtr<UBlueprint>& WidgetBlueprint,
	bool& OutIsVariable)
{
	if (Widget == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is null."),
			TEXT("Pass a template widget object path from GetWidgetTree."));
	}
	UBlueprint* Blueprint = WidgetBlueprint.LoadSynchronous();
	if (Blueprint == nullptr)
	{
		// Remote Control binds a package-only soft path when the caller passes an asset path.
		const FEditorAutomationResult Resolution =
			UEditorAutomationBlueprintLibrary::ResolveBlueprintPath(WidgetBlueprint.ToString(), Blueprint);
		if (!Resolution.bSuccess)
		{
			return Resolution;
		}
	}
	if (Blueprint->GeneratedClass == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::NotReady,
			FString::Printf(TEXT("Blueprint '%s' has no generated class yet; compile it first."), *Blueprint->GetPathName()),
			TEXT("Call CompileBlueprint, then retry."));
	}
	OutIsVariable = FindFProperty<FProperty>(Blueprint->GeneratedClass, Widget->GetFName()) != nullptr;
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::GetUserWidgetRoot(UWidget* Widget, UWidget*& OutRootWidget)
{
	if (Widget == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is null."),
			TEXT("Pass a template widget object path from GetWidgetTree."));
	}
	UUserWidget* UserWidget = OwningUserWidget(Widget);
	if (UserWidget != nullptr && UserWidget->WidgetTree != nullptr)
	{
		OutRootWidget = UserWidget->WidgetTree->RootWidget;
		return FEditorAutomationResult::Success();
	}
	// Design-time template tree: there is no UUserWidget instance, so resolve the owning
	// Widget Blueprint's tree root instead (that is what a template widget's "user root" is).
	if (UBaseWidgetBlueprint* WidgetBlueprint = Widget->GetTypedOuter<UBaseWidgetBlueprint>())
	{
		if (WidgetBlueprint->WidgetTree != nullptr)
		{
			OutRootWidget = WidgetBlueprint->WidgetTree->RootWidget;
			return FEditorAutomationResult::Success();
		}
	}
	return UMGEditorFailure(
		EEditorAutomationResultCode::NotFound,
		FString::Printf(TEXT("Widget '%s' has no owning user-widget root."), *Widget->GetPathName()),
		TEXT("Pass a widget that belongs to a WidgetBlueprint tree or a live user widget."));
}

FEditorAutomationResult UEditorAutomationUMGEditorLibrary::GetWidgetTextActiveStates(
	UWidget* Widget,
	TArray<FEditorAutomationWidgetTextState>& OutStates)
{
	if (Widget == nullptr)
	{
		return UMGEditorFailure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is null."),
			TEXT("Pass a widget or its root from GetWidgetTree."));
	}
	const UEnum* VisibilityEnum = StaticEnum<ESlateVisibility>();
	TArray<UWidget*> Widgets;
	CollectDesignTimeWidgets(Widget, Widgets);
	for (UWidget* Candidate : Widgets)
	{
		const UTextBlock* TextBlock = Cast<UTextBlock>(Candidate);
		if (TextBlock == nullptr) continue;
		FEditorAutomationWidgetTextState State;
		State.WidgetPath = TextBlock->GetPathName();
		State.Text = TextBlock->GetText().ToString();
		State.Visibility = VisibilityEnum != nullptr
			? VisibilityEnum->GetDisplayNameTextByValue(static_cast<int64>(TextBlock->GetVisibility())).ToString()
			: FString();
		State.bIsEnabled = TextBlock->GetIsEnabled();
		State.bIsVisible = TextBlock->IsVisible();
		OutStates.Add(MoveTemp(State));
	}
	return FEditorAutomationResult::Success();
}
