// Copyright Asamoto.
// Pure queries over the baked road navigation data (pathfinding.md §4).

#include "NavRoads.h"

namespace
{
	// Closest point on segment [A,B] to P, plus the parameter t in [0,1].
	FVector2D ClosestOnSegment(const FVector2D& A, const FVector2D& B,
		const FVector2D& P, float& OutT)
	{
		const FVector2D AB = B - A;
		const float LenSq = AB.SizeSquared();
		if (LenSq <= KINDA_SMALL_NUMBER)
		{
			OutT = 0.f;
			return A;
		}
		OutT = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LenSq, 0.f, 1.f);
		return A + AB * OutT;
	}
}

FVector2D FSimNavEdge::PointAtArc(float Arc) const
{
	if (Samples.Num() == 0)
	{
		return FVector2D::ZeroVector;
	}
	if (Samples.Num() == 1 || Arc <= 0.f)
	{
		return Samples[0];
	}
	if (Arc >= Length)
	{
		return Samples.Last();
	}
	// Locate the segment whose cumulative-length span contains Arc.
	for (int32 i = 0; i + 1 < Samples.Num(); ++i)
	{
		if (Arc <= CumLen[i + 1])
		{
			const float SegLen = CumLen[i + 1] - CumLen[i];
			const float T = SegLen > KINDA_SMALL_NUMBER ? (Arc - CumLen[i]) / SegLen : 0.f;
			return FMath::Lerp(Samples[i], Samples[i + 1], T);
		}
	}
	return Samples.Last();
}

FRoadProjection FSimNavRoads::ProjectToRoad(const FVector2D& P, float MaxDist) const
{
	FRoadProjection Best;
	float BestDistSq = MaxDist * MaxDist;

	TArray<FSpatialHash2D::FEntry> Candidates;
	SampleHash.QueryRadius(P, MaxDist, Candidates);

	// Each candidate sample contributes its two incident segments. Duplicate
	// segments across candidates are harmless (we keep the global minimum).
	for (const FSpatialHash2D::FEntry& C : Candidates)
	{
		if (!Edges.IsValidIndex(C.EdgeIdx))
		{
			continue;
		}
		const FSimNavEdge& E = Edges[C.EdgeIdx];
		for (int32 Seg = C.SampleIdx - 1; Seg <= C.SampleIdx; ++Seg)
		{
			if (Seg < 0 || Seg + 1 >= E.Samples.Num())
			{
				continue;
			}
			float T;
			const FVector2D CP = ClosestOnSegment(E.Samples[Seg], E.Samples[Seg + 1], P, T);
			const float DistSq = FVector2D::DistSquared(P, CP);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				const float SegLen = E.CumLen[Seg + 1] - E.CumLen[Seg];
				Best.EdgeIdx        = C.EdgeIdx;
				Best.ParamAlongEdge = E.CumLen[Seg] + T * SegLen;
				Best.ClosestPoint   = CP;
				Best.Dist           = FMath::Sqrt(DistSq);
				Best.bHit           = true;
			}
		}
	}
	return Best;
}

bool FSimNavRoads::IsPointOnRoad(const FVector2D& P, float Tolerance) const
{
	const FRoadProjection Proj = ProjectToRoad(P, HalfWidth + Tolerance);
	return Proj.bHit;
}

FVector2D Nav::StepAlongPath(FAgentPath& Path, const FVector2D& Pos,
	float BaseStep, const FSimNavParams& Params)
{
	FVector2D Cur = Pos;
	float Remaining = BaseStep;

	// Walk toward successive waypoints, consuming the per-step budget. Several
	// short (decimated) waypoints can fall inside one tick's travel.
	while (Path.CurrentIdx < Path.Waypoints.Num() && Remaining > 0.f)
	{
		const FPathWaypoint& WP = Path.Waypoints[Path.CurrentIdx];
		const float SpeedMul = WP.bOnRoad ? Params.RoadSpeedFactor : 1.f;
		const FVector2D To = WP.Pos - Cur;
		const float Dist = To.Size();

		// Distance we may cover toward this waypoint this slice (road bonus
		// applies to the segment leading INTO the waypoint).
		const float Reach = Remaining * SpeedMul;
		if (Dist <= FMath::Max(Reach, Params.WaypointReachDist))
		{
			Cur = WP.Pos;
			// Charge the budget for the ground actually covered (un-scaled).
			Remaining -= (SpeedMul > 0.f ? Dist / SpeedMul : Dist);
			++Path.CurrentIdx;
		}
		else
		{
			Cur += To / Dist * Reach;
			Remaining = 0.f;
		}
	}

	// Reaching the final waypoint snaps exactly onto the goal so the job state
	// machine's arrival check (Dist <= ArrivalTolerance) fires unchanged (§6).
	if (Path.IsConsumed() && Path.Waypoints.Num() > 0)
	{
		Cur = Path.FinalGoal;
	}
	return Cur;
}
