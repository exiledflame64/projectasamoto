// Copyright Asamoto.
// Road-aware path planner (pathfinding.md §5).

#include "RoadPathfinder.h"
#include "Algo/Reverse.h"

namespace
{
	// Below this start/goal separation a road detour is never worth it (§5.1).
	constexpr float TrivialDist = 400.f;   // cm
	constexpr int32 MaxWaypoints = 64;     // hard cap (§4.2)
	constexpr float SliceEps = 1.f;        // cm; avoids duplicating sample points

	// Transition kinds used to reconstruct the polyline from the search path.
	// Grass = straight off-road hop (cost 1.0/cm); the rest ride a road edge
	// (cost RoadCostMul/cm). StartPartial/GoalPartial attach mid-edge at the
	// projection nearest the start/goal; SameEdge slices one edge between them.
	enum class EKind : uint8 { Grass, RoadFull, StartPartial, GoalPartial, SameEdge };
	struct FTrans { EKind Kind = EKind::Grass; int32 EdgeIdx = INDEX_NONE; };

	struct FNeighbor { int32 To = INDEX_NONE; float Cost = 0.f; FTrans Via; };

	// Append the polyline points of an edge between two arc-length params, in
	// travel order, including both interpolated endpoints.
	void AppendSlice(const FSimNavEdge& E, float FromArc, float ToArc, TArray<FVector2D>& Out)
	{
		Out.Add(E.PointAtArc(FromArc));
		if (ToArc >= FromArc)
		{
			for (int32 i = 0; i < E.Samples.Num(); ++i)
			{
				if (E.CumLen[i] > FromArc + SliceEps && E.CumLen[i] < ToArc - SliceEps)
				{
					Out.Add(E.Samples[i]);
				}
			}
		}
		else
		{
			for (int32 i = E.Samples.Num() - 1; i >= 0; --i)
			{
				if (E.CumLen[i] < FromArc - SliceEps && E.CumLen[i] > ToArc + SliceEps)
				{
					Out.Add(E.Samples[i]);
				}
			}
		}
		Out.Add(E.PointAtArc(ToArc));
	}

	// Drop near-collinear interior waypoints, keeping segment-flag boundaries and
	// any heading change beyond ~5° (§4.2). Hard-caps the count.
	void Decimate(TArray<FPathWaypoint>& W)
	{
		if (W.Num() <= 2)
		{
			return;
		}
		const float CosThresh = FMath::Cos(FMath::DegreesToRadians(5.f));

		TArray<FPathWaypoint> Out;
		Out.Add(W[0]);
		for (int32 i = 1; i + 1 < W.Num(); ++i)
		{
			const FPathWaypoint& Prev = Out.Last();
			const FPathWaypoint& Cur  = W[i];
			const FPathWaypoint& Next = W[i + 1];

			bool bKeep = false;
			if (Cur.bOnRoad != Next.bOnRoad)
			{
				bKeep = true;   // grass<->road boundary: preserve exactly
			}
			else
			{
				FVector2D D0 = Cur.Pos - Prev.Pos;
				FVector2D D1 = Next.Pos - Cur.Pos;
				if (!D0.IsNearlyZero() && !D1.IsNearlyZero())
				{
					D0.Normalize();
					D1.Normalize();
					if (FVector2D::DotProduct(D0, D1) < CosThresh)
					{
						bKeep = true;
					}
				}
			}
			if (bKeep)
			{
				Out.Add(Cur);
			}
		}
		Out.Add(W.Last());

		// Coarse cap: if still over budget, keep endpoints and uniformly sample.
		if (Out.Num() > MaxWaypoints)
		{
			TArray<FPathWaypoint> Capped;
			Capped.Reserve(MaxWaypoints);
			const int32 Interior = MaxWaypoints - 2;
			Capped.Add(Out[0]);
			for (int32 k = 0; k < Interior; ++k)
			{
				const int32 Idx = 1 + (k * (Out.Num() - 2)) / Interior;
				Capped.Add(Out[Idx]);
			}
			Capped.Add(Out.Last());
			Out = MoveTemp(Capped);
		}
		W = MoveTemp(Out);
	}
}

FAgentPath RoadPathfinder::MakeStraightPath(const FVector2D& Start, const FVector2D& Goal)
{
	FAgentPath P;
	P.Waypoints.Add({ Start, 0 });
	P.Waypoints.Add({ Goal,  0 });
	P.FinalGoal = Goal;
	return P;
}

