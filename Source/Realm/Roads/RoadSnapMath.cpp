// Copyright Asamoto.

#include "Roads/RoadSnapMath.h"

namespace
{
	FVector2D Rotate2D(const FVector2D& V, float Sin, float Cos)
	{
		return FVector2D(V.X * Cos - V.Y * Sin, V.X * Sin + V.Y * Cos);
	}

	float PointToSegmentDistSq(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float LenSq = AB.SizeSquared();
		const float Frac = (LenSq > KINDA_SMALL_NUMBER)
			? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LenSq, 0.f, 1.f) : 0.f;
		return (A + AB * Frac - P).SizeSquared();
	}

	// Distance² from a point to the axis-aligned box [-H, +H] (0 when inside).
	float PointToAABBDistSq(const FVector2D& P, const FVector2D& H)
	{
		const float DX = FMath::Max(0.f, FMath::Abs(P.X) - H.X);
		const float DY = FMath::Max(0.f, FMath::Abs(P.Y) - H.Y);
		return DX * DX + DY * DY;
	}

	// Liang-Barsky slab clip: does segment [A,B] overlap the box [-H, +H]?
	bool SegmentIntersectsAABB(const FVector2D& A, const FVector2D& B, const FVector2D& H)
	{
		const FVector2D D = B - A;
		float TMin = 0.f, TMax = 1.f;
		const float AComp[2] = { A.X, A.Y };
		const float DComp[2] = { D.X, D.Y };
		const float HComp[2] = { H.X, H.Y };
		for (int32 i = 0; i < 2; ++i)
		{
			if (FMath::Abs(DComp[i]) < KINDA_SMALL_NUMBER)
			{
				if (AComp[i] < -HComp[i] || AComp[i] > HComp[i])
				{
					return false;   // parallel to this slab and outside it
				}
			}
			else
			{
				float T1 = (-HComp[i] - AComp[i]) / DComp[i];
				float T2 = ( HComp[i] - AComp[i]) / DComp[i];
				if (T1 > T2) { Swap(T1, T2); }
				TMin = FMath::Max(TMin, T1);
				TMax = FMath::Min(TMax, T2);
				if (TMin > TMax)
				{
					return false;
				}
			}
		}
		return true;
	}
}

namespace RoadSnap
{
	float SegmentToOBBDistanceSq(const FVector2D& A, const FVector2D& B,
		const FVector2D& Center, const FVector2D& HalfSize, float YawDegrees)
	{
		// Work in the box's local frame: rotate by -Yaw so the box is axis-aligned.
		const float Rad = FMath::DegreesToRadians(YawDegrees);
		const float S = FMath::Sin(-Rad);
		const float C = FMath::Cos(-Rad);
		const FVector2D LA = Rotate2D(A - Center, S, C);
		const FVector2D LB = Rotate2D(B - Center, S, C);

		if (SegmentIntersectsAABB(LA, LB, HalfSize))
		{
			return 0.f;
		}

		// Disjoint: the closest feature pair is a box corner vs the segment, or a
		// segment endpoint vs the box.
		float Best = TNumericLimits<float>::Max();
		const FVector2D Corners[4] = {
			FVector2D( HalfSize.X,  HalfSize.Y),
			FVector2D(-HalfSize.X,  HalfSize.Y),
			FVector2D(-HalfSize.X, -HalfSize.Y),
			FVector2D( HalfSize.X, -HalfSize.Y),
		};
		for (const FVector2D& Corner : Corners)
		{
			Best = FMath::Min(Best, PointToSegmentDistSq(Corner, LA, LB));
		}
		Best = FMath::Min(Best, PointToAABBDistSq(LA, HalfSize));
		Best = FMath::Min(Best, PointToAABBDistSq(LB, HalfSize));
		return Best;
	}

