// Copyright Asamoto.
// Baked, sim-side road navigation data (pathfinding.md §4). Plain C++ — no
// UObjects, no engine spatial queries, no FGuid/spline knowledge. The render/
// roads side bakes an FRoadGraph into this immutable POD-ish form and hands it
// to FSimWorld via a command (FSimNavRoadsBuilder); the sim plans pure functions
// over it (RoadPathfinder). Positions are 2D (XY) in cm, Z is a render concern.
//
// Same discipline as FSimWorld: lightweight core containers/math only
// (FVector2D, TArray, TMap, FIntPoint) so this stays unit-testable.

#pragma once

#include "CoreMinimal.h"

// --- Tunables baked from URoadSettings (the sim never reads settings) (§4.3) ---
struct FSimNavParams
{
	float RoadSpeedFactor  = 1.1f;     // speed ×  while on a road waypoint segment
	float RoadCostMul      = 0.5f;     // planner cost weight for road distance (§5.2)
	float MaxAttachDist    = 1500.f;   // cm; max start/goal distance to a road to use it
	float WaypointReachDist = 30.f;    // cm; arrival tolerance per waypoint
};

// --- Spatial hash: tiny plain-C++ uniform grid over edge polyline samples (§4.1).
// Buckets store (edge, sample) pairs so ProjectToRoad can gather local candidates
// without scanning every segment.
struct FSpatialHash2D
{
	struct FEntry
	{
		int32 EdgeIdx   = INDEX_NONE;
		int32 SampleIdx = INDEX_NONE;
	};

	float CellSize = 500.f;   // cm
	TMap<FIntPoint, TArray<FEntry>> Cells;

	FIntPoint CellOf(const FVector2D& P) const
	{
		return FIntPoint(
			FMath::FloorToInt(P.X / CellSize),
			FMath::FloorToInt(P.Y / CellSize));
	}

	void Reset(float InCellSize = 500.f)
	{
		CellSize = InCellSize;
		Cells.Reset();
	}

	void Insert(const FVector2D& P, int32 EdgeIdx, int32 SampleIdx)
	{
		Cells.FindOrAdd(CellOf(P)).Add({ EdgeIdx, SampleIdx });
	}

	// Append every entry in the cells covering the AABB (P ± Radius).
	void QueryRadius(const FVector2D& P, float Radius, TArray<FEntry>& Out) const
	{
		const FIntPoint Min = CellOf(P - FVector2D(Radius, Radius));
		const FIntPoint Max = CellOf(P + FVector2D(Radius, Radius));
		for (int32 X = Min.X; X <= Max.X; ++X)
		{
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				if (const TArray<FEntry>* Bucket = Cells.Find(FIntPoint(X, Y)))
				{
					Out.Append(*Bucket);
				}
			}
		}
	}
};

// --- Baked graph elements ---
struct FSimNavNode
{
	FVector2D Pos = FVector2D::ZeroVector;
};

struct FSimNavEdge
{
	int32 NodeA = INDEX_NONE;
	int32 NodeB = INDEX_NONE;
	TArray<FVector2D> Samples;   // arc-length resampled polyline, A->B order
	TArray<float>     CumLen;    // cumulative length per sample; CumLen.Last() = total
	float Length = 0.f;

	// Arc-length position of the projection of an arc value onto the polyline.
	FVector2D PointAtArc(float Arc) const;
};

// Result of projecting a query point onto the network (§4.1).
struct FRoadProjection
{
	int32     EdgeIdx       = INDEX_NONE;
	float     ParamAlongEdge = 0.f;          // arc length from NodeA
	FVector2D ClosestPoint  = FVector2D::ZeroVector;
	float     Dist          = 0.f;           // 2D distance from the query point
	bool      bHit          = false;
};

// Baked navigation representation handed to the sim (§4.1). Immutable once set.
struct FSimNavRoads
{
	TArray<FSimNavNode>   Nodes;
	TArray<FSimNavEdge>   Edges;
	TArray<TArray<int32>> Adjacency;   // node index -> incident edge indices
	FSpatialHash2D        SampleHash;  // entries = (edge, sampleIdx)
	uint32 Version   = 0;
	float  HalfWidth = 150.f;          // road width / 2, baked from settings

	bool IsEmpty() const { return Edges.Num() == 0; }

	// Closest point on the network to P within MaxDist (via SampleHash, refined
	// against the neighbouring segments of each candidate sample). Miss if
	// nothing is within MaxDist.
	FRoadProjection ProjectToRoad(const FVector2D& P, float MaxDist) const;

	// Convenience: P sits on a road if it projects within HalfWidth + Tolerance.
	// Exposed for future features / debug; NOT used per-tick by movement (§6).
	bool IsPointOnRoad(const FVector2D& P, float Tolerance = 0.f) const;
};

// --- Agent path (§4.2). bOnRoad on a waypoint describes the segment LEADING to
// it (so the speed bonus is applied per arriving segment). Stored in a parallel
// array in FSimWorld, never serialized.
struct FPathWaypoint
{
	FVector2D Pos     = FVector2D::ZeroVector;
	uint8     bOnRoad = 0;
};

struct FAgentPath
{
	TArray<FPathWaypoint> Waypoints;
	int32     CurrentIdx        = 0;      // waypoint currently being walked toward
	uint32    PlannedNavVersion = 0;      // nav version this path was planned against
	FVector2D FinalGoal         = FVector2D::ZeroVector;

	void Reset()
	{
		Waypoints.Reset();
		CurrentIdx        = 0;
		PlannedNavVersion = 0;
		FinalGoal         = FVector2D::ZeroVector;
	}

	bool IsConsumed() const { return CurrentIdx >= Waypoints.Num(); }
};

// Path following (shared by Phase_Movement and the speed test). Advances the
// agent toward its current waypoint by up to BaseStep cm, applying RoadSpeedFactor
// on road segments. Consumes waypoints reached within ReachDist; on reaching the
// last waypoint the agent is snapped onto FinalGoal and CurrentIdx runs past the
// end. Returns the new 2D position.
namespace Nav
{
	FVector2D StepAlongPath(FAgentPath& Path, const FVector2D& Pos,
		float BaseStep, const FSimNavParams& Params);
}
