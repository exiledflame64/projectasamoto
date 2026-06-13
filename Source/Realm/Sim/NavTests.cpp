// Copyright Asamoto.
// Unit tests for road-aware villager pathfinding (pathfinding.md §11). The
// planner is a pure function over FSimNavRoads, so tests 1-5 and 7 need no
// world; tests 6 drives FSimWorld directly.
// Run: Automation RunTests Realm.Nav

#include "Sim/SimWorld.h"
#include "Sim/RoadPathfinder.h"
#include "Sim/NavRoads.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Build a baked nav from node positions and (NodeA,NodeB) edges, sampling
	// each edge as a straight resampled polyline (mirrors what the real bake does
	// from FRoadGraph). Spacing keeps several samples per edge so CumLen and the
	// spatial hash are exercised.
	FSimNavRoads MakeNav(const TArray<FVector2D>& Nodes,
		const TArray<FIntPoint>& Edges, float Spacing = 100.f)
	{
		FSimNavRoads Nav;
		Nav.HalfWidth = 150.f;
		Nav.SampleHash.Reset(500.f);
		for (const FVector2D& P : Nodes)
		{
			Nav.Nodes.Add({ P });
		}
		Nav.Adjacency.SetNum(Nav.Nodes.Num());

		for (const FIntPoint& E : Edges)
		{
			const FVector2D A = Nodes[E.X];
			const FVector2D B = Nodes[E.Y];
			const float Len = FVector2D::Distance(A, B);
			const int32 Steps = FMath::Max(1, FMath::CeilToInt(Len / Spacing));

			FSimNavEdge Edge;
			Edge.NodeA = E.X;
			Edge.NodeB = E.Y;
			float Cum = 0.f;
			for (int32 i = 0; i <= Steps; ++i)
			{
				const FVector2D P = FMath::Lerp(A, B, (float)i / Steps);
				if (i > 0)
				{
					Cum += FVector2D::Distance(Edge.Samples.Last(), P);
				}
				Edge.Samples.Add(P);
				Edge.CumLen.Add(Cum);
			}
			Edge.Length = Cum;

			const int32 Idx = Nav.Edges.Add(MoveTemp(Edge));
			Nav.Adjacency[E.X].Add(Idx);
			Nav.Adjacency[E.Y].Add(Idx);
			const FSimNavEdge& S = Nav.Edges[Idx];
			for (int32 i = 0; i < S.Samples.Num(); ++i)
			{
				Nav.SampleHash.Insert(S.Samples[i], Idx, i);
			}
		}
		Nav.Version = 1;
		return Nav;
	}

	bool AnyOnRoad(const FAgentPath& Path)
	{
		for (const FPathWaypoint& W : Path.Waypoints)
		{
			if (W.bOnRoad)
			{
				return true;
			}
		}
		return false;
	}

	FSimNavParams DefaultParams() { return FSimNavParams{}; }
}

// 1) Empty network -> a straight 2-waypoint grass path, no road flags.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavStraightWhenNoRoadsTest,
	"Realm.Nav.StraightWhenNoRoads",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FNavStraightWhenNoRoadsTest::RunTest(const FString&)
{
	FSimNavRoads Empty;
	const FAgentPath Path = RoadPathfinder::PlanPath(
		FVector2D(0, 0), FVector2D(5000, 0), Empty, DefaultParams());

	TestEqual(TEXT("two waypoints"), Path.Waypoints.Num(), 2);
	TestFalse(TEXT("no road flags"), AnyOnRoad(Path));
	TestTrue(TEXT("ends at goal"), Path.FinalGoal.Equals(FVector2D(5000, 0), 1.f));
	return true;
}

// 2) A road near the beeline is preferred; road waypoints carry bOnRoad.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavPrefersRoadTest,
	"Realm.Nav.PrefersRoad",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FNavPrefersRoadTest::RunTest(const FString&)
{
	// Straight road along +X; start/goal sit 100 cm off it.
	const FSimNavRoads Nav = MakeNav(
		{ FVector2D(0, 0), FVector2D(4000, 0) }, { FIntPoint(0, 1) });
	const FAgentPath Path = RoadPathfinder::PlanPath(
		FVector2D(0, -100), FVector2D(4000, -100), Nav, DefaultParams());

	TestTrue(TEXT("routes via the road"), AnyOnRoad(Path));
	TestTrue(TEXT("more than a straight line"), Path.Waypoints.Num() > 2);
	TestTrue(TEXT("ends at goal"), Path.FinalGoal.Equals(FVector2D(4000, -100), 1.f));
	return true;
}

