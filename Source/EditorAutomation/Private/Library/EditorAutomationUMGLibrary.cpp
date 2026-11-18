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

FEditorAutomationResult UEditorAutomationUMGLibrary::GetWidgetRoots(TArray<FString>& OutRoots)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("GetWidgetRoots must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->GetWorld() == nullptr) continue;
		if (Widget->GetWorld()->WorldType != EWorldType::PIE) continue;
		if (!Widget->GetCachedWidget().IsValid()) continue;
		if (!Widget->IsInViewport()) continue;
		OutRoots.AddUnique(Widget->GetPathName());
	}
	OutRoots.Sort();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGLibrary::GetWidgetBlueprintPath(UWidget* Widget, FString& OutBlueprintPath)
{
	if (Widget == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is null."),
			TEXT("Pass a UWidget object path; use GetWidgetRoots to find one."));
	}
	UUserWidget* UserWidget = Cast<UUserWidget>(Widget);
	if (UserWidget == nullptr)
	{
		UserWidget = Widget->GetTypedOuter<UUserWidget>();
	}
	OutBlueprintPath = BlueprintPathOf(UserWidget);
	if (OutBlueprintPath.IsEmpty())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotFound,
			FString::Printf(TEXT("Widget '%s' belongs to no UserWidget Blueprint."), *Widget->GetPathName()),
			TEXT("Pass a widget that is part of a WidgetBlueprint tree."));
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGLibrary::GetWidgetClassPath(UWidget* Widget, FString& OutClassPath)
{
	if (Widget == nullptr || Widget->GetClass() == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is null."),
			TEXT("Pass a UWidget object path; use GetWidgetRoots to find one."));
	}
	OutClassPath = Widget->GetClass()->GetPathName();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGLibrary::GetWidgetTreeDiff(
	UUserWidget* RootWidget,
	FEditorAutomationWidgetTreeDiff& OutDiff)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("GetWidgetTreeDiff must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	if (RootWidget == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("RootWidget is null."),
			TEXT("Pass a UUserWidget object path from GetWidgetRoots."));
	}

	FEditorAutomationWidgetTreeDiff Diff;
	Diff.RootObjectPath = RootWidget->GetPathName();
	UWidget* Start = RootWidget->WidgetTree != nullptr ? RootWidget->WidgetTree->RootWidget : nullptr;
	if (Start == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			FString::Printf(TEXT("Root widget '%s' has no WidgetTree root."), *RootWidget->GetPathName()),
			TEXT("Verify the widget is fully constructed before reading its tree."));
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
	OutDiff = MoveTemp(Diff);
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGLibrary::ResetWidgetTreeCache(UUserWidget* RootWidget)
{
	if (RootWidget == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("RootWidget is null."),
			TEXT("Pass a UUserWidget object path from GetWidgetRoots."));
	}
	GWidgetTreeCache.Remove(RootWidget->GetPathName());
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGLibrary::GetWidgetGeometry(
	UWidget* Widget,
	FEditorAutomationWidgetGeometry& OutGeometry)
{
	if (Widget == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is null."),
			TEXT("Pass a UWidget object path; use GetWidgetRoots to find one."));
	}
	const TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			FString::Printf(TEXT("Widget '%s' has no Slate widget yet."), *Widget->GetPathName()),
			TEXT("Wait for the widget to be constructed/visible, then retry."));
	}
	const FGeometry& CachedGeometry = SlateWidget->GetCachedGeometry();
	if (CachedGeometry.GetAbsoluteSize().IsNearlyZero())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			FString::Printf(TEXT("Widget '%s' is not laid out yet (zero size)."), *Widget->GetPathName()),
			TEXT("Wait for the next frame/paint pass, then retry."));
	}
	OutGeometry.AbsolutePosition = CachedGeometry.GetAbsolutePosition();
	OutGeometry.AbsoluteSize = CachedGeometry.GetAbsoluteSize();
	OutGeometry.LocalSize = CachedGeometry.GetLocalSize();
	OutGeometry.LayoutScale = CachedGeometry.GetAccumulatedLayoutTransform().GetScale();
	return FEditorAutomationResult::Success();
}

