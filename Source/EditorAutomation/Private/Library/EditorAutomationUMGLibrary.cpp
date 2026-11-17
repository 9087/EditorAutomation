// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

#include "EditorAutomationUMGLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ContentWidget.h"
#include "Components/ListView.h"
#include "Components/PanelWidget.h"
#include "EditorAutomationLog.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/STableViewBase.h"

namespace
{
	/** One readable property's type and value text, as the diff cache stores it. */
	struct FWidgetPropertySnapshot
	{
		FString Type;
		FString Value;
	};

	/** What one tree read remembered: readable properties, classes and parents per widget path. */
	struct FWidgetTreeSnapshot
	{
		TMap<FString, TMap<FString, FWidgetPropertySnapshot>> Properties;
		TMap<FString, FString> Classes;
		TMap<FString, FString> Parents;
	};

	TMap<FString, FWidgetTreeSnapshot> GWidgetTreeCache;

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

	/** Read every readable property of one widget (name → type/value snapshot). */
	TMap<FString, FWidgetPropertySnapshot> ReadReadableProperties(UWidget* Widget)
	{
		TMap<FString, FWidgetPropertySnapshot> Snapshot;
		if (Widget == nullptr || Widget->GetClass() == nullptr) return Snapshot;
		for (TFieldIterator<FProperty> It(Widget->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			if (Property->IsA<FMulticastDelegateProperty>() || Property->IsA<FDelegateProperty>()
				|| Property->IsA<FObjectProperty>() || Property->IsA<FClassProperty>()
				|| Property->IsA<FSoftObjectProperty>() || Property->IsA<FSoftClassProperty>())
			{
				continue;
			}
			if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_Deprecated))
			{
				continue;
			}
			FString Value;
			Property->ExportTextItem_InContainer(Value, Widget, nullptr, Widget, PPF_None);
			FWidgetPropertySnapshot PropertySnapshot;
			PropertySnapshot.Type = Property->GetCPPType(nullptr, 0);
			PropertySnapshot.Value = MoveTemp(Value);
			Snapshot.Add(Property->GetName(), MoveTemp(PropertySnapshot));
		}
		return Snapshot;
	}

	/** Read every delegate property of one widget with its runtime binding state. */
	TArray<FEditorAutomationDelegateInfo> ReadDelegateInfos(UWidget* Widget)
	{
		TArray<FEditorAutomationDelegateInfo> Delegates;
		if (Widget == nullptr || Widget->GetClass() == nullptr) return Delegates;
		for (TFieldIterator<FProperty> It(Widget->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			const FMulticastDelegateProperty* MulticastProperty = CastField<FMulticastDelegateProperty>(Property);
			const FDelegateProperty* SingleProperty = CastField<FDelegateProperty>(Property);
			if (MulticastProperty == nullptr && SingleProperty == nullptr) continue;

			FEditorAutomationDelegateInfo Info;
			Info.PropertyName = Property->GetName();
			Info.TypeName = (MulticastProperty != nullptr)
				? MulticastProperty->GetCPPType(nullptr, 0)
				: SingleProperty->GetCPPType(nullptr, 0);
			if (MulticastProperty != nullptr)
			{
				const FMulticastScriptDelegate* Delegate = MulticastProperty->ContainerPtrToValuePtr<FMulticastScriptDelegate>(Widget);
				Info.bHasBindings = (Delegate != nullptr) ? Delegate->IsBound() : false;
			}
			else if (SingleProperty != nullptr)
			{
				FScriptDelegate* Delegate = SingleProperty->ContainerPtrToValuePtr<FScriptDelegate>(Widget);
				Info.bHasBindings = (Delegate != nullptr) ? Delegate->IsBound() : false;
			}
			Delegates.Add(MoveTemp(Info));
		}
		return Delegates;
	}

	/** Append one node and recurse into its children, remembering each widget pointer. */
	void CollectNodes(
		UWidget* Widget,
		const FString& ObjectPath,
		const FString& TreePath,
		TArray<FEditorAutomationWidgetNode>& OutNodes,
		TMap<FString, UWidget*>& OutWidgets)
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
			OutNodes.Add(Node);
			OutWidgets.Add(Node.ObjectPath, Child);
			CollectNodes(Child, Node.ObjectPath, Node.TreePath, OutNodes, OutWidgets);
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
	if (!IsInGameThread() || RootWidget == nullptr)
	{
		if (RootWidget == nullptr)
		{
			UE_LOG(LogEditorAutomation, Error, TEXT("GetWidgetTreeDiff: root widget is null."));
		}
		return Diff;
	}
	Diff.RootObjectPath = RootWidget->GetPathName();
	UWidget* Start = RootWidget->WidgetTree != nullptr ? RootWidget->WidgetTree->RootWidget : nullptr;
	if (Start == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("GetWidgetTreeDiff: root widget '%s' has no WidgetTree root."), *RootWidget->GetPathName());
		return Diff;
	}
	Diff.RootObjectPath = Start->GetPathName();

	const FString StartTreePath = TEXT("/Root");
	FEditorAutomationWidgetNode RootNode;
	RootNode.ObjectPath = Start->GetPathName();
	RootNode.TreePath = StartTreePath;
	RootNode.ClassPath = Start->GetClass() != nullptr ? Start->GetClass()->GetPathName() : FString();
	Diff.Nodes.Add(RootNode);

	TMap<FString, UWidget*> Widgets;
	Widgets.Add(RootNode.ObjectPath, Start);
	CollectNodes(Start, RootNode.ObjectPath, StartTreePath, Diff.Nodes, Widgets);

	FWidgetTreeSnapshot& Previous = GWidgetTreeCache.FindOrAdd(RootWidget->GetPathName());
	FWidgetTreeSnapshot Current;
	TArray<FEditorAutomationWidgetNode> DeleteNodes;
	TSet<FString> Seen;
	for (FEditorAutomationWidgetNode& Node : Diff.Nodes)
	{
		Seen.Add(Node.ObjectPath);
		UWidget* Widget = Widgets.FindRef(Node.ObjectPath);
		if (Widget == nullptr) continue;

		const TMap<FString, FWidgetPropertySnapshot> Properties = ReadReadableProperties(Widget);
		Current.Properties.Add(Node.ObjectPath, Properties);
		Current.Classes.Add(Node.ObjectPath, Node.ClassPath);
		Current.Parents.Add(Node.ObjectPath, Node.ParentObjectPath);

		const TMap<FString, FWidgetPropertySnapshot>* OldProperties = Previous.Properties.Find(Node.ObjectPath);
		const FString* OldClass = Previous.Classes.Find(Node.ObjectPath);

		if (OldProperties == nullptr)
		{
			Node.Kind = EEditorAutomationWidgetDiffKind::Add;
			for (const TPair<FString, FWidgetPropertySnapshot>& Property : Properties)
			{
				FEditorAutomationWidgetPropertyInfo Info;
				Info.PropertyName = Property.Key;
				Info.Type = Property.Value.Type;
				Info.NewValue = Property.Value.Value;
				Node.Properties.Add(MoveTemp(Info));
			}
		}
		else
		{
			bool bChanged = OldClass == nullptr || *OldClass != Node.ClassPath;
			for (const TPair<FString, FWidgetPropertySnapshot>& Property : Properties)
			{
				const FWidgetPropertySnapshot* Old = OldProperties->Find(Property.Key);
				if (Old != nullptr && Old->Type == Property.Value.Type && Old->Value == Property.Value.Value) continue;
				FEditorAutomationWidgetPropertyInfo Info;
				Info.PropertyName = Property.Key;
				Info.Type = Property.Value.Type;
				if (Old != nullptr) Info.OldValue = Old->Value;
				Info.NewValue = Property.Value.Value;
				Node.Properties.Add(MoveTemp(Info));
				bChanged = true;
			}
			// A property that disappeared is a change too (e.g. a cleared binding text).
			for (const TPair<FString, FWidgetPropertySnapshot>& Old : *OldProperties)
			{
				if (Properties.Contains(Old.Key)) continue;
				FEditorAutomationWidgetPropertyInfo Info;
				Info.PropertyName = Old.Key;
				Info.Type = Old.Value.Type;
				Info.OldValue = Old.Value.Value;
				Node.Properties.Add(MoveTemp(Info));
				bChanged = true;
			}
			Node.Kind = bChanged ? EEditorAutomationWidgetDiffKind::Change : EEditorAutomationWidgetDiffKind::None;
		}

		if (Node.Kind != EEditorAutomationWidgetDiffKind::None)
		{
			Node.Delegates = ReadDelegateInfos(Widget);
		}
	}

	// Deleted widgets: report the previous snapshot's paths that are gone now.
	for (const TPair<FString, TMap<FString, FWidgetPropertySnapshot>>& Old : Previous.Properties)
	{
		if (Seen.Contains(Old.Key)) continue;
		FEditorAutomationWidgetNode Node;
		Node.ObjectPath = Old.Key;
		Node.ParentObjectPath = Previous.Parents.FindRef(Old.Key);
		Node.ClassPath = Previous.Classes.FindRef(Old.Key);
		Node.Kind = EEditorAutomationWidgetDiffKind::Delete;
		DeleteNodes.Add(MoveTemp(Node));
	}
	Diff.Nodes.Append(DeleteNodes);
	GWidgetTreeCache.Add(RootWidget->GetPathName(), MoveTemp(Current));
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

