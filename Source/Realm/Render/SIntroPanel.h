// Copyright Asamoto.
// TEMPORARY onboarding window: left side of the screen, under the resource
// readout. Shows the introduction text (mirrors introduction.md), scrollable,
// closeable via the corner X (the game keeps running while it is open).
// To remove the feature: delete SIntroPanel.h/.cpp and the two blocks marked
// "TEMP intro window" in RealmPlayerController.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SIntroPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SIntroPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnCloseClicked();
};
