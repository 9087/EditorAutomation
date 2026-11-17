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
#include "EditorAutomationLog.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "WidgetBlueprintEditor.h"
#include "BlueprintModes/WidgetBlueprintApplicationModes.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

namespace
{
	template <typename ResultT>
	ResultT UMGEditorFailure(const FString& Message)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("%s"), *Message);
		return ResultT{};
	}

	UWidgetBlueprint* LoadWidgetBlueprint(const FString& Path)
	{
		if (Path.IsEmpty()) return nullptr;
	if (UWidgetBlueprint* Found = LoadObject<UWidgetBlueprint>(nullptr, *Path)) return Found;
	if (!Path.Contains(TEXT("."))) return LoadObject<UWidgetBlueprint>(nullptr, *(Path + TEXT(".") + FPaths::GetBaseFilename(Path)));
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

bool UEditorAutomationUMGEditorLibrary::OpenWidgetGraph(const FString& WidgetBlueprintPath)
{
	UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (Blueprint == nullptr || GEditor == nullptr) return false;
	UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (Subsystem == nullptr) return false;
	Subsystem->OpenEditorForAsset(Blueprint);
	IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(Blueprint, true);
	if (FWidgetBlueprintEditor* WidgetEditor = static_cast<FWidgetBlueprintEditor*>(Instance))
	{
		WidgetEditor->SetCurrentMode(FWidgetBlueprintApplicationModes::GraphMode);
		return true;
	}
	return false;
}

FString UEditorAutomationUMGEditorLibrary::CreateWidgetBlueprint(
	const FString& AssetPath,
	const FString& ParentClassPath)
{
	if (AssetPath.IsEmpty())
	{
		return UMGEditorFailure<FString>(TEXT("AssetPath is required."));
	}
	if (LoadObject<UObject>(nullptr, *AssetPath) != nullptr)
	{
		return UMGEditorFailure<FString>(FString::Printf(TEXT("Asset '%s' already exists."), *AssetPath));
	}
	UClass* ParentClass = ParentClassPath.IsEmpty() ? UUserWidget::StaticClass() : LoadObject<UClass>(nullptr, *ParentClassPath);
	if (ParentClass == nullptr || !ParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return UMGEditorFailure<FString>(TEXT("ParentClassPath must resolve to a UUserWidget subclass."));
	}
	UPackage* Package = CreatePackage(*AssetPath);
	if (Package == nullptr)
	{
		return UMGEditorFailure<FString>(TEXT("Could not create the target package."));
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
		return UMGEditorFailure<FString>(TEXT("WidgetBlueprintFactory failed."));
	}
	FAssetRegistryModule::AssetCreated(Blueprint);
	return Blueprint->GetPathName();
}

FString UEditorAutomationUMGEditorLibrary::AddRootWidget(
	const FString& WidgetBlueprintPath,
	const FString& WidgetClassPath,
	FName WidgetName)
{
	UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	UClass* WidgetClass = LoadClass<UWidget>(nullptr, *WidgetClassPath);
	if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
	{
		return UMGEditorFailure<FString>(TEXT("WidgetBlueprint was not loaded."));
	}
	if (Blueprint->WidgetTree->RootWidget != nullptr)
	{
		return UMGEditorFailure<FString>(TEXT("WidgetBlueprint already has a root widget."));
	}
	if (WidgetClass == nullptr)
	{
		return UMGEditorFailure<FString>(FString::Printf(TEXT("Widget class '%s' was not found."), *WidgetClassPath));
	}
	if (WidgetName.IsNone() || Blueprint->WidgetTree->FindWidget(WidgetName) != nullptr)
	{
		return UMGEditorFailure<FString>(TEXT("WidgetName is empty or already used."));
	}
	UWidget* Root = Blueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
	Blueprint->WidgetTree->RootWidget = Root;
	Blueprint->Modify();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return Root != nullptr ? Root->GetPathName() : FString();
}

TArray<UWidget*> UEditorAutomationUMGEditorLibrary::GetWidgetTree(const FString& WidgetBlueprintPath)
{
	TArray<UWidget*> Widgets;
	UWidgetBlueprint* Blueprint = LoadWidgetBlueprint(WidgetBlueprintPath);
	if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr) return Widgets;
	CollectWidgets(Blueprint->WidgetTree->RootWidget, Widgets);
	return Widgets;
}

UWidget* UEditorAutomationUMGEditorLibrary::GetWidgetParent(UWidget* Widget)
{
	if (Widget == nullptr || Widget->Slot == nullptr) return nullptr;
	return Widget->Slot->Parent;
}

