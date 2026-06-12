// Copyright Asamoto.
// Top-right workforce panel: one row per resource building with [-] count [+]
// buttons, plus the idle-villager pool readout, plus a Houses section showing
// each house's resident tier with upgrade/downgrade buttons (population tiers,
// see population_todos.md). Rows are rebuilt only when the building list or a
// house tier changes; the counts/button states poll the snapshot through
// attributes.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Sim/SimTypes.h"

class SVerticalBox;

class SWorkerPanel : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnWorkerChange, int32 /*BuildingIndex*/);
	DECLARE_DELEGATE_TwoParams(FOnHouseUpgrade, int32 /*BuildingIndex*/, ETier /*Target*/);

	SLATE_BEGIN_ARGS(SWorkerPanel) {}
		// Returns the latest snapshot each frame (may return nullptr).
		SLATE_ARGUMENT(TFunction<const FSimSnapshot*()>, GetSnapshot)
		SLATE_EVENT(FOnWorkerChange, OnAssign)
		SLATE_EVENT(FOnWorkerChange, OnUnassign)
		SLATE_EVENT(FOnHouseUpgrade, OnUpgradeHouse)
		SLATE_EVENT(FOnWorkerChange, OnDowngradeHouse)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Called every frame (cheap): rebuilds the rows only when the set of
	// resource buildings or a house tier changed.
	void Refresh();

private:
	void RebuildRows(const FSimSnapshot& Snap);
	void AddWorkerRow(int32 Index, EBuildingType Type, int32 Ordinal);
	void AddHouseRow(int32 Index, const FBuildingSnapshot& House, int32 Ordinal);
	static bool TakesWorkers(EBuildingType Type);

	TFunction<const FSimSnapshot*()> GetSnapshot;
	FOnWorkerChange OnAssign;
	FOnWorkerChange OnUnassign;
	FOnHouseUpgrade OnUpgradeHouse;
	FOnWorkerChange OnDowngradeHouse;

	TSharedPtr<SVerticalBox> RowsBox;
	TSharedPtr<SVerticalBox> HouseRowsBox;

	// (type, resident tier) per building — rows reflect this; rebuilt on change.
	TArray<uint16> BuiltSignature;
};
