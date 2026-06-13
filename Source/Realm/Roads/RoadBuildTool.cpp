// Copyright Asamoto.

#include "Roads/RoadBuildTool.h"
#include "Roads/RoadGraph.h"
#include "Roads/RoadNetworkSubsystem.h"
#include "Roads/RoadRendererSubsystem.h"
#include "Roads/RoadSettings.h"
#include "Roads/RoadSnapMath.h"
#include "Roads/TerrainHeight.h"
#include "Core/SimSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

URoadBuildToolComponent::URoadBuildToolComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

URoadNetworkSubsystem* URoadBuildToolComponent::GetNetwork() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<URoadNetworkSubsystem>() : nullptr;
}

URoadRendererSubsystem* URoadBuildToolComponent::GetRenderer() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<URoadRendererSubsystem>() : nullptr;
}

void URoadBuildToolComponent::SetToolActive(bool bInActive)
{
	if (bActive == bInActive)
	{
		return;
	}
	bActive = bInActive;
	if (!bActive)
	{
		ClearAll();
	}
}

void URoadBuildToolComponent::ClearAll()
{
	Points.Reset();
	bCursorValid = false;
	bPendingSegmentValid = false;
	if (URoadRendererSubsystem* Renderer = GetRenderer())
	{
		Renderer->ClearPreview();
	}
}

void URoadBuildToolComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bActive)
	{
		return;
	}
	bCursorValid = ResolveCursorPoint(CursorPoint);
	RefreshPreview();
}

bool URoadBuildToolComponent::ResolveCursorPoint(FVector& OutPoint) const
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	URoadNetworkSubsystem* Network = GetNetwork();
	if (!PC || !Network)
	{
		return false;
	}

	FHitResult Hit;
	if (!PC->GetHitResultUnderCursor(RealmTerrainTraceChannel, /*bTraceComplex=*/false, Hit)
		|| !Hit.bBlockingHit)
	{
		return false;
	}
	FVector Point = Hit.Location;

	const URoadSettings* S = URoadSettings::Get();
	const FRoadGraph& Graph = Network->GetGraph();

	// Snap pass (spec order): node first, then point-on-edge, else free.
	const FGuid NearNode = Graph.FindNearestNode(Point, S->SnapRadius);
	if (NearNode.IsValid())
	{
		OutPoint = Graph.FindNode(NearNode)->Position;
		return true;
	}
	FGuid NearEdge;
	float T;
	if (Graph.FindNearestPointOnAnyEdge(Point, S->SnapRadius, NearEdge, T))
	{
		OutPoint = Graph.SampleEdge(NearEdge, T);   // commit will split here
		return true;
	}

	// Free point. Shift: snap the segment direction to AngleSnapDegrees
	// increments relative to the previous segment (or world X for the first).
	const bool bShift = PC->IsInputKeyDown(EKeys::LeftShift)
		|| PC->IsInputKeyDown(EKeys::RightShift);
	if (bShift && Points.Num() >= 1)
	{
		const FVector Last = Points.Last().Position;
		const FVector2D Dir(Point.X - Last.X, Point.Y - Last.Y);
		const float Len = Dir.Size();
		if (Len > 1.f)
		{
			float BaseAngle = 0.f;
			if (Points.Num() >= 2)
			{
				const FVector& PrevPos = Points[Points.Num() - 2].Position;
				BaseAngle = FMath::Atan2(Last.Y - PrevPos.Y, Last.X - PrevPos.X);
			}
			const float Step = FMath::DegreesToRadians(S->AngleSnapDegrees);
			const float Relative = FMath::Atan2(Dir.Y, Dir.X) - BaseAngle;
			const float Snapped = BaseAngle + FMath::RoundToFloat(Relative / Step) * Step;
			Point.X = Last.X + FMath::Cos(Snapped) * Len;
			Point.Y = Last.Y + FMath::Sin(Snapped) * Len;

			float Z;
			if (Network->MakeHeightFn()(FVector2D(Point.X, Point.Y), Z))
			{
				Point.Z = Z;
			}
		}
	}

	// Building wall snap (lowest precedence — road node/edge snapping above
	// already returned; Shift alignment, if any, has been applied to Point).
	// When the point lands within the trigger radius of a building wall, slide it
	// onto a line parallel to that wall at the gap offset. Alt or the master
	// switch disables it.
	const bool bAlt = PC->IsInputKeyDown(EKeys::LeftAlt) || PC->IsInputKeyDown(EKeys::RightAlt);
	if (!bAlt && S->bPlacementSnappingEnabled)
	{
		TArray<FBuildingFootprint> Foots;
		GatherBuildingFootprints(Foots);

		const float RoadHalfWidth = S->DefaultWidth * 0.5f;
		// Activation is measured so the road EDGE (half-width toward the building
		// from the cursor) need only come within SnapTriggerRadiusCm of a wall —
		// symmetric with the building→road trigger, and generous enough to grab
		// from open ground rather than requiring the cursor right on the wall.
		const float WallTrigger = S->SnapTriggerRadiusCm + RoadHalfWidth;
		RoadSnap::FWallSnap BestSnap;
		float BestDistSq = TNumericLimits<float>::Max();
		for (const FBuildingFootprint& F : Foots)
		{
			const FVector Center(F.Center.X, F.Center.Y, Point.Z);
			const RoadSnap::FWallSnap Snap = RoadSnap::ComputeWallSnap(
				Center, F.HalfSize, F.Yaw, Point, RoadHalfWidth, S->SnapGapCm,
				WallTrigger);
			if (Snap.bValid)
			{
				const float DistSq = FVector::DistSquared2D(Snap.Point, Point);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestSnap = Snap;
				}
			}
		}
		if (BestSnap.bValid)
		{
			Point = BestSnap.Point;
			float Z;
			if (Network->MakeHeightFn()(FVector2D(Point.X, Point.Y), Z))
			{
				Point.Z = Z;
			}
		}
	}

	OutPoint = Point;
	return true;
}

