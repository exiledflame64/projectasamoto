// Copyright Asamoto.

#include "Roads/RoadGraph.h"

namespace
{
	// Knot interval for centripetal Catmull-Rom (alpha = 0.5), guarded so
	// coincident control points cannot divide by zero.
	float KnotDelta(const FVector& A, const FVector& B)
	{
		return FMath::Max(FMath::Sqrt(FVector::Dist(A, B)), KINDA_SMALL_NUMBER);
	}
}

// ---------------------------------------------------------------------------
// Graph ops

FGuid FRoadGraph::AddNode(const FVector& Position, ERoadNodeType Type)
{
	FRoadNode Node;
	Node.Id = FGuid::NewGuid();
	Node.Position = Position;
	Node.Type = Type;
	Nodes.Add(Node.Id, Node);
	return Node.Id;
}

FGuid FRoadGraph::AddEdge(const FGuid& NodeA, const FGuid& NodeB,
	float Curvature, float Width, ERoadTier Tier)
{
	if (NodeA == NodeB || !Nodes.Contains(NodeA) || !Nodes.Contains(NodeB))
	{
		return FGuid();
	}

	// One edge per node pair: a duplicate request returns the existing edge.
	for (const FGuid& EdgeId : Nodes[NodeA].Edges)
	{
		if (const FRoadEdge* Existing = Edges.Find(EdgeId))
		{
			if ((Existing->NodeA == NodeB) || (Existing->NodeB == NodeB))
			{
				return Existing->Id;
			}
		}
	}

	FRoadEdge Edge;
	Edge.Id = FGuid::NewGuid();
	Edge.NodeA = NodeA;
	Edge.NodeB = NodeB;
	Edge.Curvature = FMath::Clamp(Curvature, 0.f, 1.f);
	Edge.Width = FMath::Max(Width, 10.f);
	Edge.Tier = Tier;
	Edges.Add(Edge.Id, Edge);

	Nodes[NodeA].Edges.Add(Edge.Id);
	Nodes[NodeB].Edges.Add(Edge.Id);
	return Edge.Id;
}

bool FRoadGraph::RemoveEdge(const FGuid& EdgeId)
{
	FRoadEdge Edge;
	if (!Edges.RemoveAndCopyValue(EdgeId, Edge))
	{
		return false;
	}

	for (const FGuid& NodeId : { Edge.NodeA, Edge.NodeB })
	{
		if (FRoadNode* Node = Nodes.Find(NodeId))
		{
			Node->Edges.Remove(EdgeId);
			if (Node->Edges.IsEmpty() && Node->Type != ERoadNodeType::BuildingSocket)
			{
				Nodes.Remove(NodeId);
			}
		}
	}
	return true;
}

FGuid FRoadGraph::SplitEdge(const FGuid& EdgeId, float T, TArray<FGuid>* OutDirtyEdges)
{
	const FRoadEdge* Edge = Edges.Find(EdgeId);
	if (!Edge)
	{
		return FGuid();
	}
	const FRoadEdge Old = *Edge;   // copy: RemoveEdge below invalidates the pointer

	const FVector SplitPos = SampleEdge(EdgeId, FMath::Clamp(T, 0.f, 1.f));
	const FGuid NewNode = AddNode(SplitPos, ERoadNodeType::Junction);

	// New halves first, then remove the original — the shared endpoints still
	// hold the new edges, so orphan cleanup cannot eat them.
	const FGuid E1 = AddEdge(Old.NodeA, NewNode, Old.Curvature, Old.Width, Old.Tier);
	const FGuid E2 = AddEdge(NewNode, Old.NodeB, Old.Curvature, Old.Width, Old.Tier);
	RemoveEdge(EdgeId);

	if (OutDirtyEdges)
	{
		OutDirtyEdges->Add(EdgeId);   // removed — renderer prunes it
		OutDirtyEdges->Add(E1);
		OutDirtyEdges->Add(E2);
	}
	return NewNode;
}

// ---------------------------------------------------------------------------
// Queries

FGuid FRoadGraph::FindNearestNode(const FVector& Position, float Radius) const
{
	const FVector2D P(Position.X, Position.Y);
	FGuid Best;
	float BestDistSq = Radius * Radius;

	for (const auto& Pair : Nodes)
	{
		const float DistSq = (FVector2D(Pair.Value.Position.X, Pair.Value.Position.Y) - P).SizeSquared();
		if (DistSq <= BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Pair.Key;
		}
	}
	return Best;
}

