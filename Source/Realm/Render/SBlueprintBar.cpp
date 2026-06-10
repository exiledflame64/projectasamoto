// Copyright Asamoto.

#include "SBlueprintBar.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

void SBlueprintBar::Construct(const FArguments& InArgs)
{
	OnBlueprintClicked = InArgs._OnBlueprintClicked;

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	for (const FBlueprintDef& Def : GetBlueprintCatalog())
	{
		const EBlueprintKind Kind = Def.Kind;
		const FText Label = Def.bAvailable
			? Def.DisplayName
			: FText::Format(NSLOCTEXT("Realm", "BP_Soon", "{0} (soon)"), Def.DisplayName);

		Row->AddSlot()
		.AutoWidth()
		.Padding(4.f, 0.f)
		[
			SNew(SButton)
			.IsEnabled(TAttribute<bool>::CreateLambda([this, Kind] { return IsEntryEnabled(Kind); }))
			.ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this, Kind]() -> FSlateColor
			{
				if (!IsEntryEnabled(Kind))
				{
					return FLinearColor(0.35f, 0.35f, 0.35f);
				}
				return Selected == Kind
					? FLinearColor(0.25f, 0.65f, 1.f)   // selected: blue highlight
					: FLinearColor::White;
			}))
			.ContentPadding(FMargin(18.f, 10.f))
			.OnClicked_Lambda([this, Kind]
			{
				OnBlueprintClicked.ExecuteIfBound(Kind);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.Text(Label)
			]
		];
	}

	// The bar spans the whole viewport overlay slot; keep the full-screen self
	// out of hit testing so only the actual buttons intercept clicks/focus.
	SetVisibility(EVisibility::SelfHitTestInvisible);

	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Bottom)
	.Padding(FMargin(0.f, 0.f, 0.f, 24.f))
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.6f))
		.Padding(8.f)
		[
			Row
		]
	];
}

bool SBlueprintBar::IsEntryEnabled(EBlueprintKind Kind) const
{
	for (const FBlueprintDef& Def : GetBlueprintCatalog())
	{
		if (Def.Kind == Kind)
		{
			if (!Def.bAvailable)
			{
				return false;
			}
			const bool* Override = RuntimeEnabled.Find(Kind);
			return Override ? *Override : true;
		}
	}
	return false;
}
