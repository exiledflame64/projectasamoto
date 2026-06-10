// Copyright Asamoto.
// Phase 1 debug visualizer: each frame it reads the sim snapshot and mirrors it
// with placeholder shapes (cube lumberyard/storage, cylinder trees, cube villagers) and
// an on-screen storage-log readout. Never writes to sim state.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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
};