bool FRoadGraph::FindNearestPointOnAnyEdge(const FVector& Position, float Radius,
	FGuid& OutEdge, float& OutT) const
{
	const FVector2D P(Position.X, Position.Y);
	float BestDistSq = Radius * Radius;
	bool bFound = false;

	for (const auto& Pair : Edges)
	{
		FVector P0, P1, P2, P3;
		GetControlPoints(Pair.Value, P0, P1, P2, P3);

		TArray<FVector> Points;
		TArray<float> Params;
		DenseSample(P0, P1, P2, P3, Pair.Value.Curvature, /*MaxSpacing=*/50.f, Points, Params);

		for (int32 i = 0; i + 1 < Points.Num(); ++i)
		{
			const FVector2D A(Points[i].X, Points[i].Y);
			const FVector2D B(Points[i + 1].X, Points[i + 1].Y);
			const FVector2D AB = B - A;
			const float LenSq = AB.SizeSquared();
			const float Frac = (LenSq > KINDA_SMALL_NUMBER)
				? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LenSq, 0.f, 1.f) : 0.f;
			const float DistSq = (A + AB * Frac - P).SizeSquared();
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				OutEdge = Pair.Key;
				OutT = FMath::Lerp(Params[i], Params[i + 1], Frac);
				bFound = true;
			}
		}
	}
	return bFound;
}

bool FRoadGraph::IsPointOnRoad(const FVector& Position, float Tolerance) const
{
	// Widest edge bounds the search radius; the per-edge check below uses the
	// edge's own width.
	float MaxWidth = 0.f;
	for (const auto& Pair : Edges)
	{
		MaxWidth = FMath::Max(MaxWidth, Pair.Value.Width);
	}

	FGuid EdgeId;
	float T;
	if (!FindNearestPointOnAnyEdge(Position, MaxWidth * 0.5f + Tolerance, EdgeId, T))
	{
		return false;
	}

	const FRoadEdge* Edge = Edges.Find(EdgeId);
	const FVector Nearest = SampleEdge(EdgeId, T);
	const float Dist = FVector2D::Distance(
		FVector2D(Nearest.X, Nearest.Y), FVector2D(Position.X, Position.Y));
	return Edge && Dist <= Edge->Width * 0.5f + Tolerance;
}

