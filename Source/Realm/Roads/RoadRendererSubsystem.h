// Copyright Asamoto.
// URoadRendererSubsystem (road_todos.md Phases 3-4): turns road edges into
// banked ribbon meshes (one UDynamicMeshComponent per edge — simplest correct
// granularity; merge later only if profiling demands) plus junction discs.
//
// Committed roads do NOT draw in the main pass when the RVT assets exist:
// their components write into RVT_Ground (VirtualTextureRenderPassType =
// Never), the terrain material samples the RVT, and z-fighting is structurally
// impossible. Until Tools/setup_road_assets.py has authored those assets the
// renderer falls back to tinted main-pass ribbons (5 cm above ground) so the
// whole system stays testable.
//
// The build tool's live preview renders through here too (main pass, never
// into the RVT), so preview and committed geometry share one code path.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RoadRendererSubsystem.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class URuntimeVirtualTexture;
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

	UDynamicMeshComponent* CreateRoadMeshComponent(FName BaseName, bool bCommitted,
		UMaterialInterface* Material);

	// Ribbon strip: 4 verts per sample (feathered edges via vertex alpha),
	// banked to the terrain normal, U across width, V = arc length / tiling.
	void BuildRibbonMesh(const TArray<FVector>& Polyline, float Width,
		UE::Geometry::FDynamicMesh3& OutMesh) const;

	// Disc stamped at junction nodes so overlapping ribbons composite cleanly.
	void BuildDiscMesh(const FVector& Center, float Radius,
		UE::Geometry::FDynamicMesh3& OutMesh) const;

	// Explicit RVT dirty-rect: component recreation usually invalidates its
	// bounds automatically, but in-place mesh updates need this (verified
	// against 5.7 — Invalidate on the volume's component is the reliable path).
	void InvalidateRVT(const FBox& WorldBounds) const;

	// Guarantees an ARuntimeVirtualTextureVolume covering the ground exists
	// and points at RVT_Ground (spawned at runtime so the map needs no manual
	// volume placement).
	void EnsureVirtualTextureVolume(UWorld& World);

	bool UseRVTPath() const { return GroundRVT && CommittedMaterial; }

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
	TObjectPtr<UMaterialInterface> FallbackMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PreviewMaterialValid;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PreviewMaterialInvalid;

	UPROPERTY()
	TObjectPtr<URuntimeVirtualTexture> GroundRVT;
};
