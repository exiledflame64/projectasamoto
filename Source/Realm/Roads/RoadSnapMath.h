// Copyright Asamoto.
// Pure 2D placement geometry shared by both snapping directions and the
// overlap tests (road_snapping_todos.md §4-§6). No UObjects, no world access,
// no road graph dependency — plain C++ so it is unit-testable on its own and
// callable from FRoadGraph, the controller ghost path, and the road tool.
//
// Convention (matches the sim footprint table, SimTypes.h): a building's local
// -X face is the wall that sits flush against a road when snapped; HalfSize.X is
// half-depth (front/back), HalfSize.Y is half-width (along the road).

#pragma once

#include "CoreMinimal.h"

namespace RoadSnap
{
	// Squared 2D (XY) distance from segment [A,B] to the oriented box defined by
	// (Center, HalfSize, YawDegrees). Returns 0 when they overlap. Strict callers
	// compare against (HalfWidth^2) with '<' so a flush 10cm gap stays valid.
	REALM_API float SegmentToOBBDistanceSq(const FVector2D& A, const FVector2D& B,
		const FVector2D& Center, const FVector2D& HalfSize, float YawDegrees);

	// Building → road snap. Given a point on a road, its tangent, and the cursor
	// position, lands the building flush on the cursor's side of the road: the
	// -X wall faces the road across (RoadHalfWidth + SnapGap), the long side runs
	// parallel to the tangent.
	struct FBuildingSnap
	{
		FVector Position    = FVector::ZeroVector;
		float   YawDegrees  = 0.f;
	};
	REALM_API FBuildingSnap ComputeBuildingSnap(const FVector& RoadPoint,
		const FVector& Tangent, const FVector& Cursor,
		float FootprintHalfDepthX, float RoadHalfWidth, float SnapGap);

	// Road → building wall snap. Finds the nearest wall of the building footprint
	// OBB to QueryPoint (within TriggerRadius) and projects the point onto a line
	// parallel to that wall, offset outward by (RoadHalfWidth + SnapGap) and
	// extended by RoadHalfWidth past each corner so roads can run past the ends.
	struct FWallSnap
	{
		FVector   Point     = FVector::ZeroVector;
		FVector2D WallDir   = FVector2D::ZeroVector;   // along the snapped wall
		bool      bValid    = false;
	};
	REALM_API FWallSnap ComputeWallSnap(const FVector& BuildingPos,
		const FVector2D& FootprintHalfSize, float BuildingYawDegrees,
		const FVector& QueryPoint, float RoadHalfWidth, float SnapGap,
		float TriggerRadius);
}