// 3) Both ends reach a road, but the detour costs more than the beeline -> straight.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavRejectsAbsurdDetourTest,
	"Realm.Nav.RejectsAbsurdDetour",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FNavRejectsAbsurdDetourTest::RunTest(const FString&)
{
	// Road is a vertical line at X=1000; start/goal are 1000 cm off it on the
	// other axis, so using it doubles back (grass 1000 + road*0.7 + grass 1000).
	const FSimNavRoads Nav = MakeNav(
		{ FVector2D(1000, -1000), FVector2D(1000, 3000) }, { FIntPoint(0, 1) });
	const FAgentPath Path = RoadPathfinder::PlanPath(
		FVector2D(0, 0), FVector2D(0, 2000), Nav, DefaultParams());

	TestFalse(TEXT("no road used"), AnyOnRoad(Path));
	TestEqual(TEXT("straight two-waypoint path"), Path.Waypoints.Num(), 2);
	return true;
}

// 4) Start and goal project onto the same edge -> path is the edge slice, not a
//    tour out to both endpoint nodes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavSameEdgeSliceTest,
	"Realm.Nav.SameEdgeSlice",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FNavSameEdgeSliceTest::RunTest(const FString&)
{
	// One long edge 0..3000 along X; both ends project to its interior.
	const FSimNavRoads Nav = MakeNav(
		{ FVector2D(0, 0), FVector2D(3000, 0) }, { FIntPoint(0, 1) });
	const FAgentPath Path = RoadPathfinder::PlanPath(
		FVector2D(500, -100), FVector2D(2500, -100), Nav, DefaultParams());

	TestTrue(TEXT("routes via the road"), AnyOnRoad(Path));

	// No road waypoint should reach either node endpoint (X=0 or X=3000): the
	// slice stays between the two projections (~500..2500).
	for (const FPathWaypoint& W : Path.Waypoints)
	{
		if (W.bOnRoad)
		{
			TestTrue(TEXT("slice stays past the start node"), W.Pos.X > 400.f);
			TestTrue(TEXT("slice stays before the end node"), W.Pos.X < 2600.f);
		}
	}
	return true;
}

// 5) Goal is too far from any road to attach -> straight path.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavProjectionMissTest,
	"Realm.Nav.ProjectionMiss",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FNavProjectionMissTest::RunTest(const FString&)
{
	const FSimNavRoads Nav = MakeNav(
		{ FVector2D(0, 0), FVector2D(2000, 0) }, { FIntPoint(0, 1) });
	// Start hugs the road (along +X at Y=0); the goal lies straight away from it,
	// so any road use backtracks and the beeline wins.
	const FAgentPath Path = RoadPathfinder::PlanPath(
		FVector2D(0, -100), FVector2D(0, -5000), Nav, DefaultParams());

	TestFalse(TEXT("no road used"), AnyOnRoad(Path));
	TestEqual(TEXT("straight two-waypoint path"), Path.Waypoints.Num(), 2);
	return true;
}

// 8) Detach/reattach: a road that juts out of the way is used on its straight
//    arms, with the bump cut across grass — the route leaves the road mid-way
//    and rejoins it (grass leg flanked by road legs).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavDetachReattachTest,
	"Realm.Nav.DetachReattach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FNavDetachReattachTest::RunTest(const FString&)
{
	// Road: straight A-B, then a triangular jut up to C and back to D, then
	// straight D-E. Cutting B->D across grass beats following the jut.
	const FSimNavRoads Nav = MakeNav(
		{
			FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1500, 1500),
			FVector2D(2000, 0), FVector2D(3000, 0)
		},
		{ FIntPoint(0, 1), FIntPoint(1, 2), FIntPoint(2, 3), FIntPoint(3, 4) });

	const FAgentPath Path = RoadPathfinder::PlanPath(
		FVector2D(0, -200), FVector2D(3000, -200), Nav, DefaultParams());

	TestTrue(TEXT("uses the road"), AnyOnRoad(Path));

	// Look for road -> grass -> road: a road leg, then an off-road (grass) leg,
	// then a road leg again. That ordering only exists if the route detached and
	// reattached mid-way.
	bool bRoad1 = false, bGap = false, bRoad2 = false;
	for (const FPathWaypoint& W : Path.Waypoints)
	{
		if (!bRoad1) { bRoad1 = W.bOnRoad != 0; }
		else if (!bGap) { bGap = W.bOnRoad == 0; }
		else if (W.bOnRoad) { bRoad2 = true; }
	}
	TestTrue(TEXT("detaches to grass then rejoins the road"), bRoad2);
	return true;
}

