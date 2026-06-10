// Copyright Asamoto.

#include "SWorkerPanel.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
	FText BuildingLabel(EBuildingType Type, int32 OrdinalOfType)
	{
		FText Name;
		switch (Type)
		{
		case EBuildingType::Lumberyard: Name = NSLOCTEXT("Realm", "WP_Lumberyard", "Lumberyard"); break;
		case EBuildingType::Sawmill:    Name = NSLOCTEXT("Realm", "WP_Sawmill",    "Sawmill");    break;
		case EBuildingType::Farm:       Name = NSLOCTEXT("Realm", "WP_Farm",       "Farm");       break;
		default:                        Name = NSLOCTEXT("Realm", "WP_Building",   "Building");   break;
		}
		return FText::Format(NSLOCTEXT("Realm", "WP_RowLabel", "{0} {1}"), Name, OrdinalOfType);
	}
}

bool SWorkerPanel::TakesWorkers(EBuildingType Type)
{
	return Type == EBuildingType::Lumberyard ||
	       Type == EBuildingType::Sawmill ||
	       Type == EBuildingType::Farm;
}

void SWorkerPanel::Construct(const FArguments& InArgs)
{
	GetSnapshot = InArgs._GetSnapshot;
	OnAssign    = InArgs._OnAssign;
	OnUnassign  = InArgs._OnUnassign;

	// Full-screen overlay slot: keep the empty area out of hit testing.
	SetVisibility(EVisibility::SelfHitTestInvisible);

	ChildSlot
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Top)
	.Padding(16.f)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.6f))
		.Padding(FMargin(12.f, 8.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
				.Text(TAttribute<FText>::CreateLambda([this]
				{
					const FSimSnapshot* S = GetSnapshot ? GetSnapshot() : nullptr;
					return FText::Format(
						NSLOCTEXT("Realm", "WP_Header", "Workers — idle: {0}"),
						S ? S->IdleVillagers : 0);
				}))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(RowsBox, SVerticalBox)
			]
		]
	];
}

void SWorkerPanel::Refresh()
{
	const FSimSnapshot* S = GetSnapshot ? GetSnapshot() : nullptr;
	if (!S || !RowsBox.IsValid())
	{
		return;
	}

	// Rebuild only when the building list changed (append-only, so a type
	// signature comparison is enough).
	TArray<EBuildingType> Signature;
	for (const FBuildingSnapshot& B : S->Buildings)
	{
		Signature.Add(B.Type);
	}
	if (Signature == BuiltSignature)
	{
		return;
	}
	BuiltSignature = MoveTemp(Signature);
	RebuildRows(*S);
}

void SWorkerPanel::RebuildRows(const FSimSnapshot& Snap)
{
	RowsBox->ClearChildren();

	TMap<EBuildingType, int32> OrdinalCounter;
	for (int32 i = 0; i < Snap.Buildings.Num(); ++i)
	{
		const EBuildingType Type = Snap.Buildings[i].Type;
		if (!TakesWorkers(Type))
		{
			continue;
		}
		const int32 Ordinal = ++OrdinalCounter.FindOrAdd(Type);
		const int32 Index = i;   // captured by the row's lambdas

		RowsBox->AddSlot()
		.AutoHeight()
		.Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
				.Text(BuildingLabel(Type, Ordinal))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ContentPadding(FMargin(8.f, 2.f))
				.OnClicked_Lambda([this, Index] { OnUnassign.ExecuteIfBound(Index); return FReply::Handled(); })
				.IsEnabled(TAttribute<bool>::CreateLambda([this, Index]
				{
					const FSimSnapshot* S = GetSnapshot ? GetSnapshot() : nullptr;
					return S && S->Buildings.IsValidIndex(Index) &&
						S->Buildings[Index].AssignedWorkers > 0;
				}))
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("-")))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.MinDesiredWidth(44.f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
					.Text(TAttribute<FText>::CreateLambda([this, Index]
					{
						const FSimSnapshot* S = GetSnapshot ? GetSnapshot() : nullptr;
						if (S && S->Buildings.IsValidIndex(Index))
						{
							return FText::Format(NSLOCTEXT("Realm", "WP_Count", "{0} / {1}"),
								S->Buildings[Index].AssignedWorkers,
								S->Buildings[Index].MaxWorkers);
						}
						return FText::GetEmpty();
					}))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ContentPadding(FMargin(8.f, 2.f))
				.OnClicked_Lambda([this, Index] { OnAssign.ExecuteIfBound(Index); return FReply::Handled(); })
				.IsEnabled(TAttribute<bool>::CreateLambda([this, Index]
				{
					const FSimSnapshot* S = GetSnapshot ? GetSnapshot() : nullptr;
					return S && S->IdleVillagers > 0 && S->Buildings.IsValidIndex(Index) &&
						S->Buildings[Index].AssignedWorkers < S->Buildings[Index].MaxWorkers;
				}))
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("+")))
				]
			]
		];
	}
}