TArray<UWidget*> UEditorAutomationUMGEditorLibrary::GetWidgetChildren(UWidget* Widget)
{
	TArray<UWidget*> Children;
	if (Widget == nullptr) return Children;
	// A UserWidget's design-time tree hangs off WidgetTree->RootWidget; returning
	// empty hid NamedSlot content (the slot widget lives inside that tree).
	if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
	{
		if (UserWidget->WidgetTree != nullptr && UserWidget->WidgetTree->RootWidget != nullptr)
		{
			Children.Add(UserWidget->WidgetTree->RootWidget);
		}
		return Children;
	}
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index) Children.Add(Panel->GetChildAt(Index));
	}
	else if (UContentWidget* Content = Cast<UContentWidget>(Widget))
	{
		if (Content->GetContent() != nullptr) Children.Add(Content->GetContent());
	}
	return Children;
}

UPanelSlot* UEditorAutomationUMGEditorLibrary::GetWidgetSlot(UWidget* Widget)
{
	return Widget != nullptr ? Widget->Slot : nullptr;
}

FString UEditorAutomationUMGEditorLibrary::AddWidget(
	UWidget* Parent,
	const FString& WidgetClassPath,
	FName WidgetName,
	int32 ChildIndex)
{
	UWidgetTree* Tree = WidgetTreeOf(Parent);
	UClass* WidgetClass = LoadClass<UWidget>(nullptr, *WidgetClassPath);
	if (Parent == nullptr || Tree == nullptr)
	{
		return UMGEditorFailure<FString>(TEXT("Parent widget is invalid or not owned by a WidgetTree."));
	}
	if (WidgetClass == nullptr)
	{
		return UMGEditorFailure<FString>(FString::Printf(TEXT("Widget class '%s' was not found."), *WidgetClassPath));
	}
	if (WidgetName.IsNone() || Tree->FindWidget(WidgetName) != nullptr)
	{
		return UMGEditorFailure<FString>(FString::Printf(TEXT("Widget name '%s' is empty or already used."), *WidgetName.ToString()));
	}

	UWidget* NewWidget = Tree->ConstructWidget<UWidget>(WidgetClass, WidgetName);
	if (NewWidget == nullptr)
	{
		return UMGEditorFailure<FString>(TEXT("Could not construct the widget."));
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
			return UMGEditorFailure<FString>(TEXT("ChildIndex is outside the parent's child range."));
		}
	}
	else if (UContentWidget* Content = Cast<UContentWidget>(Parent))
	{
		if (ChildIndex > 0)
		{
			Tree->RemoveWidget(NewWidget);
			return UMGEditorFailure<FString>(TEXT("A content parent supports only child index 0."));
		}
		Content->SetContent(NewWidget);
	}
	else
	{
		Tree->RemoveWidget(NewWidget);
		return UMGEditorFailure<FString>(TEXT("Parent must be a PanelWidget or ContentWidget."));
	}

	MarkOwningBlueprintDirty(NewWidget);
	return NewWidget != nullptr ? NewWidget->GetPathName() : FString();
}

bool UEditorAutomationUMGEditorLibrary::RemoveWidget(UWidget* Widget)
{
	UWidgetTree* Tree = WidgetTreeOf(Widget);
	if (Widget == nullptr || Tree == nullptr)
	{
		return UMGEditorFailure<bool>(TEXT("Widget is invalid."));
	}
	Tree->RemoveWidget(Widget);
	MarkOwningBlueprintDirty(Tree->RootWidget);
	return true;
}

FString UEditorAutomationUMGEditorLibrary::RenameWidget(UWidget* Widget, FName NewName)
{
	UWidgetTree* Tree = WidgetTreeOf(Widget);
	if (Widget == nullptr || Tree == nullptr)
	{
		return UMGEditorFailure<FString>(TEXT("Widget is invalid."));
	}
	if (NewName.IsNone() || Tree->FindWidget(NewName) != nullptr)
	{
		return UMGEditorFailure<FString>(FString::Printf(TEXT("Widget name '%s' is empty or already used."), *NewName.ToString()));
	}
	if (!Widget->Rename(*NewName.ToString(), Tree, REN_DoNotDirty | REN_DontCreateRedirectors))
	{
		return UMGEditorFailure<FString>(TEXT("Could not rename the widget."));
	}
	MarkOwningBlueprintDirty(Widget);
	return Widget->GetPathName();
}