namespace
{
	/** Resolve a widget that must be a UMG UListView; the failure is returned in the envelope. */
	FEditorAutomationResult RequireListView(UWidget* Widget, const TCHAR* FunctionName, UListView*& OutListView)
	{
		if (Widget == nullptr)
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::InvalidArgument,
				TEXT("Widget is null."),
				TEXT("Pass a UListView widget object path."));
		}
		OutListView = Cast<UListView>(Widget);
		if (OutListView == nullptr)
		{
			return FEditorAutomationResult::Failure(
				EEditorAutomationResultCode::WrongType,
				FString::Printf(TEXT("%s: widget '%s' is not a UListView."), FunctionName, *Widget->GetPathName()),
				TEXT("List introspection requires UMG's UListView."));
		}
		return FEditorAutomationResult::Success();
	}
}

FEditorAutomationResult UEditorAutomationUMGLibrary::GetListViewInfo(
	UWidget* Widget,
	const int32 MaxItems,
	FEditorAutomationListViewInfo& OutInfo)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("GetListViewInfo must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	UListView* ListView = nullptr;
	const FEditorAutomationResult Required = RequireListView(Widget, TEXT("GetListViewInfo"), ListView);
	if (!Required.bSuccess)
	{
		return Required;
	}

	OutInfo.ItemCount = ListView->GetNumItems();
	const int32 Limit = (MaxItems > 0) ? FMath::Min(MaxItems, OutInfo.ItemCount) : OutInfo.ItemCount;
	for (int32 Index = 0; Index < Limit; ++Index)
	{
		UObject* Item = ListView->GetItemAt(Index);
		FEditorAutomationListItemInfo ItemInfo;
		ItemInfo.Index = Index;
		ItemInfo.Item = Item;
		ItemInfo.ItemClass = (Item != nullptr) ? Item->GetClass() : nullptr;
		OutInfo.Items.Add(MoveTemp(ItemInfo));
	}
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGLibrary::GetListEntryInfo(
	UWidget* Widget,
	const int32 Index,
	FEditorAutomationListEntryInfo& OutEntry)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("GetListEntryInfo must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	UListView* ListView = nullptr;
	const FEditorAutomationResult Required = RequireListView(Widget, TEXT("GetListEntryInfo"), ListView);
	if (!Required.bSuccess)
	{
		return Required;
	}
	const int32 ItemCount = ListView->GetNumItems();
	if (Index < 0 || Index >= ItemCount)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			FString::Printf(TEXT("Index %d is out of range (item count %d)."), Index, ItemCount),
			TEXT("Pass a valid item index; call GetListViewInfo to read the item count."));
	}
	UObject* Item = ListView->GetItemAt(Index);
	UUserWidget* EntryWidget = (Item != nullptr) ? ListView->GetEntryWidgetFromItem(Item) : nullptr;
	if (EntryWidget == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			FString::Printf(TEXT("Item %d has no row widget (it is virtualized out of view)."), Index),
			TEXT("Call ScrollToListEntry first to bring the item on screen."));
	}
	OutEntry.Index = Index;
	OutEntry.EntryWidget = EntryWidget;
	OutEntry.EntryWidgetClass = EntryWidget->GetClass();
	return FEditorAutomationResult::Success();
}

FEditorAutomationResult UEditorAutomationUMGLibrary::ScrollToListEntry(
	UWidget* Widget,
	const int32 Index,
	FEditorAutomationListEntryInfo& OutEntry)
{
	FEditorAutomationResult Result = GetListEntryInfo(Widget, Index, OutEntry);
	if (Result.bSuccess)
	{
		return Result;
	}
	if (Result.ErrorCode != EEditorAutomationResultCode::NotReady)
	{
		return Result;
	}

	if (UListView* ListView = Cast<UListView>(Widget))
	{
		if (Index >= 0 && Index < ListView->GetNumItems())
		{
			if (TSharedPtr<STableViewBase> TableView = StaticCastSharedPtr<STableViewBase>(ListView->GetCachedWidget()))
			{
				TableView->SetScrollOffset(Index);
				// A step of at least 1/12s makes FInterpTo (speed 12.0) reach the target within
				// this one tick, whether or not scrolling animates.
				TableView->Tick(TableView->GetTickSpaceGeometry(), FSlateApplication::Get().GetCurrentTime(), 1.0f);
			}
			return GetListEntryInfo(Widget, Index, OutEntry);
		}
	}
	return Result;
}

