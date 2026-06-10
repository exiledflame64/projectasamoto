// Copyright Asamoto.

#include "SResourcePanel.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

void SResourcePanel::Construct(const FArguments& InArgs)
{
	// Display-only overlay: never intercept clicks meant for the world.
	SetVisibility(EVisibility::HitTestInvisible);

	ChildSlot
	[
		SNew(SOverlay)

		// Resource readout, top-left.
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(16.f)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.6f))
			.Padding(FMargin(12.f, 8.f))
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
				.Text(InArgs._ResourceText)
			]
		]

		// Pause indicator, top-center.
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0.f, 16.f)
		[
			SNew(SBorder)
			.Visibility(InArgs._PausedVisibility)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.6f))
			.Padding(FMargin(16.f, 8.f))
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				.ColorAndOpacity(FLinearColor(1.f, 0.85f, 0.2f))
				.Text(NSLOCTEXT("Realm", "Paused", "PAUSED  (Space)"))
			]
		]

		// Game-over banner, centered.
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.Visibility(InArgs._GameOverVisibility)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.75f))
			.Padding(FMargin(32.f, 20.f))
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
				.ColorAndOpacity(FLinearColor(0.95f, 0.2f, 0.15f))
				.Text(NSLOCTEXT("Realm", "GameOver", "GAME OVER — the settlement starved"))
			]
		]
	];
}
