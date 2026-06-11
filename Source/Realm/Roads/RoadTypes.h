// Copyright Asamoto.
// Road network data types (road_todos.md Phase 1). USTRUCT + UPROPERTY so the
// graph stays SaveGame-compatible, but the structs hold no UObject references
// and the graph itself (FRoadGraph) is plain C++ — it must survive a renderer
// rewrite untouched.
//
// Units: centimeters everywhere, matching the rest of the project (the spec's
// meter values are converted in URoadSettings defaults: 3 m width = 300 cm).

#pragma once

#include "CoreMinimal.h"
#include "RoadTypes.generated.h"

UENUM()
enum class ERoadNodeType : uint8
{
	Free,            // plain endpoint / waypoint
	Junction,        // created by snapping onto an existing edge (edge split)
	BuildingSocket   // future: building entrance points register these
};

UENUM()
enum class ERoadTier : uint8
{
	DirtPath,
	Road             // visual variants later
};

USTRUCT()
struct FRoadNode
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	// Z comes from the terrain height provider at creation.
	UPROPERTY()
	FVector Position = FVector::ZeroVector;

	// Incident edge ids (kept in sync by FRoadGraph).
	UPROPERTY()
	TArray<FGuid> Edges;

	UPROPERTY()
	ERoadNodeType Type = ERoadNodeType::Free;
};

USTRUCT()
struct FRoadEdge
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	UPROPERTY()
	FGuid NodeA;

	UPROPERTY()
	FGuid NodeB;

	// Scales the Catmull-Rom tangent length: 0 = straight, 0.5 = standard, 1 =
	// exaggerated. Per-segment, set while drawing (Ctrl + wheel).
	UPROPERTY()
	float Curvature = 0.5f;

	// Ribbon width in cm (3 m default; data-driven widening later).
	UPROPERTY()
	float Width = 300.f;

	UPROPERTY()
	ERoadTier Tier = ERoadTier::DirtPath;
};

// One point of a pending road polyline handed to URoadNetworkSubsystem on
// commit. Snapping is re-resolved at commit time from the raw position (not
// from ids captured during the drag) so earlier splits in the same commit
// cannot invalidate later points.
struct FRoadCommitPoint
{
	FVector Position = FVector::ZeroVector;

	// Curvature of the segment ENDING at this point (ignored on the first point).
	float Curvature = 0.5f;
};
