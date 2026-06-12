// Copyright Asamoto.
// Phase 1 debug villager: a placeholder cube plus floating text showing the
// agent's current state. Render-side proxy only; driven from the sim snapshot.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/SimTypes.h"
#include "AgentVisual.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UVillagerVisualSet;

UCLASS()
class REALM_API AAgentVisual : public AActor
{
	GENERATED_BODY()

public:
	AAgentVisual();

	// Called each frame by the visualizer with fresh snapshot data.
	void UpdateVisual(const FAgentSnapshot& Snap, const FVector& CameraLocation);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UTextRenderComponent> Label;

	// Resolved once at BeginPlay; supplies the per-tier mesh defs.
	UPROPERTY()
	TObjectPtr<const UVillagerVisualSet> VisualSet;

private:
	// (Re)apply the tier's mesh def — on first update and whenever the home
	// house changes tier (promotion/demotion swaps the villager's look).
	void ApplyTierDef(ETier Tier);

	ETier AppliedTier = ETier::COUNT;   // COUNT = nothing applied yet
};
