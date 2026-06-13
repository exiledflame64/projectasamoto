// Copyright Asamoto.
// Road-aware path planner (pathfinding.md §5). Pure functions over a const
// FSimNavRoads — no UObjects, no engine access, deterministic. Called from the
// sim whenever an agent acquires a movement target; returns an FAgentPath the
// movement phase follows.

#pragma once

#include "CoreMinimal.h"
#include "NavRoads.h"

class REALM_API RoadPathfinder
{
public:
	// Plan a path from Start to Goal (2D, cm). Prefers roads when a road route
	// is cheaper than cutting across grass (road distance weighted by
	// Params.RoadCostMul); otherwise returns a straight 2-waypoint grass path.
	static FAgentPath PlanPath(const FVector2D& Start, const FVector2D& Goal,
		const FSimNavRoads& Nav, const FSimNavParams& Params);

	// The straight grass fallback, exposed for callers that want it directly.
	static FAgentPath MakeStraightPath(const FVector2D& Start, const FVector2D& Goal);
};
