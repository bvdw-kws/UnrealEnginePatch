// Copyright (C) 2024 Van de Walle Bastien
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once
#include "Widgets/SCompoundWidget.h"
#include "EnginePatchTypes.h"

class UUnrealEnginePatchSubsystem;

class SEnginePatchPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEnginePatchPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<ITableRow> OnGeneratePatchRow(TSharedPtr<FEnginePatch> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void RefreshList();

	FReply OnApplyAll();
	FReply OnUnpatchAll();
	FReply OnRefreshStatus();
	FReply OnApplySingle(FString PatchId);
	FReply OnUnpatchSingle(FString PatchId);

	FText GetStatusText(EPatchStatus Status) const;
	FSlateColor GetStatusColor(EPatchStatus Status) const;
	FText GetEngineVersionText() const;

	TArray<TSharedPtr<FEnginePatch>> PatchItems;
	TSharedPtr<SListView<TSharedPtr<FEnginePatch>>> PatchListView;

	UUnrealEnginePatchSubsystem* GetSubsystem() const;
};