FAgentPath RoadPathfinder::PlanPath(const FVector2D& Start, const FVector2D& Goal,
	const FSimNavRoads& Nav, const FSimNavParams& Params)
{
	FAgentPath Straight = MakeStraightPath(Start, Goal);

	// 1) Trivial outs (§5.1).
	const float DirectDist = FVector2D::Distance(Start, Goal);
	if (Nav.IsEmpty() || DirectDist < TrivialDist)
	{
		return Straight;
	}

	// 2) Mixed grass+road graph with two virtual nodes (Vs=start, Vg=goal).
	// Road edges cost RoadCostMul/cm; grass hops cost 1.0/cm and connect ANY two
	// road nodes (and start/goal) so a route can leave the road, cut across grass,
	// and rejoin further along — "use the road where it helps, the grass where it
	// is shorter" rather than all-or-nothing. The straight beeline is just the
	// Vs->Vg grass edge, so Dijkstra's result is optimal and never worse than it.
	const int32 N  = Nav.Nodes.Num();
	const int32 Vs = N;
	const int32 Vg = N + 1;
	const int32 Total = N + 2;
	const float Mul = Params.RoadCostMul;

	// Precise mid-edge attach points nearest the start/goal (best on/off ramps).
	const FRoadProjection PS = Nav.ProjectToRoad(Start, Params.MaxAttachDist);
	const FRoadProjection PG = Nav.ProjectToRoad(Goal,  Params.MaxAttachDist);
	const float GrassStart = PS.bHit ? FVector2D::Distance(Start, PS.ClosestPoint) : 0.f;
	const float GrassGoal  = PG.bHit ? FVector2D::Distance(Goal,  PG.ClosestPoint) : 0.f;

	auto PosOf = [&](int32 Node) -> FVector2D
	{
		if (Node == Vs) return Start;
		if (Node == Vg) return Goal;
		return Nav.Nodes[Node].Pos;
	};

	auto GetNeighbors = [&](int32 Node, TArray<FNeighbor>& Out)
	{
		if (Node == Vg)
		{
			return;   // goal: no outgoing edges
		}
		const FVector2D From = PosOf(Node);

		if (Node == Vs)
		{
			// Precise mid-edge attach near the start.
			if (PS.bHit)
			{
				const FSimNavEdge& E = Nav.Edges[PS.EdgeIdx];
				Out.Add({ E.NodeA, GrassStart + PS.ParamAlongEdge * Mul,
					{ EKind::StartPartial, PS.EdgeIdx } });
				Out.Add({ E.NodeB, GrassStart + (E.Length - PS.ParamAlongEdge) * Mul,
					{ EKind::StartPartial, PS.EdgeIdx } });
				if (PG.bHit && PG.EdgeIdx == PS.EdgeIdx)
				{
					Out.Add({ Vg, GrassStart +
						FMath::Abs(PS.ParamAlongEdge - PG.ParamAlongEdge) * Mul + GrassGoal,
						{ EKind::SameEdge, PS.EdgeIdx } });
				}
			}
		}
		else
		{
			// Real road node: ride each incident road edge to its far endpoint.
			if (Nav.Adjacency.IsValidIndex(Node))
			{
				for (int32 EdgeIdx : Nav.Adjacency[Node])
				{
					const FSimNavEdge& E = Nav.Edges[EdgeIdx];
					const int32 Other = (E.NodeA == Node) ? E.NodeB : E.NodeA;
					Out.Add({ Other, E.Length * Mul, { EKind::RoadFull, EdgeIdx } });
				}
			}
			// Precise mid-edge detach toward the goal.
			if (PG.bHit)
			{
				const FSimNavEdge& E = Nav.Edges[PG.EdgeIdx];
				if (Node == E.NodeA)
				{
					Out.Add({ Vg, PG.ParamAlongEdge * Mul + GrassGoal,
						{ EKind::GoalPartial, PG.EdgeIdx } });
				}
				if (Node == E.NodeB)
				{
					Out.Add({ Vg, (E.Length - PG.ParamAlongEdge) * Mul + GrassGoal,
						{ EKind::GoalPartial, PG.EdgeIdx } });
				}
			}
		}

		// Grass hops: to every road node (detach/reattach) and straight to goal.
		for (int32 m = 0; m < N; ++m)
		{
			if (m != Node)
			{
				Out.Add({ m, (float)FVector2D::Distance(From, Nav.Nodes[m].Pos),
					{ EKind::Grass, INDEX_NONE } });
			}
		}
		Out.Add({ Vg, (float)FVector2D::Distance(From, Goal), { EKind::Grass, INDEX_NONE } });
	};

	TArray<float>  GScore;  GScore.Init(TNumericLimits<float>::Max(), Total);
	TArray<int32>  CameFrom; CameFrom.Init(INDEX_NONE, Total);
	TArray<FTrans> CameVia;  CameVia.SetNum(Total);
	TArray<bool>   Closed;   Closed.Init(false, Total);
	GScore[Vs] = 0.f;

	// Tiny graph: a linear-scan open set is well within budget (§5.3).
	TArray<FNeighbor> Neighbors;
	for (;;)
	{
		int32 Cur = INDEX_NONE;
		float Best = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Total; ++i)
		{
			if (!Closed[i] && GScore[i] < Best)
			{
				Best = GScore[i];
				Cur  = i;
			}
		}
		if (Cur == INDEX_NONE || Cur == Vg)
		{
			break;
		}
		Closed[Cur] = true;

		Neighbors.Reset();
		GetNeighbors(Cur, Neighbors);
		for (const FNeighbor& Nb : Neighbors)
		{
			const float Tentative = GScore[Cur] + Nb.Cost;
			if (Tentative < GScore[Nb.To])
			{
				GScore[Nb.To]  = Tentative;
				CameFrom[Nb.To] = Cur;
				CameVia[Nb.To]  = Nb.Via;
			}
		}
	}

	if (CameFrom[Vg] == INDEX_NONE)
	{
		return Straight;   // unreachable (defensive; the Vs->Vg grass edge exists)
	}

	// Reconstruct the node sequence Vs..Vg and the transition into each node.
	TArray<int32>  NodeSeq;
	TArray<FTrans> TransSeq;
	for (int32 Node = Vg; Node != INDEX_NONE; Node = CameFrom[Node])
	{
		NodeSeq.Add(Node);
		if (Node != Vs)
		{
			TransSeq.Add(CameVia[Node]);
		}
	}
	Algo::Reverse(NodeSeq);
	Algo::Reverse(TransSeq);

	// 3) Expand the transitions into waypoints. bOnRoad describes the segment
	// LEADING to a waypoint: grass legs flag 0, road slices flag 1. AddPt dedups
	// the shared boundary point between consecutive transitions.
	TArray<FPathWaypoint> WP;
	WP.Add({ Start, 0 });
	auto AddPt = [&](const FVector2D& P, uint8 Flag)
	{
		if (WP.Num() > 0 && FVector2D::DistSquared(WP.Last().Pos, P) < SliceEps * SliceEps)
		{
			return;
		}
		WP.Add({ P, Flag });
	};
	auto AddRoadSlice = [&](int32 EdgeIdx, float A, float B)
	{
		TArray<FVector2D> Tmp;
		AppendSlice(Nav.Edges[EdgeIdx], A, B, Tmp);
		for (const FVector2D& Pt : Tmp)
		{
			AddPt(Pt, 1);
		}
	};

	for (int32 s = 0; s < TransSeq.Num(); ++s)
	{
		const FTrans& T = TransSeq[s];
		const int32 PrevNode = NodeSeq[s];
		const int32 CurNode  = NodeSeq[s + 1];

		switch (T.Kind)
		{
		case EKind::Grass:
			AddPt(PosOf(CurNode), 0);   // straight off-road hop
			break;
		case EKind::RoadFull:
		{
			const FSimNavEdge& E = Nav.Edges[T.EdgeIdx];
			const float FromArc = (PrevNode == E.NodeA) ? 0.f : E.Length;
			const float ToArc   = (CurNode  == E.NodeA) ? 0.f : E.Length;
			AddRoadSlice(T.EdgeIdx, FromArc, ToArc);
			break;
		}
		case EKind::StartPartial:
		{
			const FSimNavEdge& E = Nav.Edges[T.EdgeIdx];
			AddPt(PS.ClosestPoint, 0);   // Start->entry is grass
			const float ToArc = (CurNode == E.NodeA) ? 0.f : E.Length;
			AddRoadSlice(T.EdgeIdx, PS.ParamAlongEdge, ToArc);
			break;
		}
		case EKind::GoalPartial:
		{
			const FSimNavEdge& E = Nav.Edges[T.EdgeIdx];
			const float FromArc = (PrevNode == E.NodeA) ? 0.f : E.Length;
			AddRoadSlice(T.EdgeIdx, FromArc, PG.ParamAlongEdge);
			AddPt(Goal, 0);              // exit->Goal is grass
			break;
		}
		case EKind::SameEdge:
		{
			AddPt(PS.ClosestPoint, 0);
			AddRoadSlice(T.EdgeIdx, PS.ParamAlongEdge, PG.ParamAlongEdge);
			AddPt(Goal, 0);
			break;
		}
		}
	}
	AddPt(Goal, 0);   // guarantee the final waypoint is exactly the goal

	if (WP.Num() < 2)
	{
		return Straight;
	}

	FAgentPath P;
	P.Waypoints = MoveTemp(WP);
	P.FinalGoal = Goal;
	Decimate(P.Waypoints);
	return P;
}