bool FRoadGraph::IsConnected(const FGuid& NodeA, const FGuid& NodeB) const
{
	if (!Nodes.Contains(NodeA) || !Nodes.Contains(NodeB))
	{
		return false;
	}
	if (NodeA == NodeB)
	{
		return true;
	}

	TSet<FGuid> Visited;
	TArray<FGuid> Queue;
	Visited.Add(NodeA);
	Queue.Add(NodeA);

	while (!Queue.IsEmpty())
	{
		const FGuid Current = Queue.Pop(EAllowShrinking::No);
		const FRoadNode* Node = Nodes.Find(Current);
		if (!Node)
		{
			continue;
		}
		for (const FGuid& EdgeId : Node->Edges)
		{
			const FRoadEdge* Edge = Edges.Find(EdgeId);
			if (!Edge)
			{
				continue;
			}
			const FGuid Other = (Edge->NodeA == Current) ? Edge->NodeB : Edge->NodeA;
			if (Other == NodeB)
			{
				return true;
			}
			if (!Visited.Contains(Other))
			{
				Visited.Add(Other);
				Queue.Add(Other);
			}
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Spline evaluation

void FRoadGraph::GetControlPoints(const FRoadEdge& Edge,
	FVector& P0, FVector& P1, FVector& P2, FVector& P3) const
{
	const FRoadNode* A = Nodes.Find(Edge.NodeA);
	const FRoadNode* B = Nodes.Find(Edge.NodeB);
	check(A && B);

	P1 = A->Position;
	P2 = B->Position;
	const FVector Dir = (P2 - P1).GetSafeNormal();

	if (!FindContinuationPoint(*A, Edge.Id, Dir, P0))
	{
		P0 = P1 + (P1 - P2);   // mirrored phantom
	}
	if (!FindContinuationPoint(*B, Edge.Id, -Dir, P3))
	{
		P3 = P2 + (P2 - P1);
	}
}

bool FRoadGraph::FindContinuationPoint(const FRoadNode& Node, const FGuid& ExcludeEdge,
	const FVector& EdgeDir, FVector& OutPoint) const
{
	// EdgeDir points from this node INTO the edge being evaluated; the best
	// continuation neighbor approaches from the opposite side (dot closest
	// to 1 for normalize(Node - Neighbor) vs EdgeDir).
	float BestDot = -2.f;
	bool bFound = false;

	for (const FGuid& EdgeId : Node.Edges)
	{
		if (EdgeId == ExcludeEdge)
		{
			continue;
		}
		const FRoadEdge* Edge = Edges.Find(EdgeId);
		if (!Edge)
		{
			continue;
		}
		const FGuid OtherId = (Edge->NodeA == Node.Id) ? Edge->NodeB : Edge->NodeA;
		const FRoadNode* Other = Nodes.Find(OtherId);
		if (!Other)
		{
			continue;
		}
		const float Dot = FVector::DotProduct(
			(Node.Position - Other->Position).GetSafeNormal(), EdgeDir);
		if (Dot > BestDot)
		{
			BestDot = Dot;
			OutPoint = Other->Position;
			bFound = true;
		}
	}
	return bFound;
}

FVector FRoadGraph::EvalSegment(const FVector& P0, const FVector& P1,
	const FVector& P2, const FVector& P3, float Curvature, float T)
{
	// Centripetal Catmull-Rom converted to Hermite form (Barry-Goldman), with
	// the tangents scaled by Curvature * 2 so 0.5 is the standard curve and 0
	// degenerates to the straight chord.
	const float D01 = KnotDelta(P0, P1);
	const float D12 = KnotDelta(P1, P2);
	const float D23 = KnotDelta(P2, P3);

	FVector M1 = ((P1 - P0) / D01 - (P2 - P0) / (D01 + D12) + (P2 - P1) / D12) * D12;
	FVector M2 = ((P2 - P1) / D12 - (P3 - P1) / (D12 + D23) + (P3 - P2) / D23) * D12;

	const float TangentScale = FMath::Clamp(Curvature, 0.f, 1.f) * 2.f;
	M1 *= TangentScale;
	M2 *= TangentScale;

	const float T2 = T * T;
	const float T3 = T2 * T;
	return (2.f * T3 - 3.f * T2 + 1.f) * P1
		+ (T3 - 2.f * T2 + T) * M1
		+ (-2.f * T3 + 3.f * T2) * P2
		+ (T3 - T2) * M2;
}

void FRoadGraph::DenseSample(const FVector& P0, const FVector& P1,
	const FVector& P2, const FVector& P3, float Curvature, float MaxSpacing,
	TArray<FVector>& OutPoints, TArray<float>& OutParams)
{
	// Pre-tessellate at ~4x the target spacing so the arc-length walk below
	// has little chord error. Deterministic: count depends only on geometry.
	const float Chord = FVector::Dist(P1, P2);
	const int32 Subdivs = FMath::Clamp(
		FMath::CeilToInt32(Chord / FMath::Max(MaxSpacing * 0.25f, 1.f)), 8, 1024);

	OutPoints.Reset(Subdivs + 1);
	OutParams.Reset(Subdivs + 1);
	for (int32 i = 0; i <= Subdivs; ++i)
	{
		const float T = static_cast<float>(i) / Subdivs;
		OutPoints.Add(EvalSegment(P0, P1, P2, P3, Curvature, T));
		OutParams.Add(T);
	}
}

FVector FRoadGraph::SampleEdge(const FGuid& EdgeId, float T) const
{
	const FRoadEdge* Edge = Edges.Find(EdgeId);
	if (!Edge)
	{
		return FVector::ZeroVector;
	}
	FVector P0, P1, P2, P3;
	GetControlPoints(*Edge, P0, P1, P2, P3);
	return EvalSegment(P0, P1, P2, P3, Edge->Curvature, FMath::Clamp(T, 0.f, 1.f));
}

TArray<FVector> FRoadGraph::SampleEdgePolyline(const FGuid& EdgeId, float MaxSpacing,
	const FHeightFn& HeightFn) const
{
	const FRoadEdge* Edge = Edges.Find(EdgeId);
	if (!Edge)
	{
		return {};
	}
	FVector P0, P1, P2, P3;
	GetControlPoints(*Edge, P0, P1, P2, P3);
	return SampleSegmentPolyline(P0, P1, P2, P3, true, true, Edge->Curvature,
		MaxSpacing, HeightFn);
}

TArray<FVector> FRoadGraph::SampleSegmentPolyline(
	const FVector& Prev, const FVector& A, const FVector& B, const FVector& Next,
	bool bHasPrev, bool bHasNext, float Curvature, float MaxSpacing,
	const FHeightFn& HeightFn)
{
	const FVector P0 = bHasPrev ? Prev : A + (A - B);
	const FVector P3 = bHasNext ? Next : B + (B - A);

	TArray<FVector> Dense;
	TArray<float> Params;
	DenseSample(P0, A, B, P3, Curvature, FMath::Max(MaxSpacing, 1.f), Dense, Params);

	// Cumulative arc length over the dense tessellation.
	TArray<float> Cumulative;
	Cumulative.Reserve(Dense.Num());
	Cumulative.Add(0.f);
	for (int32 i = 1; i < Dense.Num(); ++i)
	{
		Cumulative.Add(Cumulative[i - 1] + FVector::Dist(Dense[i - 1], Dense[i]));
	}
	const float TotalLength = Cumulative.Last();

	// Even spacing at or below MaxSpacing, endpoints exact.
	const int32 NumOut = FMath::Max(2,
		FMath::CeilToInt32(TotalLength / FMath::Max(MaxSpacing, 1.f)) + 1);
	const float Step = TotalLength / (NumOut - 1);

	TArray<FVector> Out;
	Out.Reserve(NumOut);
	int32 Seg = 0;
	for (int32 i = 0; i < NumOut; ++i)
	{
		const float Target = (i == NumOut - 1) ? TotalLength : Step * i;
		while (Seg + 1 < Cumulative.Num() - 1 && Cumulative[Seg + 1] < Target)
		{
			++Seg;
		}
		const float SegLen = Cumulative[Seg + 1] - Cumulative[Seg];
		const float Frac = (SegLen > KINDA_SMALL_NUMBER)
			? (Target - Cumulative[Seg]) / SegLen : 0.f;
		Out.Add(FMath::Lerp(Dense[Seg], Dense[Seg + 1], Frac));
	}

	if (HeightFn)
	{
		for (FVector& Point : Out)
		{
			float Z;
			if (HeightFn(FVector2D(Point.X, Point.Y), Z))
			{
				Point.Z = Z;
			}
		}
	}
	return Out;
}

// ---------------------------------------------------------------------------
// Serialization

void FRoadGraph::Serialize(FArchive& Ar)
{
	int32 Version = 1;
	Ar << Version;

	int32 NodeCount = Nodes.Num();
	int32 EdgeCount = Edges.Num();
	Ar << NodeCount;
	Ar << EdgeCount;

	if (Ar.IsLoading())
	{
		Nodes.Reset();
		Edges.Reset();
		for (int32 i = 0; i < NodeCount; ++i)
		{
			FRoadNode Node;
			uint8 Type = 0;
			Ar << Node.Id << Node.Position << Node.Edges << Type;
			Node.Type = static_cast<ERoadNodeType>(Type);
			Nodes.Add(Node.Id, Node);
		}
		for (int32 i = 0; i < EdgeCount; ++i)
		{
			FRoadEdge Edge;
			uint8 Tier = 0;
			Ar << Edge.Id << Edge.NodeA << Edge.NodeB
				<< Edge.Curvature << Edge.Width << Tier;
			Edge.Tier = static_cast<ERoadTier>(Tier);
			Edges.Add(Edge.Id, Edge);
		}
	}
	else
	{
		for (auto& Pair : Nodes)
		{
			uint8 Type = static_cast<uint8>(Pair.Value.Type);
			Ar << Pair.Value.Id << Pair.Value.Position << Pair.Value.Edges << Type;
		}
		for (auto& Pair : Edges)
		{
			uint8 Tier = static_cast<uint8>(Pair.Value.Tier);
			Ar << Pair.Value.Id << Pair.Value.NodeA << Pair.Value.NodeB
				<< Pair.Value.Curvature << Pair.Value.Width << Tier;
		}
	}
}