void URoadBuildToolComponent::GatherBuildingFootprints(TArray<FBuildingFootprint>& Out) const
{
	Out.Reset();
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	const USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}
	const FSimSnapshot& Snap = Sub->GetSnapshot();
	Out.Reserve(Snap.Buildings.Num());
	for (const FBuildingSnapshot& B : Snap.Buildings)
	{
		FBuildingFootprint F;
		F.Center   = FVector2D(B.Position.X, B.Position.Y);
		F.HalfSize = BuildingFootprintHalfSize(B.Type);
		F.Yaw      = B.YawDegrees;
		Out.Add(F);
	}
}

bool URoadBuildToolComponent::SegmentClearOfBuildings(const TArray<FVector>& Polyline) const
{
	if (Polyline.Num() < 2)
	{
		return true;
	}
	TArray<FBuildingFootprint> Foots;
	GatherBuildingFootprints(Foots);
	if (Foots.IsEmpty())
	{
		return true;
	}

	const float RoadHalfWidth = URoadSettings::Get()->DefaultWidth * 0.5f;
	const float WidthSq = RoadHalfWidth * RoadHalfWidth;
	for (int32 i = 0; i + 1 < Polyline.Num(); ++i)
	{
		const FVector2D A(Polyline[i].X, Polyline[i].Y);
		const FVector2D B(Polyline[i + 1].X, Polyline[i + 1].Y);
		for (const FBuildingFootprint& F : Foots)
		{
			if (RoadSnap::SegmentToOBBDistanceSq(A, B, F.Center, F.HalfSize, F.Yaw) < WidthSq)
			{
				return false;
			}
		}
	}
	return true;
}

TArray<FVector> URoadBuildToolComponent::BuildSegmentPolyline(
	const TArray<FVector>& Chain, int32 SegmentIndex, float Curvature) const
{
	// Tangent continuity from the in-progress chain only; once committed, the
	// graph re-evaluates continuations against the whole network (snapped
	// connections included), so this is the honest local approximation.
	const FVector& A = Chain[SegmentIndex - 1];
	const FVector& B = Chain[SegmentIndex];
	const bool bHasPrev = SegmentIndex >= 2;
	const bool bHasNext = SegmentIndex + 1 < Chain.Num();
	const FVector Prev = bHasPrev ? Chain[SegmentIndex - 2] : FVector::ZeroVector;
	const FVector Next = bHasNext ? Chain[SegmentIndex + 1] : FVector::ZeroVector;

	URoadNetworkSubsystem* Network = GetNetwork();
	return FRoadGraph::SampleSegmentPolyline(Prev, A, B, Next, bHasPrev, bHasNext,
		Curvature, URoadSettings::Get()->SampleSpacing,
		Network ? Network->MakeHeightFn() : FRoadGraph::FHeightFn());
}

bool URoadBuildToolComponent::ValidateSegment(const TArray<FVector>& Polyline) const
{
	if (Polyline.Num() < 2)
	{
		return false;
	}
	const URoadSettings* S = URoadSettings::Get();

	// Min length on the endpoints' chord — avoids degenerate ribbons.
	if (FVector::Dist(Polyline[0], Polyline.Last()) < S->MinSegmentLength)
	{
		return false;
	}

	const float MaxSlopeTan = FMath::Tan(FMath::DegreesToRadians(S->MaxSlopeDegrees));
	FVector2D PrevDir = FVector2D::ZeroVector;
	for (int32 i = 1; i < Polyline.Num(); ++i)
	{
		const FVector Delta = Polyline[i] - Polyline[i - 1];
		const float Horizontal = FVector2D(Delta.X, Delta.Y).Size();

		// Max slope between consecutive samples.
		if (FMath::Abs(Delta.Z) > Horizontal * MaxSlopeTan + KINDA_SMALL_NUMBER)
		{
			return false;
		}

		// Direction reversal = the spline cusps back on itself (curvature too
		// aggressive for the turn radius); reject instead of self-intersecting.
		const FVector2D Dir = FVector2D(Delta.X, Delta.Y).GetSafeNormal();
		if (i > 1 && FVector2D::DotProduct(Dir, PrevDir) < -0.5f)
		{
			return false;
		}
		PrevDir = Dir;
	}
	return true;
}