// 6) A nav swap bumps the version; a walking agent replans toward its FinalGoal.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavReplanOnVersionBumpTest,
	"Realm.Nav.ReplanOnVersionBump",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FNavReplanOnVersionBumpTest::RunTest(const FString&)
{
	FSimWorld Sim;
	const FAgentId Agent = Sim.SpawnAgent(FVector(0, -100, 0));

	Sim.SetNavRoads(MakeNav({ FVector2D(0, 0), FVector2D(4000, 0) },
		{ FIntPoint(0, 1) }), DefaultParams());
	const uint32 V1 = Sim.GetNavVersion();

	Sim.DebugMoveAgent(Agent, FVector(4000, -100, 0));
	Sim.Tick(0.1f);   // plans against V1

	FAgentPath P1;
	TestTrue(TEXT("path exists"), Sim.GetAgentPath(Agent, P1));
	TestEqual(TEXT("planned against v1"), P1.PlannedNavVersion, V1);
	TestTrue(TEXT("has waypoints"), P1.Waypoints.Num() >= 2);

	// Swap in a different (empty) network -> version bump -> lazy replan.
	Sim.SetNavRoads(FSimNavRoads{}, DefaultParams());
	const uint32 V2 = Sim.GetNavVersion();
	TestTrue(TEXT("version advanced"), V2 != V1);

	Sim.Tick(0.1f);
	FAgentPath P2;
	Sim.GetAgentPath(Agent, P2);
	TestEqual(TEXT("replanned against v2"), P2.PlannedNavVersion, V2);
	return true;
}

// 7) On a road segment the agent covers RoadSpeedFactor× the grass distance,
//    and the factor stacks multiplicatively with an already-scaled base step.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNavSpeedFactorAppliedTest,
	"Realm.Nav.SpeedFactorApplied",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FNavSpeedFactorAppliedTest::RunTest(const FString&)
{
	const FSimNavParams Params = DefaultParams();   // RoadSpeedFactor 1.1
	const float BaseStep = 20.f;                    // e.g. 200 cm/s * 0.1 s

	auto StepOnce = [&](uint8 bOnRoad) -> float
	{
		FAgentPath Path;
		Path.Waypoints.Add({ FVector2D(0, 0), 0 });           // start (already here)
		Path.Waypoints.Add({ FVector2D(100000, 0), bOnRoad }); // far waypoint
		Path.FinalGoal = FVector2D(100000, 0);
		const FVector2D New = Nav::StepAlongPath(Path, FVector2D(0, 0), BaseStep, Params);
		return New.X;   // distance travelled along +X
	};

	const float Grass = StepOnce(0);
	const float Road  = StepOnce(1);
	TestTrue(TEXT("grass step is the base step"), FMath::IsNearlyEqual(Grass, BaseStep, 0.5f));
	TestTrue(TEXT("road step is faster by the factor"),
		FMath::IsNearlyEqual(Road, BaseStep * Params.RoadSpeedFactor, 0.5f));

	// Stacking: an already-starving base step still gets the road multiplier.
	const float StarvingBase = BaseStep * 0.2f;
	FAgentPath Path;
	Path.Waypoints.Add({ FVector2D(0, 0), 0 });
	Path.Waypoints.Add({ FVector2D(100000, 0), 1 });
	Path.FinalGoal = FVector2D(100000, 0);
	const float Stacked = Nav::StepAlongPath(Path, FVector2D(0, 0), StarvingBase, Params).X;
	TestTrue(TEXT("road factor stacks with starving"),
		FMath::IsNearlyEqual(Stacked, StarvingBase * Params.RoadSpeedFactor, 0.5f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
