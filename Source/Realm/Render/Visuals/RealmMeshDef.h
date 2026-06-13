// Copyright Asamoto.
// The shared "how does one sim thing look" building block: mesh + per-slot
// material overrides + scale + tint. Used by every category visual set
// (Visuals/VegetationVisualSet.h, VillagerVisualSet.h, BuildingVisualSet.h).

#pragma once

#include "CoreMinimal.h"
#include "RealmMeshDef.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UStaticMeshComponent;

USTRUCT()
struct FRealmMeshDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Visual")
	TObjectPtr<UStaticMesh> Mesh;

	// Per-slot material overrides, index-matched to the mesh's material slots.
	// Picking a mesh auto-sizes this array and pre-fills every entry with the
	// mesh's own slot material, so multi-material meshes (trunk + leaves,
	// multi-part houses) expose ALL their slots for editing here. Clear an
	// entry to keep the mesh's own material for that slot.
	UPROPERTY(EditAnywhere, Category = "Visual")
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	// Legacy single override (pre slot-array); migrated into Materials[0] on
	// load. Kept hidden only so older assets still load their data.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, Category = "Visual")
	FVector Scale = FVector::OneVector;

	// Applied via each override slot's dynamic instance "Color" parameter when
	// the material has one (the engine BasicShapeMaterial does); ignored otherwise.
	UPROPERTY(EditAnywhere, Category = "Visual")
	FLinearColor Tint = FLinearColor::White;

	// Yaw correction (degrees) added to the sim placement yaw so a mesh whose
	// "front" isn't authored along the snap convention still faces the road
	// correctly. Meaningful for buildings; 0 for placeholder meshes. The
	// visualizer and seed previews both apply (SimYaw + this).
	UPROPERTY(EditAnywhere, Category = "Visual")
	float FrontYawOffsetDegrees = 0.f;

	// Z offset that puts the scaled mesh's lowest point on the ground plane —
	// correct for both centred engine shapes and base-pivot art assets.
	float GroundLift() const;
	float GroundLiftFor(const FVector& InScale) const;

	// Per-instance override resolution: zero scale means "use the def's scale".
	FVector EffectiveScale(const FVector& Override) const
	{
		return Override.IsNearlyZero() ? Scale : Override;
	}

	// Configure a component to show this def (mesh, per-slot tinted materials,
	// and — unless suppressed — scale). Seeds skip the scale so per-instance
	// editor scaling isn't stomped on every construction run.
	void ApplyTo(UStaticMeshComponent* Comp, UObject* MIDOuter, bool bApplyScale = true) const;

	// Old single-Material assets: move the value into Materials[0].
	void MigrateLegacyMaterial();

#if WITH_EDITOR
	// Match the Materials array to the mesh's slot count, pre-filling new
	// entries with the mesh's own materials so every slot shows up editable.
	void SyncMaterialsToMesh();
#endif
};
