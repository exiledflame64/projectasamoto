// Copyright Asamoto.
// Top-right workforce panel: one row per resource building with [-] count [+]
// buttons, plus the idle-villager pool readout. Rows are rebuilt only when the
// building list changes; the counts poll the snapshot through attributes.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Sim/SimTypes.h"

class SVerticalBox;

class SWorkerPanel : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnWorkerChange, int32 /*BuildingIndex*/);

	SLATE_BEGIN_ARGS(SWorkerPanel) {}
		// Returns the latest snapshot each frame (may return nullptr).
		SLATE_ARGUMENT(TFunction<const FSimSnapshot*()>, GetSnapshot)
		SLATE_EVENT(FOnWorkerChange, OnAssign)
		SLATE_EVENT(FOnWorkerChange, OnUnassign)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Called every frame (cheap): rebuilds the rows only when the set of
	// resource buildings changed.
	void Refresh();

private:
	void RebuildRows(const FSimSnapshot& Snap);
	static bool TakesWorkers(EBuildingType Type);

	TFunction<const FSimSnapshot*()> GetSnapshot;
	FOnWorkerChange OnAssign;
	FOnWorkerChange OnUnassign;

	TSharedPtr<SVerticalBox> RowsBox;
	TArray<EBuildingType> BuiltSignature;   // building types the current rows reflect
};