namespace
{
	/** Resolve a widget that must be a UMG UListView; logs when it is not. */
	UListView* AsListView(UWidget* Widget, const TCHAR* FunctionName)
	{
		if (Widget == nullptr)
		{
			UE_LOG(LogEditorAutomation, Error, TEXT("%s: Widget must not be null."), FunctionName);
			return nullptr;
		}
		UListView* ListView = Cast<UListView>(Widget);
		if (ListView == nullptr)
		{
			UE_LOG(LogEditorAutomation, Error,
				TEXT("%s: widget '%s' is not a UListView; list introspection requires UMG's UListView."),
				FunctionName, *Widget->GetPathName());
			return nullptr;
		}
		return ListView;
	}
}

FEditorAutomationListViewInfo UEditorAutomationUMGLibrary::GetListViewInfo(UWidget* Widget, int32 MaxItems)
{
	FEditorAutomationListViewInfo Result;
	if (!IsInGameThread())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("GetListViewInfo must be called on the game thread."));
		Result.bSuccess = false;
		return Result;
	}
	UListView* ListView = AsListView(Widget, TEXT("GetListViewInfo"));
	if (ListView == nullptr)
	{
		Result.bSuccess = false;
		return Result;
	}

	Result.ItemCount = ListView->GetNumItems();
	const int32 Limit = (MaxItems > 0) ? FMath::Min(MaxItems, Result.ItemCount) : Result.ItemCount;
	for (int32 Index = 0; Index < Limit; ++Index)
	{
		UObject* Item = ListView->GetItemAt(Index);
		FEditorAutomationListItemInfo ItemInfo;
		ItemInfo.Index = Index;
		ItemInfo.Item = Item;
		ItemInfo.ItemClass = (Item != nullptr) ? Item->GetClass() : nullptr;
		Result.Items.Add(MoveTemp(ItemInfo));
	}
	return Result;
}