bool UEditorAutomationUMGEditorLibrary::ReparentWidget(
	UWidget* Widget,
	UWidget* NewParent,
	int32 ChildIndex)
{
	if (Widget == nullptr || NewParent == nullptr)
	{
		return UMGEditorFailure<bool>(TEXT("Widget and NewParent are required."));
	}
	UWidget* OldParent = GetWidgetParent(Widget);
	if (OldParent == nullptr)
	{
		return UMGEditorFailure<bool>(TEXT("Widget has no parent."));
	}
	if (UPanelWidget* OldPanel = Cast<UPanelWidget>(OldParent)) OldPanel->RemoveChild(Widget);
	else if (UContentWidget* OldContent = Cast<UContentWidget>(OldParent)) OldContent->SetContent(nullptr);
	else return UMGEditorFailure<bool>(TEXT("Old parent is unsupported."));

	if (UPanelWidget* NewPanel = Cast<UPanelWidget>(NewParent))
	{
		if (ChildIndex >= 0 && ChildIndex <= NewPanel->GetChildrenCount()) NewPanel->InsertChildAt(ChildIndex, Widget);
		else if (ChildIndex < 0) NewPanel->AddChild(Widget);
		else return UMGEditorFailure<bool>(TEXT("ChildIndex is outside the new parent's range."));
	}
	else if (UContentWidget* NewContent = Cast<UContentWidget>(NewParent))
	{
		if (ChildIndex > 0) return UMGEditorFailure<bool>(TEXT("A content parent supports only child index 0."));
		NewContent->SetContent(Widget);
	}
	else
	{
		return UMGEditorFailure<bool>(TEXT("NewParent must be a PanelWidget or ContentWidget."));
	}
	MarkOwningBlueprintDirty(Widget);
	return true;
}

bool UEditorAutomationUMGEditorLibrary::SetWidgetProperty(
	UWidget* Widget,
	const FString& PropertyPath,
	const FString& ValueJson)
{
	if (Widget == nullptr)
	{
		return UMGEditorFailure<bool>(TEXT("Widget is invalid."));
	}
	const FEditorAutomationPropertyWrite Write = UEditorAutomationCoreLibrary::SetPropertyValue(
		Widget->GetPathName(), PropertyPath, ValueJson, false);
	if (!Write.bSuccess)
	{
		return UMGEditorFailure<bool>(TEXT("Setting the property failed; see the editor log."));
	}
	MarkOwningBlueprintDirty(Widget);
	return true;
}

bool UEditorAutomationUMGEditorLibrary::SetWidgetSlotProperty(
	UPanelSlot* Slot,
	const FString& PropertyPath,
	const FString& ValueJson)
{
	if (Slot == nullptr)
	{
		return UMGEditorFailure<bool>(TEXT("Slot is invalid."));
	}
	const FEditorAutomationPropertyWrite Write = UEditorAutomationCoreLibrary::SetPropertyValue(
		Slot->GetPathName(), PropertyPath, ValueJson, false);
	if (!Write.bSuccess)
	{
		return UMGEditorFailure<bool>(TEXT("Setting the property failed; see the editor log."));
	}
	MarkOwningBlueprintDirty(Slot);
	return true;
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

bool UEditorAutomationUMGEditorLibrary::IsWidgetVariable(UWidget* Widget, UBlueprint* WidgetBlueprint)
{
	if (Widget == nullptr || WidgetBlueprint == nullptr || WidgetBlueprint->GeneratedClass == nullptr)
	{
		return false;
	}
	return FindFProperty<FProperty>(WidgetBlueprint->GeneratedClass, Widget->GetFName()) != nullptr;
}

UWidget* UEditorAutomationUMGEditorLibrary::GetUserWidgetRoot(UWidget* Widget)
{
	UUserWidget* UserWidget = OwningUserWidget(Widget);
	if (UserWidget != nullptr && UserWidget->WidgetTree != nullptr)
	{
		return UserWidget->WidgetTree->RootWidget;
	}
	// Design-time template tree: there is no UUserWidget instance, so resolve the owning
	// Widget Blueprint's tree root instead (that is what a template widget's "user root" is).
	if (UBaseWidgetBlueprint* WidgetBlueprint = Widget != nullptr ? Widget->GetTypedOuter<UBaseWidgetBlueprint>() : nullptr)
	{
		if (WidgetBlueprint->WidgetTree != nullptr)
		{
			return WidgetBlueprint->WidgetTree->RootWidget;
		}
	}
	return nullptr;
}

TArray<FEditorAutomationWidgetTextState> UEditorAutomationUMGEditorLibrary::GetWidgetTextActiveStates(UWidget* Widget)
{
	TArray<FEditorAutomationWidgetTextState> States;
	if (Widget == nullptr)
	{
		return UMGEditorFailure<TArray<FEditorAutomationWidgetTextState>>(TEXT("GetWidgetTextActiveStates: Widget must not be null."));
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
		States.Add(MoveTemp(State));
	}
	return States;
}