// Copyright Asamoto.
// The single place appearance is authored: per-type mesh/material/scale/tint
// for everything the sim mirrors (trees, buildings, villagers, field plots).
// Both the editor seed previews and the runtime visualizer proxies read this,
// so what you see in the editor is exactly what spawns at play. Edit the
// /Game/Realm/RealmVisualSet asset; the C++ defaults (engine basic shapes) are
// the fallback when the asset doesn't exist.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sim/SimTypes.h"
#include "RealmVisualSet.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UStaticMeshComponent;

USTRUCT()
struct FRealmMeshDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Visual")
	TObjectPtr<UStaticMesh> Mesh;

	// Optional override; leave empty to keep the mesh's own materials.
	UPROPERTY(EditAnywhere, Category = "Visual")
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, Category = "Visual")
	FVector Scale = FVector::OneVector;

	// Applied via a dynamic instance's "Color" parameter when the material has
	// one (the engine BasicShapeMaterial does); ignored otherwise.
	UPROPERTY(EditAnywhere, Category = "Visual")
	FLinearColor Tint = FLinearColor::White;

	// Z offset that puts the scaled mesh's lowest point on the ground plane —
	// correct for both centred engine shapes and base-pivot art assets.
	float GroundLift() const;
	float GroundLiftFor(const FVector& InScale) const;

	// Per-instance override resolution: zero scale means "use the def's scale".
	FVector EffectiveScale(const FVector& Override) const
	{
		return Override.IsNearlyZero() ? Scale : Override;
	}

	// Configure a component to show this def (mesh, tinted material, and —
	// unless suppressed — scale). Seeds skip the scale so per-instance editor
	// scaling isn't stomped on every construction run.
	void ApplyTo(UStaticMeshComponent* Comp, UObject* MIDOuter, bool bApplyScale = true) const;
};

UCLASS(BlueprintType)
class REALM_API URealmVisualSet : public UDataAsset
{
	GENERATED_BODY()

public:
	URealmVisualSet();

	UPROPERTY(EditAnywhere, Category = "Realm")
	FRealmMeshDef Tree;

	UPROPERTY(EditAnywhere, Category = "Realm")
	FRealmMeshDef Villager;

	UPROPERTY(EditAnywhere, Category = "Realm")
	FRealmMeshDef FieldPlot;

	UPROPERTY(EditAnywhere, Category = "Realm")
	TMap<EBuildingType, FRealmMeshDef> Buildings;

	const FRealmMeshDef& BuildingDef(EBuildingType Type) const;

	// The project's asset if it exists (/Game/Realm/RealmVisualSet), else the
	// class defaults.
	static const URealmVisualSet* Resolve();
};