FEditorAutomationListEntryInfo UEditorAutomationUMGLibrary::GetListEntryInfo(UWidget* Widget, int32 Index)
{
	FEditorAutomationListEntryInfo Result;
	Result.Index = Index;
	if (!IsInGameThread())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("GetListEntryInfo must be called on the game thread."));
		Result.bSuccess = false;
		return Result;
	}
	UListView* ListView = AsListView(Widget, TEXT("GetListEntryInfo"));
	if (ListView == nullptr)
	{
		Result.bSuccess = false;
		return Result;
	}
	const int32 ItemCount = ListView->GetNumItems();
	if (Index < 0 || Index >= ItemCount)
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("GetListEntryInfo: index %d is out of range (item count %d)."), Index, ItemCount);
		Result.bSuccess = false;
		return Result;
	}
	UObject* Item = ListView->GetItemAt(Index);
	UUserWidget* EntryWidget = (Item != nullptr) ? ListView->GetEntryWidgetFromItem(Item) : nullptr;
	if (EntryWidget == nullptr)
	{
		// Read-only: a virtualized item simply has no row widget yet; ScrollToListEntry
		// brings it on screen as an explicit action.
		UE_LOG(LogEditorAutomation, Warning,
			TEXT("GetListEntryInfo: item %d has no row widget (virtualized out of view)."), Index);
		Result.bSuccess = false;
		return Result;
	}
	Result.EntryWidget = EntryWidget;
	Result.EntryWidgetClass = EntryWidget->GetClass();
	return Result;
}

FEditorAutomationListEntryInfo UEditorAutomationUMGLibrary::ScrollToListEntry(UWidget* Widget, int32 Index)
{
	FEditorAutomationListEntryInfo Entry = GetListEntryInfo(Widget, Index);
	if (!Entry.bSuccess)
	{
		if (UListView* ListView = Cast<UListView>(Widget))
		{
			if (Index >= 0 && Index < ListView->GetNumItems())
			{
				if (TSharedPtr<STableViewBase> TableView = StaticCastSharedPtr<STableViewBase>(ListView->GetCachedWidget()))
				{
					TableView->SetScrollOffset(Index);
					// A step of at least 1/12s makes FInterpTo (speed 12.0) reach the
					// target within this one tick, whether or not scrolling animates.
					TableView->Tick(TableView->GetTickSpaceGeometry(), FSlateApplication::Get().GetCurrentTime(), 1.0f);
				}
				Entry = GetListEntryInfo(Widget, Index);
			}
		}
	}
	if (!Entry.bSuccess)
	{
		UE_LOG(LogEditorAutomation, Warning,
			TEXT("ScrollToListEntry: row for item %d is still not available."), Index);
	}
	return Entry;
}

