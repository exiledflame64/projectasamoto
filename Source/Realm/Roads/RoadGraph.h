// Copyright Asamoto.
// The road network graph (road_todos.md Phase 1). Plain C++ — no UObjects, no
// mesh/material references, no world access — so it is unit-testable, save-
// friendly, and survives a renderer rewrite untouched (same discipline as
// FSimWorld). Spline evaluation lives here because it is data, not rendering:
// gameplay (pathfinding cost, IsPointOnRoad) needs the same polylines the
// renderer ribbons.
//
// Heights enter through an optional sampler callback so the graph never knows
// the terrain's concrete type.

#pragma once

#include "CoreMinimal.h"
#include "Roads/RoadTypes.h"

class REALM_API FRoadGraph
{
public:
	// Returns true and writes the terrain Z for a world XY. Bound by the
	// network subsystem to UTerrainHeightSubsystem; tests pass lambdas.
	using FHeightFn = TFunction<bool(const FVector2D&, float&)>;

	// --- Graph ops ---
	FGuid AddNode(const FVector& Position, ERoadNodeType Type = ERoadNodeType::Free);
	FGuid AddEdge(const FGuid& NodeA, const FGuid& NodeB,
		float Curvature, float Width, ERoadTier Tier);

	// Removing an edge also removes endpoint nodes left with no edges (except
	// BuildingSocket nodes, which buildings own).
	bool RemoveEdge(const FGuid& EdgeId);

	// Splits the edge at spline parameter T: inserts a Junction node, replaces
	// the edge with two edges inheriting its properties. Returns the new node
	// id (invalid guid on failure). New/removed edge ids are appended to the
	// optional dirty list for the renderer.
	FGuid SplitEdge(const FGuid& EdgeId, float T, TArray<FGuid>* OutDirtyEdges = nullptr);

	// --- Queries (distances measured in XY; cursor Z never matches stored Z) ---
	FGuid FindNearestNode(const FVector& Position, float Radius) const;
	bool FindNearestPointOnAnyEdge(const FVector& Position, float Radius,
		FGuid& OutEdge, float& OutT) const;

	// Distance to the nearest edge polyline <= width/2 + Tolerance.
	bool IsPointOnRoad(const FVector& Position, float Tolerance = 0.f) const;

	// BFS over the graph (future building connection checks).
	bool IsConnected(const FGuid& NodeA, const FGuid& NodeB) const;

	// --- Spline evaluation ---
	// Centripetal Catmull-Rom over [prev, A, B, next]: prev/next are the best
	// continuing neighbor nodes if the path continues, else mirrored phantoms.
	// Edge curvature scales tangent length (0 = straight, 0.5 = standard).
	FVector SampleEdge(const FGuid& EdgeId, float T) const;

	// Arc-length resampled polyline (target spacing MaxSpacing, endpoints
	// exact, deterministic). Sample Z snaps to HeightFn when provided.
	TArray<FVector> SampleEdgePolyline(const FGuid& EdgeId, float MaxSpacing,
		const FHeightFn& HeightFn = nullptr) const;

	// Same math on raw points, shared with the build tool's live preview so
	// the preview shows exactly what commit will produce. bHasPrev/bHasNext
	// false = mirrored phantom control points.
	static TArray<FVector> SampleSegmentPolyline(
		const FVector& Prev, const FVector& A, const FVector& B, const FVector& Next,
		bool bHasPrev, bool bHasNext, float Curvature, float MaxSpacing,
		const FHeightFn& HeightFn = nullptr);

	// --- Access ---
	const TMap<FGuid, FRoadNode>& GetNodes() const { return Nodes; }
	const TMap<FGuid, FRoadEdge>& GetEdges() const { return Edges; }
	const FRoadNode* FindNode(const FGuid& Id) const { return Nodes.Find(Id); }
	const FRoadEdge* FindEdge(const FGuid& Id) const { return Edges.Find(Id); }
	int32 NumNodes() const { return Nodes.Num(); }
	int32 NumEdges() const { return Edges.Num(); }
	void Reset() { Nodes.Reset(); Edges.Reset(); }

	// Byte-blob round trip for URealmSaveGame (same pattern as FSimWorld).
	void Serialize(FArchive& Ar);

private:
	TMap<FGuid, FRoadNode> Nodes;
	TMap<FGuid, FRoadEdge> Edges;

	// Control points for an edge: P1/P2 = endpoints, P0/P3 = continuation
	// neighbors or mirrored phantoms.
	void GetControlPoints(const FRoadEdge& Edge,
		FVector& P0, FVector& P1, FVector& P2, FVector& P3) const;

	// Best continuing neighbor of Node coming into this edge: the incident
	// other-end whose incoming direction lines up most with the edge direction.
	bool FindContinuationPoint(const FRoadNode& Node, const FGuid& ExcludeEdge,
		const FVector& EdgeDir, FVector& OutPoint) const;

	static FVector EvalSegment(const FVector& P0, const FVector& P1,
		const FVector& P2, const FVector& P3, float Curvature, float T);

	// Dense pre-sample used by resampling and nearest-point queries; returns
	// positions and their spline parameters.
	static void DenseSample(const FVector& P0, const FVector& P1,
		const FVector& P2, const FVector& P3, float Curvature, float MaxSpacing,
		TArray<FVector>& OutPoints, TArray<float>& OutParams);
};
