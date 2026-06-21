// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#include "SEnginePatchPanel.h"
#include "UnrealEnginePatchSubsystem.h"
#include "EnginePatchManager.h"
#include "Editor.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"

#define LOCTEXT_NAMESPACE "SEnginePatchPanel"

void SEnginePatchPanel::Construct(const FArguments& InArgs)
{
	if (UUnrealEnginePatchSubsystem* Sub = GetSubsystem()) Sub->RefreshStatus();
	RefreshList();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4)
			[
				SNew(SButton)
				.Text(LOCTEXT("ApplyAll", "Apply All"))
				.OnClicked(this, &SEnginePatchPanel::OnApplyAll)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4)
			[
				SNew(SButton)
				.Text(LOCTEXT("UnpatchAll", "Unpatch All"))
				.OnClicked(this, &SEnginePatchPanel::OnUnpatchAll)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.OnClicked(this, &SEnginePatchPanel::OnRefreshStatus)
			]
			+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(4)
			[
				SNew(STextBlock).Text(this, &SEnginePatchPanel::GetEngineVersionText)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1)
		[
			SAssignNew(PatchListView, SListView<TSharedPtr<FEnginePatch>>)
			.ListItemsSource(&PatchItems)
			.OnGenerateRow(this, &SEnginePatchPanel::OnGeneratePatchRow)
			.HeaderRow(
				SNew(SHeaderRow)
				+ SHeaderRow::Column("PatchId").DefaultLabel(LOCTEXT("ColId", "Patch ID")).FillWidth(0.25f)
				+ SHeaderRow::Column("Description").DefaultLabel(LOCTEXT("ColDesc", "Description")).FillWidth(0.4f)
				+ SHeaderRow::Column("Plugin").DefaultLabel(LOCTEXT("ColPlugin", "Plugin")).FillWidth(0.15f)
				+ SHeaderRow::Column("Status").DefaultLabel(LOCTEXT("ColStatus", "Status")).FillWidth(0.1f)
				+ SHeaderRow::Column("Actions").DefaultLabel(LOCTEXT("ColActions", "Actions")).FillWidth(0.1f)
			)
		]
	];
}

void SEnginePatchPanel::RefreshList()
{
	PatchItems.Empty();
	if (UUnrealEnginePatchSubsystem* Sub = GetSubsystem())
	{
		for (FEnginePatch& Patch : Sub->Patches)
		{
			PatchItems.Add(MakeShared<FEnginePatch>(Patch));
		}
	}
	if (PatchListView.IsValid()) PatchListView->RequestListRefresh();
}

TSharedRef<ITableRow> SEnginePatchPanel::OnGeneratePatchRow(TSharedPtr<FEnginePatch> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FEnginePatch>>, OwnerTable)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.25f).Padding(2)
		[ SNew(STextBlock).Text(FText::FromString(Item->PatchId)) ]
		+ SHorizontalBox::Slot().FillWidth(0.4f).Padding(2)
		[ SNew(STextBlock).Text(FText::FromString(Item->Description)) ]
		+ SHorizontalBox::Slot().FillWidth(0.15f).Padding(2)
		[ SNew(STextBlock).Text(FText::FromString(Item->Plugin.IsEmpty() ? TEXT("-") : Item->Plugin)) ]
		+ SHorizontalBox::Slot().FillWidth(0.1f).Padding(2)
		[ SNew(STextBlock).Text(GetStatusText(Item->Status)).ColorAndOpacity(GetStatusColor(Item->Status)) ]
		+ SHorizontalBox::Slot().FillWidth(0.1f).Padding(2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Apply", "Apply"))
				.OnClicked(this, &SEnginePatchPanel::OnApplySingle, Item->PatchId)
				.IsEnabled(Item->Status == EPatchStatus::NotApplied || Item->Status == EPatchStatus::Error)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Unpatch", "Unpatch"))
				.OnClicked(this, &SEnginePatchPanel::OnUnpatchSingle, Item->PatchId)
				.IsEnabled(Item->Status == EPatchStatus::Applied || Item->Status == EPatchStatus::Error)
			]
		]
	];
}

FReply SEnginePatchPanel::OnApplyAll()
{
	if (UUnrealEnginePatchSubsystem* Sub = GetSubsystem())
	{
		TArray<FString> Errors;
		Sub->ApplyAll(Errors);
		for (const FString& E : Errors) UE_LOG(LogTemp, Error, TEXT("EnginePatch: %s"), *E);
	}
	RefreshList();
	return FReply::Handled();
}

FReply SEnginePatchPanel::OnUnpatchAll()
{
	if (UUnrealEnginePatchSubsystem* Sub = GetSubsystem())
	{
		TArray<FString> Errors;
		Sub->UnpatchAll(Errors);
		for (const FString& E : Errors) UE_LOG(LogTemp, Error, TEXT("EnginePatch: %s"), *E);
	}
	RefreshList();
	return FReply::Handled();
}

FReply SEnginePatchPanel::OnRefreshStatus()
{
	if (UUnrealEnginePatchSubsystem* Sub = GetSubsystem()) Sub->RefreshStatus();
	RefreshList();
	return FReply::Handled();
}

FReply SEnginePatchPanel::OnApplySingle(FString PatchId)
{
	if (UUnrealEnginePatchSubsystem* Sub = GetSubsystem())
	{
		FString Error;
		if (!Sub->ApplyPatch(PatchId, Error)) UE_LOG(LogTemp, Error, TEXT("EnginePatch: %s"), *Error);
	}
	RefreshList();
	return FReply::Handled();
}

FReply SEnginePatchPanel::OnUnpatchSingle(FString PatchId)
{
	if (UUnrealEnginePatchSubsystem* Sub = GetSubsystem())
	{
		FString Error;
		if (!Sub->UnpatchPatch(PatchId, Error)) UE_LOG(LogTemp, Error, TEXT("EnginePatch: %s"), *Error);
	}
	RefreshList();
	return FReply::Handled();
}

FText SEnginePatchPanel::GetStatusText(EPatchStatus Status) const
{
	switch (Status)
	{
	case EPatchStatus::Applied:        return LOCTEXT("Applied", "Applied");
	case EPatchStatus::NotApplied:     return LOCTEXT("NotApplied", "Not Applied");
	case EPatchStatus::NotApplicable:  return LOCTEXT("NotApplicable", "N/A");
	case EPatchStatus::Error:          return LOCTEXT("Error", "Error");
	default:                           return LOCTEXT("Unknown", "Unknown");
	}
}

FSlateColor SEnginePatchPanel::GetStatusColor(EPatchStatus Status) const
{
	switch (Status)
	{
	case EPatchStatus::Applied:       return FSlateColor(FLinearColor::Green);
	case EPatchStatus::NotApplied:    return FSlateColor(FLinearColor::Yellow);
	case EPatchStatus::NotApplicable: return FSlateColor(FLinearColor::Gray);
	case EPatchStatus::Error:         return FSlateColor(FLinearColor::Red);
	default:                          return FSlateColor(FLinearColor::White);
	}
}

FText SEnginePatchPanel::GetEngineVersionText() const
{
	return FText::Format(LOCTEXT("EngineVersion", "Engine: {0}"),
		FText::FromString(FEnginePatchManager::GetCurrentEngineVersion()));
}

UUnrealEnginePatchSubsystem* SEnginePatchPanel::GetSubsystem() const
{
	if (GEditor) return GEditor->GetEditorSubsystem<UUnrealEnginePatchSubsystem>();
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
