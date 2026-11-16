// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationUMGLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ContentWidget.h"
#include "Components/PanelWidget.h"
#include "EditorAutomationLog.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"

namespace
{
	TMap<FString, TMap<FString, FString>> GWidgetTreeCache;

	UWidget* FindWidget(const FString& ObjectPath)
	{
		if (!IsInGameThread() || ObjectPath.IsEmpty()) return nullptr;
		return FindObject<UWidget>(nullptr, *ObjectPath);
	}

	FString BlueprintPathOf(UUserWidget* UserWidget)
	{
		if (UserWidget == nullptr) return FString();
		const FString ClassPath = UserWidget->GetClass()->GetPathName();
		if (!ClassPath.EndsWith(TEXT("_C"))) return ClassPath;
		return ClassPath.LeftChop(2);
	}

	void AddChildWidgets(UWidget* Widget, const FString& ObjectPath, const FString& TreePath, TArray<FEditorAutomationWidgetNode>& Out)
	{
		if (Widget == nullptr) return;

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
			Children.Add(Content->GetContent());
		}

		for (UWidget* Child : Children)
		{
			if (Child == nullptr || Child->GetClass() == nullptr) continue;
			const FString ChildTreePath = TreePath + TEXT("/") + Child->GetName();
			FEditorAutomationWidgetNode Node;
			Node.ObjectPath = Child->GetPathName();
			Node.TreePath = ChildTreePath;
			Node.ParentObjectPath = ObjectPath;
			Node.ParentTreePath = TreePath;
			Node.ClassPath = Child->GetClass()->GetPathName();
			Out.Add(Node);
			AddChildWidgets(Child, Node.ObjectPath, Node.TreePath, Out);
		}
	}
}

TArray<FString> UEditorAutomationUMGLibrary::GetWidgetRoots()
{
	TArray<FString> Roots;
	if (!IsInGameThread()) return Roots;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() == nullptr) continue;
		if (Widget->GetWorld()->WorldType != EWorldType::PIE) continue;
		if (!Widget->GetCachedWidget().IsValid()) continue;
		if (!Widget->IsInViewport()) continue;
		Roots.AddUnique(Widget->GetPathName());
	}
	Roots.Sort();
	return Roots;
}

FString UEditorAutomationUMGLibrary::GetWidgetBlueprintPath(UWidget* Widget)
{
	if (Widget == nullptr) return FString();
	UUserWidget* UserWidget = Cast<UUserWidget>(Widget);
	if (UserWidget == nullptr)
	{
		UserWidget = Widget->GetTypedOuter<UUserWidget>();
	}
	return BlueprintPathOf(UserWidget);
}

FString UEditorAutomationUMGLibrary::GetWidgetClassPath(UWidget* Widget)
{
	return Widget != nullptr && Widget->GetClass() != nullptr ? Widget->GetClass()->GetPathName() : FString();
}

FEditorAutomationWidgetTreeDiff UEditorAutomationUMGLibrary::GetWidgetTreeDiff(UUserWidget* RootWidget)
{
	FEditorAutomationWidgetTreeDiff Diff;
	if (!IsInGameThread() || RootWidget == nullptr) return Diff;
	Diff.RootObjectPath = RootWidget->GetPathName();
	UWidget* Start = RootWidget->WidgetTree != nullptr ? RootWidget->WidgetTree->RootWidget : nullptr;
	if (Start == nullptr) return Diff;
	Diff.RootObjectPath = Start->GetPathName();

	const FString StartTreePath = TEXT("/Root");
	FEditorAutomationWidgetNode RootNode;
	RootNode.ObjectPath = Start->GetPathName();
	RootNode.TreePath = StartTreePath;
	RootNode.ClassPath = Start->GetClass() != nullptr ? Start->GetClass()->GetPathName() : FString();
	Diff.Nodes.Add(RootNode);
	AddChildWidgets(Start, RootNode.ObjectPath, StartTreePath, Diff.Nodes);

	TMap<FString, FString>& Previous = GWidgetTreeCache.FindOrAdd(RootWidget->GetPathName());
	TSet<FString> Seen;
	for (FEditorAutomationWidgetNode& Node : Diff.Nodes)
	{
		Seen.Add(Node.ObjectPath);
		const FString* OldClass = Previous.Find(Node.ObjectPath);
		Node.Kind = OldClass == nullptr ? EEditorAutomationWidgetDiffKind::Add
			: (*OldClass == Node.ClassPath ? EEditorAutomationWidgetDiffKind::None : EEditorAutomationWidgetDiffKind::Change);
		Previous.Add(Node.ObjectPath, Node.ClassPath);
	}
	for (const TPair<FString, FString>& Old : Previous)
	{
		if (Seen.Contains(Old.Key)) continue;
		FEditorAutomationWidgetNode Node;
		Node.ObjectPath = Old.Key;
		Node.ClassPath = Old.Value;
		Node.Kind = EEditorAutomationWidgetDiffKind::Delete;
		Diff.Nodes.Add(Node);
	}
	for (const TPair<FString, FString>& Old : Previous)
	{
		if (!Seen.Contains(Old.Key)) Previous.Remove(Old.Key);
	}
	return Diff;
}

void UEditorAutomationUMGLibrary::ResetWidgetTreeCache(UUserWidget* RootWidget)
{
	if (RootWidget != nullptr) GWidgetTreeCache.Remove(RootWidget->GetPathName());
}

FEditorAutomationWidgetGeometry UEditorAutomationUMGLibrary::GetWidgetGeometry(UWidget* Widget)
{
	FEditorAutomationWidgetGeometry Geometry;
	if (Widget == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("GetWidgetGeometry: widget is null."));
		return Geometry;
	}
	const TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("GetWidgetGeometry: widget '%s' has no Slate widget yet."), *Widget->GetPathName());
		return Geometry;
	}
	const FGeometry& CachedGeometry = SlateWidget->GetCachedGeometry();
	if (CachedGeometry.GetAbsoluteSize().IsNearlyZero())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("GetWidgetGeometry: widget '%s' is not laid out yet."), *Widget->GetPathName());
		return Geometry;
	}
	Geometry.AbsolutePosition = CachedGeometry.GetAbsolutePosition();
	Geometry.AbsoluteSize = CachedGeometry.GetAbsoluteSize();
	Geometry.LocalSize = CachedGeometry.GetLocalSize();
	Geometry.LayoutScale = CachedGeometry.GetAccumulatedLayoutTransform().GetScale();
	return Geometry;
}
