// Copyright Asamoto.

#include "Render/Visuals/VegetationVisualSet.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UVegetationVisualSet::UVegetationVisualSet()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	Tree.Mesh = CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr;
	Tree.Materials = { MatFinder.Succeeded() ? MatFinder.Object : nullptr };
	Tree.Scale = FVector(0.6f, 0.6f, 3.0f);
	Tree.Tint = FLinearColor(0.1f, 0.55f, 0.15f);
}

const UVegetationVisualSet* UVegetationVisualSet::Resolve()
{
	if (const UVegetationVisualSet* Asset = LoadObject<UVegetationVisualSet>(nullptr,
		TEXT("/Game/Realm/Vegetation/VegetationVisualSet.VegetationVisualSet")))
	{
		return Asset;
	}
	return GetDefault<UVegetationVisualSet>();
}

void UVegetationVisualSet::PostLoad()
{
	Super::PostLoad();
	Tree.MigrateLegacyMaterial();
#if WITH_EDITOR
	Tree.SyncMaterialsToMesh();
#endif
}

#if WITH_EDITOR
void UVegetationVisualSet::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Tree.SyncMaterialsToMesh();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
