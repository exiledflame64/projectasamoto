// Copyright Asamoto.
// Player-facing blueprint catalog: the things the player can order placed into
// the sim. Data-driven (struct entries with an availability flag) rather than
// UENUM metadata specifiers, because UMETA() metadata is editor-only — it is
// stripped from shipping builds, so runtime UI cannot rely on it. Future
// blueprints are added here with bAvailable = false and show up greyed out.

#pragma once

#include "CoreMinimal.h"
#include "Sim/SimTypes.h"

enum class EBlueprintKind : uint8
{
	None,
	House,       // grants 1 idle villager
	Warehouse,   // settlement stockpile; max 1 (bar greys it out once placed)
	Lumberyard,
	Sawmill,     // logs -> planks
	Farm,        // food source (workers tend the attached field)
	Road         // arms the road build tool (not a building; see Roads/)
};

struct FBlueprintDef
{
	EBlueprintKind Kind = EBlueprintKind::None;
	EBuildingType  BuildingType = EBuildingType::None;   // what placement spawns
	FText          DisplayName;
	FText          Tooltip;
	bool           bAvailable = false;   // false = "coming soon", greyed out in UI
};

// Static catalog; order defines UI order.
REALM_API const TArray<FBlueprintDef>& GetBlueprintCatalog();
REALM_API const FBlueprintDef* FindBlueprintDef(EBlueprintKind Kind);
