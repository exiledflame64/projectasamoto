// Copyright Asamoto.
// Bottom-center Slate toolbar listing the blueprint catalog. Clicking a button
// reports the kind to the owner (the player controller decides selection);
// the bar only renders state pushed back via SetSelected / SetEntryEnabled.
// Slate consumes clicks on the buttons, so UI clicks never reach ground placement.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "BlueprintCatalog.h"

class SBlueprintBar : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnBlueprintClicked, EBlueprintKind);

	SLATE_BEGIN_ARGS(SBlueprintBar) {}
		SLATE_EVENT(FOnBlueprintClicked, OnBlueprintClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetSelected(EBlueprintKind Kind) { Selected = Kind; }

	// Runtime override on top of the catalog's bAvailable (e.g. a blueprint
	// whose unlock condition lapses can grey out at runtime).
	void SetEntryEnabled(EBlueprintKind Kind, bool bEnabled) { RuntimeEnabled.Add(Kind, bEnabled); }

private:
	bool IsEntryEnabled(EBlueprintKind Kind) const;

	FOnBlueprintClicked OnBlueprintClicked;
	EBlueprintKind Selected = EBlueprintKind::None;
	TMap<EBlueprintKind, bool> RuntimeEnabled;
};
