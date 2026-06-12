// Copyright Asamoto.

#include "Render/Visuals/VillagerVisualSet.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Sengoku-flavoured placeholder colors, shared by the C++ defaults and the
	// legacy-asset migration.
	FLinearColor DefaultTierTint(ETier Tier)
	{
		switch (Tier)
		{
		case ETier::Peasant:     return FLinearColor(0.62f, 0.55f, 0.42f);   // undyed hemp
		case ETier::Artisan:     return FLinearColor(0.20f, 0.40f, 0.75f);   // indigo
		case ETier::Samurai:     return FLinearColor(0.75f, 0.12f, 0.12f);   // lacquer red
		case ETier::Monk:        return FLinearColor(0.90f, 0.55f, 0.10f);   // saffron
		case ETier::WarriorMonk: return FLinearColor(0.45f, 0.20f, 0.60f);   // dark purple
		default:                 return FLinearColor::White;
		}
	}
}

UVillagerVisualSet::UVillagerVisualSet()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	UStaticMesh* Cube = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;
	UMaterialInterface* BasicMat = MatFinder.Succeeded() ? MatFinder.Object : nullptr;

	// One def per tier (same shape, per-tier tint, until Anton assigns meshes
	// from /Game/ImportedBlenderAssets — same workflow as the buildings map).
	for (int32 t = 0; t < NumTiers; ++t)
	{
		const ETier Tier = static_cast<ETier>(t);
		FRealmMeshDef Def;
		Def.Mesh = Cube;
		Def.Materials = { BasicMat };
		Def.Scale = FVector(0.5f, 0.5f, 1.0f);
		Def.Tint = DefaultTierTint(Tier);
		Villagers.Add(Tier, Def);
	}
}

const FRealmMeshDef& UVillagerVisualSet::VillagerDef(ETier Tier) const
{
	if (const FRealmMeshDef* Found = Villagers.Find(Tier))
	{
		return *Found;
	}
	// The asset predates this tier: fall back to the C++ defaults.
	if (this != GetDefault<UVillagerVisualSet>())
	{
		return GetDefault<UVillagerVisualSet>()->VillagerDef(Tier);
	}
	static const FRealmMeshDef Empty;
	return Empty;
}

const UVillagerVisualSet* UVillagerVisualSet::Resolve()
{
	if (const UVillagerVisualSet* Asset = LoadObject<UVillagerVisualSet>(nullptr,
		TEXT("/Game/Realm/Villagers/VillagerVisualSet.VillagerVisualSet")))
	{
		return Asset;
	}
	return GetDefault<UVillagerVisualSet>();
}

void UVillagerVisualSet::PostLoad()
{
	Super::PostLoad();

	// Pre-split asset (single Villager def, optional TierTints): seed every
	// tier from the legacy def so authored mesh/materials carry over.
	if (Villagers.IsEmpty() && Villager.Mesh)
	{
		for (int32 t = 0; t < NumTiers; ++t)
		{
			const ETier Tier = static_cast<ETier>(t);
			FRealmMeshDef Def = Villager;
			const FLinearColor* Authored = TierTints.Find(Tier);
			Def.Tint = Authored ? *Authored : DefaultTierTint(Tier);
			Villagers.Add(Tier, Def);
		}
	}

	for (auto& Pair : Villagers)
	{
		Pair.Value.MigrateLegacyMaterial();
#if WITH_EDITOR
		Pair.Value.SyncMaterialsToMesh();
#endif
	}
}

#if WITH_EDITOR
void UVillagerVisualSet::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	for (auto& Pair : Villagers)
	{
		Pair.Value.SyncMaterialsToMesh();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
