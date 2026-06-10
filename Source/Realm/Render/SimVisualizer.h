// Copyright Asamoto.
// Phase 2 debug visualizer: each frame it reads the sim snapshot and mirrors it
// with placeholder shapes (tinted cubes per building type, cylinder trees, cube
// villagers with state labels). Never writes to sim state. Proxies are pruned /
// respawned when a loaded save shrinks or retypes the sim arrays.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/SimTypes.h"
#include "SimVisualizer.generated.h"

class AAgentVisual;
class AStaticMeshActor;
class UStaticMesh;

UCLASS()
class REALM_API ASimVisualizer : public AActor
{
	GENERATED_BODY()

public:
	ASimVisualizer();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TArray<TObjectPtr<AAgentVisual>> AgentVisuals;

	UPROPERTY()
	TArray<TObjectPtr<AStaticMeshActor>> BuildingVisuals;

	// Type each building visual was spawned for (respawn on mismatch after load).
	TArray<EBuildingType> BuildingVisualTypes;

	UPROPERTY()
	TArray<TObjectPtr<AStaticMeshActor>> TreeVisuals;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ShapeMaterial;

	AStaticMeshActor* SpawnShape(UStaticMesh* Mesh, const FVector& Scale,
		const FLinearColor& Color);
	FVector GetCameraLocation() const;
	void PruneTo(int32 Count, TArray<TObjectPtr<AStaticMeshActor>>& Visuals,
		TArray<EBuildingType>* Types);
};