FEditorAutomationResult UEditorAutomationUMGLibrary::ScrollToListEntryAndSendClick(
	UWidget* Widget,
	const int32 Index,
	const FEditorAutomationSendClickOptions& Options)
{
	// Probe first: a row already on screen was drawn by an earlier frame, so its geometry and
	// hit-test entry are in place and the click reaches it.
	FEditorAutomationListEntryInfo Entry;
	FEditorAutomationResult Result = GetListEntryInfo(Widget, Index, Entry);
	if (Result.bSuccess && Entry.EntryWidget != nullptr)
	{
		return UEditorAutomationInputLibrary::SendClick(Entry.EntryWidget, Options);
	}
	if (!Result.bSuccess && Result.ErrorCode != EEditorAutomationResultCode::NotReady)
	{
		return Result;
	}

	Result = ScrollToListEntry(Widget, Index, Entry);
	if (!Result.bSuccess || Entry.EntryWidget == nullptr)
	{
		return Result;
	}

	// The row exists now, but this call created it and no frame has drawn it yet, so the click
	// cannot reach it. Report that; the next call clicks the now-drawn row.
	return FEditorAutomationResult::Failure(
		EEditorAutomationResultCode::NotReady,
		FString::Printf(TEXT("Row for item %d was created by this call and is not drawn yet."), Index),
		TEXT("Call again to click the now-drawn row."));
}

FEditorAutomationResult UEditorAutomationUMGLibrary::CaptureWidgetScreenshot(
	UWidget* Widget,
	const FString& OutputPath,
	const int32 Width,
	const int32 Height,
	FString& OutFilePath)
{
	if (!IsInGameThread())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("CaptureWidgetScreenshot must be called on the game thread."),
			TEXT("Retry from the editor's game thread."));
	}
	if (Widget == nullptr)
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InvalidArgument,
			TEXT("Widget is null."),
			TEXT("Pass a UWidget object path; use GetWidgetRoots to find one."));
	}
	if (!FApp::CanEverRender())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			TEXT("Rendering is unavailable in this build (headless/server)."),
			TEXT("Screenshots require a rendering-capable editor."));
	}

	// Prefer the live Slate widget; build it when the UMG widget was never laid out.
	TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		SlateWidget = Widget->TakeWidget();
	}
	if (!SlateWidget.IsValid())
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::NotReady,
			FString::Printf(TEXT("Widget '%s' has no Slate widget to render."), *Widget->GetPathName()),
			TEXT("Wait for the widget to be constructed/visible, then retry."));
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
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InternalError,
			TEXT("The widget renderer failed to create a render target."),
			TEXT("This is an EditorAutomation/engine bug; report the widget size and render settings."));
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
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::InternalError,
			TEXT("Failed to encode the rendered widget as PNG."),
			TEXT("This is an EditorAutomation/engine bug; report the widget and render settings."));
	}
	if (FArchive* Writer = IFileManager::Get().CreateFileWriter(*FullPath))
	{
		Writer->Serialize(const_cast<uint8*>(Buffer.GetData()), Buffer.Num());
		delete Writer;
	}
	else
	{
		return FEditorAutomationResult::Failure(
			EEditorAutomationResultCode::EditorRefused,
			FString::Printf(TEXT("Failed to create file '%s'."), *FullPath),
			TEXT("Check that the output directory exists and is writable."));
	}
	OutFilePath = FullPath;
	return FEditorAutomationResult::Success();
}