// Copyright Asamoto.

#include "Render/Visuals/VillagerVisualSet.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UVillagerVisualSet::UVillagerVisualSet()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	Villager.Mesh = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;
	Villager.Materials = { MatFinder.Succeeded() ? MatFinder.Object : nullptr };
	Villager.Scale = FVector(0.5f, 0.5f, 1.0f);
	Villager.Tint = FLinearColor(0.6f, 0.6f, 0.6f);
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
	Villager.MigrateLegacyMaterial();
#if WITH_EDITOR
	Villager.SyncMaterialsToMesh();
#endif
}

#if WITH_EDITOR
void UVillagerVisualSet::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Villager.SyncMaterialsToMesh();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
