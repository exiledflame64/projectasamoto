// Copyright Asamoto.

#include "SWorkerPanel.h"

#include "Sim/SimWorld.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
	FText TierName(ETier Tier)
	{
		switch (Tier)
		{
		case ETier::Peasant:     return NSLOCTEXT("Realm", "Tier_Peasant",     "Peasant");
		case ETier::Artisan:     return NSLOCTEXT("Realm", "Tier_Artisan",     "Artisan");
		case ETier::Samurai:     return NSLOCTEXT("Realm", "Tier_Samurai",     "Samurai");
		case ETier::Monk:        return NSLOCTEXT("Realm", "Tier_Monk",        "Monk");
		case ETier::WarriorMonk: return NSLOCTEXT("Realm", "Tier_WarriorMonk", "Warrior Monk");
		default:                 return FText::GetEmpty();
		}
	}

	FText ResourceName(EResource Resource)
	{
		switch (Resource)
		{
		case EResource::Log:   return NSLOCTEXT("Realm", "Res_Log",   "logs");
		case EResource::Plank: return NSLOCTEXT("Realm", "Res_Plank", "planks");
		case EResource::Food:  return NSLOCTEXT("Realm", "Res_Food",  "food");
		default:               return FText::GetEmpty();
		}
	}

	FText BuildingName(EBuildingType Type)
	{
		switch (Type)
		{
		case EBuildingType::Lumberyard: return NSLOCTEXT("Realm", "WP_Lumberyard", "Lumberyard");
		case EBuildingType::Sawmill:    return NSLOCTEXT("Realm", "WP_Sawmill",    "Sawmill");
		case EBuildingType::Farm:       return NSLOCTEXT("Realm", "WP_Farm",       "Farm");
		case EBuildingType::Temple:     return NSLOCTEXT("Realm", "WP_Temple",     "Temple");
		case EBuildingType::Dojo:       return NSLOCTEXT("Realm", "WP_Dojo",       "Dojo");
		case EBuildingType::House:      return NSLOCTEXT("Realm", "WP_House",      "House");
		default:                        return NSLOCTEXT("Realm", "WP_Building",   "Building");
		}
	}

	FText BuildingLabel(EBuildingType Type, int32 OrdinalOfType)
	{
		return FText::Format(NSLOCTEXT("Realm", "WP_RowLabel", "{0} {1}"),
			BuildingName(Type), OrdinalOfType);
	}

	// "Monk, Warrior Monk" from a TierBit mask.
	FText TierMaskText(uint8 Mask)
	{
		FString Out;
		for (int32 t = 0; t < NumTiers; ++t)
		{
			if (Mask & TierBit(static_cast<ETier>(t)))
			{
				if (!Out.IsEmpty())
				{
					Out += TEXT(", ");
				}
				Out += TierName(static_cast<ETier>(t)).ToString();
			}
		}
		return FText::FromString(Out);
	}

	// The (unique) rule arriving at To — the tier graph is a tree.
	const FHouseUpgradeRule* FindRuleTo(ETier From, ETier To)
	{
		for (const FHouseUpgradeRule& Rule : GetHouseUpgradeRules())
		{
			if (Rule.From == From && Rule.To == To)
			{
				return &Rule;
			}
		}
		return nullptr;
	}

	// "4 planks, 2 food" from a rule's cost list.
	FText CostText(const FHouseUpgradeRule& Rule)
	{
		FString Out;
		for (const FResourceCost& Cost : Rule.Costs)
		{
			if (Cost.Amount <= 0)
			{
				continue;
			}
			if (!Out.IsEmpty())
			{
				Out += TEXT(", ");
			}
			Out += FString::Printf(TEXT("%d %s"), Cost.Amount,
				*ResourceName(Cost.Resource).ToString());
		}
		if (Out.IsEmpty())
		{
			Out = NSLOCTEXT("Realm", "WP_Free", "free").ToString();
		}
		return FText::FromString(Out);
	}
}

bool SWorkerPanel::TakesWorkers(EBuildingType Type)
{
	return FSimWorld::MaxWorkersFor(Type) > 0;
}

void SWorkerPanel::Construct(const FArguments& InArgs)
{
	GetSnapshot      = InArgs._GetSnapshot;
	OnAssign         = InArgs._OnAssign;
	OnUnassign       = InArgs._OnUnassign;
	OnUpgradeHouse   = InArgs._OnUpgradeHouse;
	OnDowngradeHouse = InArgs._OnDowngradeHouse;

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

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(HouseRowsBox, SVerticalBox)
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

	// Rebuild only when the building list or a house tier changed (the list is
	// append-only, so a (type, tier) signature comparison is enough). Button
	// enable states and tooltips poll the snapshot, so they need no rebuild.
	TArray<uint16> Signature;
	for (const FBuildingSnapshot& B : S->Buildings)
	{
		Signature.Add(static_cast<uint16>(B.Type) << 8 |
			static_cast<uint16>(B.ResidentTier));
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
	HouseRowsBox->ClearChildren();

	TMap<EBuildingType, int32> OrdinalCounter;
	bool bAnyHouse = false;
	for (int32 i = 0; i < Snap.Buildings.Num(); ++i)
	{
		const EBuildingType Type = Snap.Buildings[i].Type;
		if (Type == EBuildingType::House)
		{
			if (!bAnyHouse)
			{
				bAnyHouse = true;
				HouseRowsBox->AddSlot()
				.AutoHeight()
				.Padding(0.f, 8.f, 0.f, 2.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
					.Text(NSLOCTEXT("Realm", "WP_Houses", "Houses"))
				];
			}
			AddHouseRow(i, Snap.Buildings[i], ++OrdinalCounter.FindOrAdd(Type));
		}
		else if (TakesWorkers(Type))
		{
			AddWorkerRow(i, Type, ++OrdinalCounter.FindOrAdd(Type));
		}
	}
}

void SWorkerPanel::AddWorkerRow(int32 Index, EBuildingType Type, int32 Ordinal)
{
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
			.ToolTipText(FText::Format(
				NSLOCTEXT("Realm", "WP_RequiresTiers", "Workers: {0}"),
				TierMaskText(FSimWorld::AllowedTiersFor(Type))))
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
				// Greyed when full OR when no idle villager of an eligible
				// tier exists (e.g. a dojo with no idle samurai).
				const FSimSnapshot* S = GetSnapshot ? GetSnapshot() : nullptr;
				if (!S || !S->Buildings.IsValidIndex(Index) ||
					S->Buildings[Index].AssignedWorkers >= S->Buildings[Index].MaxWorkers)
				{
					return false;
				}
				const uint8 Allowed = S->Buildings[Index].AllowedTiers;
				for (int32 t = 0; t < NumTiers; ++t)
				{
					if ((Allowed & TierBit(static_cast<ETier>(t))) && S->IdleByTier[t] > 0)
					{
						return true;
					}
				}
				return false;
			}))
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("+")))
			]
		]
	];
}

