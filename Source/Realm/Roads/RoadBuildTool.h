// Copyright Asamoto.
// URoadBuildToolComponent (road_todos.md Phase 2): the click-to-place road
// tool, owned by ARealmPlayerController and armed via the blueprint bar's
// "Road" entry. Pure input + state machine — preview geometry is drawn by
// URoadRendererSubsystem's preview path and commits go through
// URoadNetworkSubsystem in one transaction.
//
// States: Idle -> Placing(point list) -> Commit/Cancel.
// LMB appends a point (snapped to nodes/edges within SnapRadius; Shift snaps
// the segment direction to 15-degree increments). RMB pops the last point —
// on an empty list the controller disarms the blueprint. Ctrl+wheel adjusts
// the pending segment's curvature. Enter commits.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Roads/RoadTypes.h"
#include "RoadBuildTool.generated.h"

class URoadNetworkSubsystem;
class URoadRendererSubsystem;

UCLASS()
class REALM_API URoadBuildToolComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoadBuildToolComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	void SetToolActive(bool bInActive);
	bool IsToolActive() const { return bActive; }

	// LMB while armed: append the (snapped, validated) cursor point.
	void HandleClick();

	// RMB: pop the last point. Returns false when there was nothing left to
	// pop — the controller then disarms the road blueprint.
	bool HandleUndo();

	// Enter: commit all points as one transaction (blocked while any segment
	// is invalid).
	void HandleCommit();

	// Ctrl + mouse wheel: step the pending segment's curvature in [0..1].
	void AdjustCurvature(float Direction);

private:
	struct FSegmentPreview
	{
		TArray<FVector> Polyline;
		bool bValid = true;
	};

	// Cursor ray -> Terrain channel ground point, with node/edge snapping and
	// optional Shift angle snapping applied.
	bool ResolveCursorPoint(FVector& OutPoint) const;

	// Spline polyline for the segment Points[Index-1] -> Points[Index] (or ->
	// pending cursor point), using neighbors for tangent continuity — the
	// exact math CommitPolyline will produce.
	TArray<FVector> BuildSegmentPolyline(const TArray<FVector>& Chain,
		int32 SegmentIndex, float Curvature) const;

	// Slope / length / reversal validation on the resampled polyline.
	bool ValidateSegment(const TArray<FVector>& Polyline) const;

	// One building's footprint OBB, pulled from the latest sim snapshot (never
	// FSimWorld directly — hard rule #2).
	struct FBuildingFootprint
	{
		FVector2D Center   = FVector2D::ZeroVector;
		FVector2D HalfSize = FVector2D::ZeroVector;
		float     Yaw      = 0.f;
	};
	void GatherBuildingFootprints(TArray<FBuildingFootprint>& Out) const;

	// True if no building footprint corridor-overlaps the resampled polyline
	// (strict, so a wall-snapped road at the 10 cm gap stays clear).
	bool SegmentClearOfBuildings(const TArray<FVector>& Polyline) const;

	void RefreshPreview();
	void ClearAll();

	URoadNetworkSubsystem* GetNetwork() const;
	URoadRendererSubsystem* GetRenderer() const;

	bool bActive = false;

	// Points committed so far this drawing (positions + per-segment curvature).
	TArray<FRoadCommitPoint> Points;

	// Curvature applied to the segment currently being drawn.
	float CurrentCurvature = 0.5f;

	// Cursor state computed in Tick, reused by HandleClick.
	FVector CursorPoint = FVector::ZeroVector;
	bool bCursorValid = false;
	bool bPendingSegmentValid = false;
};
