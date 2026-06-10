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
class UMaterialInstanceDynamic;

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

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MeshMID;
};
