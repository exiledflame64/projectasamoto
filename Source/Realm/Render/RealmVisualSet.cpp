// Copyright Asamoto.

#include "RealmVisualSet.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

float FRealmMeshDef::GroundLift() const
{
	return GroundLiftFor(Scale);
}

float FRealmMeshDef::GroundLiftFor(const FVector& InScale) const
{
	return Mesh ? -Mesh->GetBoundingBox().Min.Z * InScale.Z : 0.f;
}

void FRealmMeshDef::ApplyTo(UStaticMeshComponent* Comp, UObject* MIDOuter, bool bApplyScale) const
{
	if (!Comp)
	{
		return;
	}
	if (Mesh)
	{
		Comp->SetStaticMesh(Mesh);
	}
	if (bApplyScale)
	{
		Comp->SetRelativeScale3D(Scale);
	}

	// Re-resolve the base material each time so repeated application (e.g.
	// OnConstruction) never stacks MIDs on MIDs.
	UMaterialInterface* Base = Material ? Material.Get() : Comp->GetMaterial(0);
	if (const UMaterialInstanceDynamic* AsMID = Cast<UMaterialInstanceDynamic>(Base))
	{
		Base = AsMID->Parent;
	}
	if (Base)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, MIDOuter);
		MID->SetVectorParameterValue(TEXT("Color"), Tint);
		Comp->SetMaterial(0, MID);
	}
}

URealmVisualSet::URealmVisualSet()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	UStaticMesh* Cube     = CubeFinder.Succeeded() ? CubeFinder.Object : nullptr;
	UStaticMesh* Cylinder = CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr;
	UMaterialInterface* BasicMat = MatFinder.Succeeded() ? MatFinder.Object : nullptr;

	Tree      = { Cylinder, BasicMat, FVector(0.6f, 0.6f, 3.0f),  FLinearColor(0.1f, 0.55f, 0.15f) };
	Villager  = { Cube,     BasicMat, FVector(0.5f, 0.5f, 1.0f),  FLinearColor(0.6f, 0.6f, 0.6f)   };
	FieldPlot = { Cube,     BasicMat, FVector(4.4f, 4.4f, 0.04f), FLinearColor(0.45f, 0.33f, 0.16f) };

	Buildings.Add(EBuildingType::Lumberyard, { Cube, BasicMat, FVector(2.0f, 2.0f, 1.5f), FLinearColor(0.5f, 0.3f, 0.12f)  });
	Buildings.Add(EBuildingType::Warehouse,  { Cube, BasicMat, FVector(2.5f, 2.5f, 2.0f), FLinearColor(0.15f, 0.4f, 0.9f)  });
	Buildings.Add(EBuildingType::Sawmill,    { Cube, BasicMat, FVector(2.2f, 2.2f, 1.8f), FLinearColor(0.85f, 0.45f, 0.1f) });
	Buildings.Add(EBuildingType::Farm,       { Cube, BasicMat, FVector(2.4f, 2.4f, 1.2f), FLinearColor(0.55f, 0.7f, 0.15f) });
	Buildings.Add(EBuildingType::House,      { Cube, BasicMat, FVector(1.5f, 1.5f, 1.2f), FLinearColor(0.85f, 0.8f, 0.7f)  });
}

const FRealmMeshDef& URealmVisualSet::BuildingDef(EBuildingType Type) const
{
	if (const FRealmMeshDef* Found = Buildings.Find(Type))
	{
		return *Found;
	}
	static const FRealmMeshDef Empty;
	return Empty;
}

const URealmVisualSet* URealmVisualSet::Resolve()
{
	if (const URealmVisualSet* Asset = LoadObject<URealmVisualSet>(nullptr,
		TEXT("/Game/Realm/RealmVisualSet.RealmVisualSet")))
	{
		return Asset;
	}
	return GetDefault<URealmVisualSet>();
}