void URoadBuildToolComponent::RefreshPreview()
{
	URoadRendererSubsystem* Renderer = GetRenderer();
	if (!Renderer)
	{
		return;
	}

	// Chain = committed points (+ live cursor as the pending end).
	TArray<FVector> Chain;
	Chain.Reserve(Points.Num() + 1);
	for (const FRoadCommitPoint& P : Points)
	{
		Chain.Add(P.Position);
	}
	if (bCursorValid)
	{
		Chain.Add(CursorPoint);
	}

	TArray<FRoadPreviewSegment> Segments;
	bPendingSegmentValid = false;
	for (int32 i = 1; i < Chain.Num(); ++i)
	{
		const bool bPending = (i == Chain.Num() - 1) && bCursorValid;
		const float Curvature = bPending ? CurrentCurvature : Points[i].Curvature;

		FRoadPreviewSegment Segment;
		Segment.Polyline = BuildSegmentPolyline(Chain, i, Curvature);
		Segment.bValid = ValidateSegment(Segment.Polyline)
			&& SegmentClearOfBuildings(Segment.Polyline);
		if (bPending)
		{
			bPendingSegmentValid = Segment.bValid;
		}
		Segments.Add(MoveTemp(Segment));
	}

	Renderer->SetPreview(Segments, URoadSettings::Get()->DefaultWidth);

	// Placement hint + curvature feedback at the cursor.
	if (bCursorValid && !Points.IsEmpty())
	{
		DrawDebugString(GetWorld(), CursorPoint + FVector(0, 0, 120),
			FString::Printf(TEXT("curve %.1f  (Ctrl+wheel, Enter commits)"), CurrentCurvature),
			nullptr, bPendingSegmentValid ? FColor::Green : FColor::Red, 0.f, true);
	}
}

void URoadBuildToolComponent::HandleClick()
{
	if (!bActive || !bCursorValid)
	{
		return;
	}

	// First click opens the polyline; later clicks require a valid pending
	// segment (red segments cannot be committed, so don't let them in).
	if (!Points.IsEmpty() && !bPendingSegmentValid)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(0x0AD5, 2.f, FColor::Red,
				TEXT("Road segment invalid (too short, too steep, or too sharp)."));
		}
		return;
	}

	FRoadCommitPoint Point;
	Point.Position = CursorPoint;
	Point.Curvature = CurrentCurvature;   // curvature of the segment ending here
	Points.Add(Point);
}

bool URoadBuildToolComponent::HandleUndo()
{
	if (!bActive || Points.IsEmpty())
	{
		return false;
	}
	Points.Pop();
	if (Points.IsEmpty())
	{
		ClearAll();   // exit to Idle; the controller disarms the blueprint
		return false;
	}
	return true;
}

void URoadBuildToolComponent::HandleCommit()
{
	if (!bActive || Points.Num() < 2)
	{
		return;
	}

	// Re-validate every committed segment; any invalid one blocks the commit.
	TArray<FVector> Chain;
	for (const FRoadCommitPoint& P : Points)
	{
		Chain.Add(P.Position);
	}
	for (int32 i = 1; i < Chain.Num(); ++i)
	{
		// Backstop: re-resample (curvature may have changed via Ctrl+wheel after
		// points were placed) and re-check both slope/length and building overlap.
		const TArray<FVector> Poly = BuildSegmentPolyline(Chain, i, Points[i].Curvature);
		if (!ValidateSegment(Poly) || !SegmentClearOfBuildings(Poly))
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(0x0AD5, 2.f, FColor::Red,
					TEXT("Cannot commit: a road segment is invalid or crosses a building."));
			}
			return;
		}
	}

	if (URoadNetworkSubsystem* Network = GetNetwork())
	{
		const URoadSettings* S = URoadSettings::Get();
		Network->CommitPolyline(Points, S->DefaultWidth, ERoadTier::DirtPath);
	}

	// Stay armed for the next road; just reset the point list.
	Points.Reset();
	if (URoadRendererSubsystem* Renderer = GetRenderer())
	{
		Renderer->ClearPreview();
	}
}

void URoadBuildToolComponent::AdjustCurvature(float Direction)
{
	if (!bActive)
	{
		return;
	}
	const URoadSettings* S = URoadSettings::Get();
	CurrentCurvature = FMath::Clamp(
		CurrentCurvature + Direction * S->CurvatureStep, 0.f, 1.f);
}
