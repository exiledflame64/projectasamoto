// Copyright Asamoto.

#include "Render/Visuals/BuildingVisualSet.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UBuildingVisualSet::UBuildingVisualSet()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	UStaticMesh* Cube = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;
	UMaterialInterface* BasicMat = MatFinder.Succeeded() ? MatFinder.Object : nullptr;

	const auto MakeDef = [&](const FVector& InScale, const FLinearColor& InTint)
	{
		FRealmMeshDef Def;
		Def.Mesh = Cube;
		Def.Materials = { BasicMat };
		Def.Scale = InScale;
		Def.Tint = InTint;
		return Def;
	};

	FieldPlot = MakeDef(FVector(4.4f, 4.4f, 0.04f), FLinearColor(0.45f, 0.33f, 0.16f));

	Buildings.Add(EBuildingType::Lumberyard, MakeDef(FVector(2.0f, 2.0f, 1.5f), FLinearColor(0.5f, 0.3f, 0.12f)));
	Buildings.Add(EBuildingType::Warehouse,  MakeDef(FVector(2.5f, 2.5f, 2.0f), FLinearColor(0.15f, 0.4f, 0.9f)));
	Buildings.Add(EBuildingType::Sawmill,    MakeDef(FVector(2.2f, 2.2f, 1.8f), FLinearColor(0.85f, 0.45f, 0.1f)));
	Buildings.Add(EBuildingType::Farm,       MakeDef(FVector(2.4f, 2.4f, 1.2f), FLinearColor(0.55f, 0.7f, 0.15f)));
	Buildings.Add(EBuildingType::House,      MakeDef(FVector(1.5f, 1.5f, 1.2f), FLinearColor(0.85f, 0.8f, 0.7f)));
	Buildings.Add(EBuildingType::Temple,     MakeDef(FVector(2.2f, 2.2f, 2.2f), FLinearColor(0.8f, 0.2f, 0.15f)));
	Buildings.Add(EBuildingType::Dojo,       MakeDef(FVector(2.2f, 2.2f, 1.6f), FLinearColor(0.25f, 0.25f, 0.35f)));
}

const FRealmMeshDef& UBuildingVisualSet::BuildingDef(EBuildingType Type) const
{
	if (const FRealmMeshDef* Found = Buildings.Find(Type))
	{
		return *Found;
	}
	// The asset predates this building type (e.g. Temple/Dojo added after the
	// map was saved): fall back to the C++ engine-shape defaults.
	if (this != GetDefault<UBuildingVisualSet>())
	{
		return GetDefault<UBuildingVisualSet>()->BuildingDef(Type);
	}
	static const FRealmMeshDef Empty;
	return Empty;
}

const UBuildingVisualSet* UBuildingVisualSet::Resolve()
{
	if (const UBuildingVisualSet* Asset = LoadObject<UBuildingVisualSet>(nullptr,
		TEXT("/Game/Realm/Buildings/BuildingVisualSet.BuildingVisualSet")))
	{
		return Asset;
	}
	return GetDefault<UBuildingVisualSet>();
}

void UBuildingVisualSet::PostLoad()
{
	Super::PostLoad();
	FieldPlot.MigrateLegacyMaterial();
	for (auto& Pair : Buildings)
	{
		Pair.Value.MigrateLegacyMaterial();
	}
#if WITH_EDITOR
	FieldPlot.SyncMaterialsToMesh();
	for (auto& Pair : Buildings)
	{
		Pair.Value.SyncMaterialsToMesh();
	}
#endif
}

#if WITH_EDITOR
void UBuildingVisualSet::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FieldPlot.SyncMaterialsToMesh();
	for (auto& Pair : Buildings)
	{
		Pair.Value.SyncMaterialsToMesh();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
