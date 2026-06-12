// Copyright Asamoto.

#include "Render/Visuals/RealmMeshDef.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

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

	// Reset first so repeated application (OnConstruction reruns, asset edits)
	// never stacks MIDs on MIDs and removed overrides actually disappear.
	Comp->EmptyOverrideMaterials();

	const int32 NumSlots = FMath::Min(Materials.Num(), Comp->GetNumMaterials());
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		if (!Materials[Slot])
		{
			continue;   // empty entry: this slot keeps the mesh's own material
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Materials[Slot], MIDOuter);
		MID->SetVectorParameterValue(TEXT("Color"), Tint);
		Comp->SetMaterial(Slot, MID);
	}
}

void FRealmMeshDef::MigrateLegacyMaterial()
{
	if (Material && Materials.IsEmpty())
	{
		Materials.Add(Material);
	}
	Material = nullptr;
}

#if WITH_EDITOR
void FRealmMeshDef::SyncMaterialsToMesh()
{
	if (!Mesh)
	{
		return;
	}
	const TArray<FStaticMaterial>& SlotMaterials = Mesh->GetStaticMaterials();
	Materials.SetNum(SlotMaterials.Num());
	for (int32 Slot = 0; Slot < Materials.Num(); ++Slot)
	{
		if (!Materials[Slot])
		{
			Materials[Slot] = SlotMaterials[Slot].MaterialInterface;
		}
	}
}
#endif
