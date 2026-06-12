// Copyright Asamoto.
// URoadRendererSubsystem: turns road edges into
// banked ribbon meshes (one UDynamicMeshComponent per edge — simplest correct
// granularity; merge later only if profiling demands) plus junction discs.
//
// Roads are standalone render entities, exactly like building/villager
// proxies: meshes floating RibbonZOffset above the ground, never baked into
// the terrain. The ground and its materials are NEVER touched. The road graph
// (URoadNetworkSubsystem) stays the gameplay-facing entity for road
// interaction — movement bonuses, building sockets, snapping.
//
// The build tool's live preview renders through here too, so preview and
// committed geometry share one code path.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RoadRendererSubsystem.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
namespace UE::Geometry { class FDynamicMesh3; }

// One segment of the build tool's live preview.
struct FRoadPreviewSegment
{
	TArray<FVector> Polyline;   // terrain-snapped samples
	bool bValid = true;         // green vs red
};

UCLASS()
class REALM_API URoadRendererSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// Build tool preview path: rebuilds the preview meshes (valid segments in
	// green, invalid in red). Cheap enough per-frame at drag-preview sizes.
	void SetPreview(const TArray<FRoadPreviewSegment>& Segments, float Width);
	void ClearPreview();

private:
	void HandleNetworkChanged(const TArray<FGuid>& DirtyEdges);
	void RebuildEdge(const FGuid& EdgeId);
	void RebuildJunction(const FGuid& NodeId);
	void PruneStaleComponents();

	UDynamicMeshComponent* CreateRoadMeshComponent(FName BaseName,
		UMaterialInterface* Material);

	UPROPERTY()
	TObjectPtr<AActor> MeshRoot;

	UPROPERTY()
	TMap<FGuid, TObjectPtr<UDynamicMeshComponent>> EdgeMeshes;

	UPROPERTY()
	TMap<FGuid, TObjectPtr<UDynamicMeshComponent>> JunctionMeshes;

	UPROPERTY()
	TObjectPtr<UDynamicMeshComponent> PreviewValidMesh;

	UPROPERTY()
	TObjectPtr<UDynamicMeshComponent> PreviewInvalidMesh;

	// Resolved once at world start: asset materials when authored, tinted
	// engine-material fallbacks otherwise.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CommittedMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PreviewMaterialValid;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PreviewMaterialInvalid;
};