bool UEditorAutomationUMGLibrary::ScrollToListEntryAndSendClick(
	UWidget* Widget, int32 Index, const FEditorAutomationSendClickOptions& Options)
{
	// Probe first: a row already on screen was drawn by an earlier frame, so its
	// geometry and hit-test entry are in place and the click reaches it.
	FEditorAutomationListEntryInfo Entry = GetListEntryInfo(Widget, Index);
	if (Entry.bSuccess && Entry.EntryWidget != nullptr)
	{
		return UEditorAutomationInputLibrary::SendClick(Entry.EntryWidget, Options);
	}

	UListView* ListView = AsListView(Widget, TEXT("ScrollToListEntryAndSendClick"));
	if (ListView == nullptr)
	{
		return false;
	}
	if (Index < 0 || Index >= ListView->GetNumItems())
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("ScrollToListEntryAndSendClick: index %d is out of range (item count %d)."),
			Index, ListView->GetNumItems());
		return false;
	}

	Entry = ScrollToListEntry(Widget, Index);
	if (!Entry.bSuccess || Entry.EntryWidget == nullptr)
	{
		return false;
	}

	// The row exists now, but this call created it and no frame has drawn it yet:
	UE_LOG(LogEditorAutomation, Warning,
		TEXT("ScrollToListEntryAndSendClick: row for item %d was created by this call and is not drawn yet; call again to click it."),
		Index);
	return false;
}

FString UEditorAutomationUMGLibrary::CaptureWidgetScreenshot(
	UWidget* Widget, const FString& OutputPath, int32 Width, int32 Height)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CaptureWidgetScreenshot must be called on the game thread."));
		return FString();
	}
	if (Widget == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CaptureWidgetScreenshot: Widget must not be null."));
		return FString();
	}
	if (!FApp::CanEverRender())
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("CaptureWidgetScreenshot: rendering is unavailable in this build (headless/server)."));
		return FString();
	}

	// Prefer the live Slate widget; build it when the UMG widget was never laid out.
	TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		SlateWidget = Widget->TakeWidget();
	}
	if (!SlateWidget.IsValid())
	{
		UE_LOG(LogEditorAutomation, Error,
			TEXT("CaptureWidgetScreenshot: widget '%s' has no Slate widget to render."), *Widget->GetPathName());
		return FString();
	}

	FVector2D DrawSize;
	{
		const FVector2D CachedSize = SlateWidget->GetCachedGeometry().GetAbsoluteSize();
		const FVector2D DesiredSize = SlateWidget->GetDesiredSize();
		DrawSize = (Width > 0 && Height > 0)
			? FVector2D(static_cast<float>(Width), static_cast<float>(Height))
			: (CachedSize.X >= 1.f && CachedSize.Y >= 1.f ? CachedSize : DesiredSize);
		if (DrawSize.X < 1.f || DrawSize.Y < 1.f)
		{
			DrawSize = FVector2D(1200.f, 800.f);
		}
		DrawSize.X = FMath::Clamp(DrawSize.X, 64.f, 4096.f);
		DrawSize.Y = FMath::Clamp(DrawSize.Y, 64.f, 4096.f);
	}

	// Linear render into a linear-tagged target; PNG export applies exactly one sRGB encode.
	TSharedRef<FWidgetRenderer> WidgetRenderer = MakeShared<FWidgetRenderer>(/*bUseGammaCorrection=*/false, /*bInClearTarget=*/true);
	UTextureRenderTarget2D* RenderTarget = FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, /*bUseGammaCorrection=*/true);
	if (RenderTarget == nullptr)
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CaptureWidgetScreenshot: the widget renderer failed to create a render target."));
		return FString();
	}
	WidgetRenderer->DrawWidget(RenderTarget, SlateWidget.ToSharedRef(), DrawSize, 0.f, /*bDeferRenderTargetUpdate=*/false);

	FString FullPath = OutputPath.TrimStartAndEnd();
	if (FullPath.IsEmpty())
	{
		const FString ScreenshotDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
		IFileManager::Get().MakeDirectory(*ScreenshotDir, true);
		const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		FullPath = FPaths::Combine(ScreenshotDir, FString::Printf(TEXT("EditorAutomation_%s.png"), *Stamp));
	}
	else if (FPaths::GetExtension(FullPath).IsEmpty())
	{
		FullPath += TEXT(".png");
	}

	FBufferArchive Buffer;
	if (!FImageUtils::ExportRenderTarget2DAsPNG(RenderTarget, Buffer))
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CaptureWidgetScreenshot: failed to encode the rendered widget as PNG."));
		return FString();
	}
	if (FArchive* Writer = IFileManager::Get().CreateFileWriter(*FullPath))
	{
		Writer->Serialize(const_cast<uint8*>(Buffer.GetData()), Buffer.Num());
		delete Writer;
	}
	else
	{
		UE_LOG(LogEditorAutomation, Error, TEXT("CaptureWidgetScreenshot: failed to create file '%s'."), *FullPath);
		return FString();
	}
	return FullPath;
}