	FBuildingSnap ComputeBuildingSnap(const FVector& RoadPoint, const FVector& Tangent,
		const FVector& Cursor, float FootprintHalfDepthX, float RoadHalfWidth, float SnapGap)
	{
		FVector2D T(Tangent.X, Tangent.Y);
		if (!T.Normalize())
		{
			T = FVector2D(1.f, 0.f);
		}
		// N = T x Up (Up = +Z) in 2D = (T.Y, -T.X). Flip onto the cursor's side.
		FVector2D N(T.Y, -T.X);
		const FVector2D ToCursor(Cursor.X - RoadPoint.X, Cursor.Y - RoadPoint.Y);
		if (FVector2D::DotProduct(N, ToCursor) < 0.f)
		{
			N = -N;
		}

		const float Offset = RoadHalfWidth + SnapGap + FootprintHalfDepthX;
		FBuildingSnap Out;
		Out.Position = FVector(RoadPoint.X + N.X * Offset, RoadPoint.Y + N.Y * Offset, RoadPoint.Z);
		// Local +X points away from the road (along N), so the -X wall faces it.
		Out.YawDegrees = FMath::RadiansToDegrees(FMath::Atan2(N.Y, N.X));
		return Out;
	}

	FWallSnap ComputeWallSnap(const FVector& BuildingPos, const FVector2D& FootprintHalfSize,
		float BuildingYawDegrees, const FVector& QueryPoint, float RoadHalfWidth, float SnapGap,
		float TriggerRadius)
	{
		const float Rad = FMath::DegreesToRadians(BuildingYawDegrees);
		const float S = FMath::Sin(Rad);
		const float C = FMath::Cos(Rad);
		const FVector2D Origin(BuildingPos.X, BuildingPos.Y);
		const FVector2D Q(QueryPoint.X, QueryPoint.Y);

		// Local CCW corners; consecutive pairs form the 4 walls. The outward
		// normal of each wall is its local axis direction, rotated into world.
		const FVector2D LocalCorners[4] = {
			FVector2D( FootprintHalfSize.X,  FootprintHalfSize.Y),
			FVector2D(-FootprintHalfSize.X,  FootprintHalfSize.Y),
			FVector2D(-FootprintHalfSize.X, -FootprintHalfSize.Y),
			FVector2D( FootprintHalfSize.X, -FootprintHalfSize.Y),
		};
		const FVector2D LocalNormals[4] = {
			FVector2D( 0.f,  1.f),   // c0->c1 : +Y face
			FVector2D(-1.f,  0.f),   // c1->c2 : -X face
			FVector2D( 0.f, -1.f),   // c2->c3 : -Y face
			FVector2D( 1.f,  0.f),   // c3->c0 : +X face
		};

		FVector2D WorldCorners[4];
		for (int32 i = 0; i < 4; ++i)
		{
			WorldCorners[i] = Origin + Rotate2D(LocalCorners[i], S, C);
		}

		int32 BestWall = INDEX_NONE;
		float BestDistSq = TriggerRadius * TriggerRadius;
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector2D& E0 = WorldCorners[i];
			const FVector2D& E1 = WorldCorners[(i + 1) % 4];
			const float DistSq = PointToSegmentDistSq(Q, E0, E1);
			if (DistSq <= BestDistSq)
			{
				BestDistSq = DistSq;
				BestWall = i;
			}
		}

		FWallSnap Out;
		if (BestWall == INDEX_NONE)
		{
			return Out;   // bValid stays false
		}

		const FVector2D E0 = WorldCorners[BestWall];
		const FVector2D E1 = WorldCorners[(BestWall + 1) % 4];
		FVector2D W = E1 - E0;
		if (!W.Normalize())
		{
			return Out;
		}
		const FVector2D M = Rotate2D(LocalNormals[BestWall], S, C);

		// Snap line: the wall pushed outward by (RoadHalfWidth + SnapGap), then
		// extended by RoadHalfWidth past each corner.
		const float OutOffset = RoadHalfWidth + SnapGap;
		const FVector2D LineStart = E0 + M * OutOffset - W * RoadHalfWidth;
		const FVector2D LineEnd   = E1 + M * OutOffset + W * RoadHalfWidth;

		const FVector2D Seg = LineEnd - LineStart;
		const float LenSq = Seg.SizeSquared();
		const float Frac = (LenSq > KINDA_SMALL_NUMBER)
			? FMath::Clamp(FVector2D::DotProduct(Q - LineStart, Seg) / LenSq, 0.f, 1.f) : 0.f;
		const FVector2D Projected = LineStart + Seg * Frac;

		Out.Point   = FVector(Projected.X, Projected.Y, QueryPoint.Z);
		Out.WallDir = W;
		Out.bValid  = true;
		return Out;
	}
}