void SWorkerPanel::AddHouseRow(int32 Index, const FBuildingSnapshot& House, int32 Ordinal)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
			.Text(FText::Format(NSLOCTEXT("Realm", "WP_HouseLabel", "{0} — {1}"),
				BuildingLabel(EBuildingType::House, Ordinal), TierName(House.ResidentTier)))
		];

	// One button per outgoing tier edge (a Peasant house shows two: the
	// artisan ladder and the monk ladder). Greyed with a fail-reason tooltip
	// while CanUpgradeHouse rejects.
	for (int32 e = 0; e < House.NumUpgradeEdges; ++e)
	{
		const ETier Target = House.UpgradeTo[e];
		const FHouseUpgradeRule* Rule = FindRuleTo(House.ResidentTier, Target);

		FText Tooltip = Rule
			? FText::Format(NSLOCTEXT("Realm", "WP_UpgradeTip", "Promote to {0}.\nCost: {1}{2}"),
				TierName(Target), CostText(*Rule),
				Rule->RequiredBuilding != EBuildingType::None
					? FText::Format(NSLOCTEXT("Realm", "WP_UpgradeReq", "\nRequires: {0}"),
						BuildingName(Rule->RequiredBuilding))
					: FText::GetEmpty())
			: FText::GetEmpty();

		Row->AddSlot()
		.AutoWidth()
		.Padding(2.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.ContentPadding(FMargin(8.f, 2.f))
			.ToolTipText(TAttribute<FText>::CreateLambda([this, Index, e, Tooltip]
			{
				// Append the live fail reason while the button is greyed.
				const FSimSnapshot* S = GetSnapshot ? GetSnapshot() : nullptr;
				if (!S || !S->Buildings.IsValidIndex(Index) ||
					e >= S->Buildings[Index].NumUpgradeEdges)
				{
					return Tooltip;
				}
				switch (S->Buildings[Index].UpgradeFail[e])
				{
				case EUpgradeFail::MissingBuilding:
					return FText::Format(NSLOCTEXT("Realm", "WP_FailBuilding",
						"{0}\n(blocked: required building missing)"), Tooltip);
				case EUpgradeFail::NotEnoughResources:
					return FText::Format(NSLOCTEXT("Realm", "WP_FailResources",
						"{0}\n(blocked: not enough resources)"), Tooltip);
				default:
					return Tooltip;
				}
			}))
			.OnClicked_Lambda([this, Index, Target]
			{
				OnUpgradeHouse.ExecuteIfBound(Index, Target);
				return FReply::Handled();
			})
			.IsEnabled(TAttribute<bool>::CreateLambda([this, Index, e]
			{
				const FSimSnapshot* S = GetSnapshot ? GetSnapshot() : nullptr;
				return S && S->Buildings.IsValidIndex(Index) &&
					e < S->Buildings[Index].NumUpgradeEdges &&
					S->Buildings[Index].UpgradeFail[e] == EUpgradeFail::None;
			}))
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
				.Text(FText::Format(NSLOCTEXT("Realm", "WP_UpgradeBtn", "▲ {0}"),
					TierName(Target)))
			]
		];
	}

	if (House.bCanDowngrade)
	{
		// The tier tree makes the previous tier unambiguous; show it.
		ETier Prev = ETier::Peasant;
		for (const FHouseUpgradeRule& Rule : GetHouseUpgradeRules())
		{
			if (Rule.To == House.ResidentTier)
			{
				Prev = Rule.From;
				break;
			}
		}

		Row->AddSlot()
		.AutoWidth()
		.Padding(2.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.ContentPadding(FMargin(8.f, 2.f))
			.ToolTipText(FText::Format(NSLOCTEXT("Realm", "WP_DowngradeTip",
				"Revert to {0}. Refunds the upgrade cost.\nResidents leave workplaces their new tier cannot work."),
				TierName(Prev)))
			.OnClicked_Lambda([this, Index]
			{
				OnDowngradeHouse.ExecuteIfBound(Index);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
				.Text(FText::FromString(TEXT("▼")))
			]
		];
	}

	HouseRowsBox->AddSlot()
	.AutoHeight()
	.Padding(0.f, 2.f)
	[
		Row
	];
}
