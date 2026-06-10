// Copyright Asamoto.
// Top-left resource readout (wood / planks / food / population) plus the
// centered game-over banner. Pure display: polls its text attributes each
// frame, never interacts with input (HitTestInvisible).

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SResourcePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SResourcePanel) {}
		SLATE_ATTRIBUTE(FText, ResourceText)
		SLATE_ATTRIBUTE(EVisibility, GameOverVisibility)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
