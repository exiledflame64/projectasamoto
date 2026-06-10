// Copyright Asamoto.
// Player-facing blueprint catalog: the things the player can order placed into
// the sim. Data-driven (struct entries with an availability flag) rather than
// UENUM metadata specifiers, because UMETA() metadata is editor-only — it is
// stripped from shipping builds, so runtime UI cannot rely on it. Future
// blueprints are added here with bAvailable = false and show up greyed out.

#pragma once

#include "CoreMinimal.h"

enum class EBlueprintKind : uint8
{
	None,
	Building,   // Phase 1: lumberyard (spawns its villagers automatically)
	Farm        // Phase 2 placeholder; listed but not yet available
};

struct FBlueprintDef
{
	EBlueprintKind Kind = EBlueprintKind::None;
	FText          DisplayName;
	bool           bAvailable = false;   // false = "coming soon", greyed out in UI
};

// Static catalog; order defines UI order.
REALM_API const TArray<FBlueprintDef>& GetBlueprintCatalog();
