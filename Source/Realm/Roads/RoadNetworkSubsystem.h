// Copyright Asamoto.
// URoadNetworkSubsystem (road_todos.md Phase 1/5): owns the FRoadGraph, binds
// terrain heights into it, and broadcasts OnNetworkChanged with dirty edge ids.
// The data layer never references the renderer — URoadRendererSubsystem
// subscribes to the delegate and prunes any id no longer present in the graph.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Roads/RoadGraph.h"
#include "RoadNetworkSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnRoadNetworkChanged, const TArray<FGuid>& /*DirtyEdges*/);

UCLASS()
class REALM_API URoadNetworkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Commits a drawn polyline in one transaction: every point re-snaps to a
	// node (within SnapRadius), else splits the edge under it (within
	// SnapRadius), else becomes a new Free node with terrain-snapped Z. Edges
	// connect consecutive points with the per-point segment curvature. One
	// broadcast at the end. Returns the created edge ids.
	TArray<FGuid> CommitPolyline(const TArray<FRoadCommitPoint>& Points,
		float Width, ERoadTier Tier);

	bool RemoveEdge(const FGuid& EdgeId);

	// --- Queries (Phase 5 gameplay API; pathfinding rasterizes these later) ---
	bool IsPointOnRoad(const FVector& Position, float Tolerance = 0.f) const
	{
		return Graph.IsPointOnRoad(Position, Tolerance);
	}
	bool IsConnected(const FGuid& NodeA, const FGuid& NodeB) const
	{
		return Graph.IsConnected(NodeA, NodeB);
	}

	// --- Snapping queries (road_snapping_todos.md §4.3) ---

	// Closest road point to P within MaxDist (XY); drives the building→road snap.
	bool FindClosestRoadPoint(const FVector& P, float MaxDist, FRoadClosestPoint& Out) const;

	// True if any road corridor overlaps the building footprint OBB. Uses the
	// settings road half-width; strict '<' so a flush snapped gap stays valid.
	bool DoesAnyRoadOverlapFootprint(const FVector& Center, const FVector2D& HalfSize,
		float YawDegrees) const;

	// Arc-length resampled, terrain-snapped polyline for an edge (renderer +
	// future movement-cost rasterization share this).
	TArray<FVector> GetEdgePolyline(const FGuid& EdgeId) const;

	const FRoadGraph& GetGraph() const { return Graph; }

	// Terrain height sampler bound to UTerrainHeightSubsystem (shared with the
	// build tool's preview so preview and committed geometry agree).
	FRoadGraph::FHeightFn MakeHeightFn() const;

	// --- Save/load (byte blob in URealmSaveGame, same pattern as SimBytes) ---
	void SerializeToBytes(TArray<uint8>& OutBytes);
	void LoadFromBytes(const TArray<uint8>& Bytes);

	FOnRoadNetworkChanged OnNetworkChanged;

private:
	FRoadGraph Graph;

	// Snaps a commit point to the existing network; may split an edge.
	FGuid ResolveCommitNode(const FVector& Position, float SnapRadius,
		TArray<FGuid>& DirtyEdges);

	void BroadcastAllEdgesDirty();
};
