// Copyright Asamoto.
// Villager appearance per population tier (same per-key pattern as
// UBuildingVisualSet). Edit the /Game/Realm/Villagers/VillagerVisualSet asset;
// the C++ defaults (engine basic shapes, per-tier tints) are the fallback when
// the asset doesn't exist or lacks a tier entry.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Render/Visuals/RealmMeshDef.h"
#include "Sim/SimTypes.h"
#include "VillagerVisualSet.generated.h"

UCLASS(BlueprintType)
class REALM_API UVillagerVisualSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UVillagerVisualSet();

	UPROPERTY(EditAnywhere, Category = "Villagers")
	TMap<ETier, FRealmMeshDef> Villagers;

	// Asset entry first, then the C++ defaults (covers assets saved before a
	// tier existed), then an empty def.
	const FRealmMeshDef& VillagerDef(ETier Tier) const;

	// The project's asset if it exists, else the class defaults.
	static const UVillagerVisualSet* Resolve();

	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// Legacy single-def layout (pre per-tier split, 2026-06-12); migrated on
	// load into Villagers — one entry per tier, tinted from TierTints or the
	// C++ tier colors. Kept hidden (original names) only so older assets
	// still load their data.
	UPROPERTY()
	FRealmMeshDef Villager;

	UPROPERTY()
	TMap<ETier, FLinearColor> TierTints;
};
