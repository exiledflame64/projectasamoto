// Copyright Asamoto.
// Unit tests for FRoadGraph (road_todos.md Phase 1): graph ops, edge
// splitting, arc-length resampling determinism. Pure data — no world needed.
// Run: Automation RunTests Realm.Roads

#include "Roads/RoadGraph.h"
#include "Roads/RoadSnapMath.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadGraphOpsTest,
	"Realm.Roads.GraphOps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadGraphOpsTest::RunTest(const FString& Parameters)
{
	FRoadGraph Graph;
	const FGuid A = Graph.AddNode(FVector(0, 0, 0));
	const FGuid B = Graph.AddNode(FVector(1000, 0, 0));

	const FGuid Edge = Graph.AddEdge(A, B, 0.5f, 300.f, ERoadTier::DirtPath);
	TestTrue(TEXT("edge created"), Edge.IsValid());
	TestEqual(TEXT("node A wired"), Graph.FindNode(A)->Edges.Num(), 1);
	TestEqual(TEXT("node B wired"), Graph.FindNode(B)->Edges.Num(), 1);

	// Duplicate edge request returns the existing edge.
	TestEqual(TEXT("duplicate edge collapses"),
		Graph.AddEdge(B, A, 0.2f, 100.f, ERoadTier::Road), Edge);
	TestEqual(TEXT("still one edge"), Graph.NumEdges(), 1);

	// Self-edge and unknown nodes are rejected.
	TestFalse(TEXT("self edge rejected"),
		Graph.AddEdge(A, A, 0.5f, 300.f, ERoadTier::DirtPath).IsValid());

	// Removing the edge orphans both Free nodes -> they are removed too.
	TestTrue(TEXT("edge removed"), Graph.RemoveEdge(Edge));
	TestEqual(TEXT("orphan free nodes removed"), Graph.NumNodes(), 0);

	// BuildingSocket nodes survive orphaning (buildings own them).
	const FGuid Socket = Graph.AddNode(FVector::ZeroVector, ERoadNodeType::BuildingSocket);
	const FGuid C = Graph.AddNode(FVector(500, 0, 0));
	const FGuid E2 = Graph.AddEdge(Socket, C, 0.5f, 300.f, ERoadTier::DirtPath);
	Graph.RemoveEdge(E2);
	TestNotNull(TEXT("socket survives"), Graph.FindNode(Socket));
	TestNull(TEXT("free node removed"), Graph.FindNode(C));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadGraphSplitTest,
	"Realm.Roads.EdgeSplit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadGraphSplitTest::RunTest(const FString& Parameters)
{
	FRoadGraph Graph;
	const FGuid A = Graph.AddNode(FVector(0, 0, 0));
	const FGuid B = Graph.AddNode(FVector(1000, 0, 0));
	const FGuid Edge = Graph.AddEdge(A, B, 0.5f, 300.f, ERoadTier::DirtPath);

	// Snap query just off the midpoint finds the edge.
	FGuid FoundEdge;
	float T = 0.f;
	TestTrue(TEXT("nearest point found"),
		Graph.FindNearestPointOnAnyEdge(FVector(500, 30, 0), 250.f, FoundEdge, T));
	TestEqual(TEXT("found the edge"), FoundEdge, Edge);

	TArray<FGuid> Dirty;
	const FGuid Junction = Graph.SplitEdge(Edge, T, &Dirty);
	TestTrue(TEXT("junction created"), Junction.IsValid());
	TestEqual(TEXT("split into two edges"), Graph.NumEdges(), 2);
	TestNull(TEXT("original edge gone"), Graph.FindEdge(Edge));

	const FRoadNode* JNode = Graph.FindNode(Junction);
	TestNotNull(TEXT("junction exists"), JNode);
	if (JNode)
	{
		TestEqual(TEXT("junction type"), JNode->Type, ERoadNodeType::Junction);
		TestEqual(TEXT("junction degree 2"), JNode->Edges.Num(), 2);
		TestTrue(TEXT("split near midpoint"),
			FMath::Abs(JNode->Position.X - 500.f) < 10.f
			&& FMath::Abs(JNode->Position.Y) < 1.f);
	}

	// Halves inherit the original properties.
	for (const auto& Pair : Graph.GetEdges())
	{
		TestEqual(TEXT("width inherited"), Pair.Value.Width, 300.f);
		TestEqual(TEXT("tier inherited"), Pair.Value.Tier, ERoadTier::DirtPath);
	}

	// Dirty list: removed original + two halves.
	TestEqual(TEXT("dirty count"), Dirty.Num(), 3);
	TestTrue(TEXT("removed edge reported dirty"), Dirty.Contains(Edge));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadGraphResampleTest,
	"Realm.Roads.Resample",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadGraphResampleTest::RunTest(const FString& Parameters)
{
	FRoadGraph Graph;
	const FGuid A = Graph.AddNode(FVector(0, 0, 0));
	const FGuid B = Graph.AddNode(FVector(1200, 0, 0));
	const FGuid C = Graph.AddNode(FVector(1200, 900, 0));
	const FGuid AB = Graph.AddEdge(A, B, 0.5f, 300.f, ERoadTier::DirtPath);
	Graph.AddEdge(B, C, 0.5f, 300.f, ERoadTier::DirtPath);

	const float Spacing = 75.f;
	const TArray<FVector> Poly1 = Graph.SampleEdgePolyline(AB, Spacing);
	const TArray<FVector> Poly2 = Graph.SampleEdgePolyline(AB, Spacing);

	// Determinism: identical arrays on repeat evaluation.
	TestEqual(TEXT("same sample count"), Poly1.Num(), Poly2.Num());
	for (int32 i = 0; i < FMath::Min(Poly1.Num(), Poly2.Num()); ++i)
	{
		TestTrue(TEXT("deterministic sample"), Poly1[i].Equals(Poly2[i], 0.f));
	}

	// Endpoints exact, spacing bounded.
	TestTrue(TEXT("enough samples"), Poly1.Num() >= 2);
	TestTrue(TEXT("starts at A"), Poly1[0].Equals(FVector(0, 0, 0), 1.f));
	TestTrue(TEXT("ends at B"), Poly1.Last().Equals(FVector(1200, 0, 0), 1.f));
	for (int32 i = 1; i < Poly1.Num(); ++i)
	{
		TestTrue(TEXT("spacing within bound"),
			FVector::Dist(Poly1[i - 1], Poly1[i]) <= Spacing * 1.05f);
	}

	// Height function snaps every sample's Z.
	const TArray<FVector> Snapped = Graph.SampleEdgePolyline(AB, Spacing,
		[](const FVector2D& XY, float& OutZ) { OutZ = XY.X * 0.01f; return true; });
	for (const FVector& P : Snapped)
	{
		TestTrue(TEXT("Z snapped to height fn"),
			FMath::IsNearlyEqual(P.Z, P.X * 0.01f, 0.01f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadGraphCurvatureTest,
	"Realm.Roads.Curvature",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadGraphCurvatureTest::RunTest(const FString& Parameters)
{
	// A bent path A-B-C: B's continuation neighbor pulls the B-C spline off
	// its chord — unless curvature is 0, which must mean dead straight.
	auto BuildGraph = [](float Curvature, FRoadGraph& Graph, FGuid& OutBC)
	{
		const FGuid A = Graph.AddNode(FVector(-1000, 0, 0));
		const FGuid B = Graph.AddNode(FVector(0, 0, 0));
		const FGuid C = Graph.AddNode(FVector(1000, 1000, 0));
		Graph.AddEdge(A, B, Curvature, 300.f, ERoadTier::DirtPath);
		OutBC = Graph.AddEdge(B, C, Curvature, 300.f, ERoadTier::DirtPath);
	};

	auto MaxChordDeviation = [](const FRoadGraph& Graph, const FGuid& EdgeId)
	{
		const TArray<FVector> Poly = Graph.SampleEdgePolyline(EdgeId, 50.f);
		const FVector Start = Poly[0];
		const FVector End = Poly.Last();
		float MaxDev = 0.f;
		for (const FVector& P : Poly)
		{
			MaxDev = FMath::Max(MaxDev,
				FMath::PointDistToLine(P, (End - Start).GetSafeNormal(), Start));
		}
		return MaxDev;
	};

	FRoadGraph Straight;
	FGuid BCStraight;
	BuildGraph(0.f, Straight, BCStraight);
	TestTrue(TEXT("curvature 0 is straight"),
		MaxChordDeviation(Straight, BCStraight) < 1.f);

	FRoadGraph Curved;
	FGuid BCCurved;
	BuildGraph(0.5f, Curved, BCCurved);
	TestTrue(TEXT("curvature 0.5 bends with continuation"),
		MaxChordDeviation(Curved, BCCurved) > 5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadGraphConnectivityTest,
	"Realm.Roads.Connectivity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadGraphConnectivityTest::RunTest(const FString& Parameters)
{
	FRoadGraph Graph;
	const FGuid A = Graph.AddNode(FVector(0, 0, 0));
	const FGuid B = Graph.AddNode(FVector(1000, 0, 0));
	const FGuid C = Graph.AddNode(FVector(2000, 0, 0));
	const FGuid D = Graph.AddNode(FVector(5000, 5000, 0));
	const FGuid E = Graph.AddNode(FVector(6000, 5000, 0));
	Graph.AddEdge(A, B, 0.5f, 300.f, ERoadTier::DirtPath);
	Graph.AddEdge(B, C, 0.5f, 300.f, ERoadTier::DirtPath);
	Graph.AddEdge(D, E, 0.5f, 300.f, ERoadTier::DirtPath);

	TestTrue(TEXT("A-C connected via B"), Graph.IsConnected(A, C));
	TestFalse(TEXT("A-D disconnected"), Graph.IsConnected(A, D));

	Graph.AddEdge(C, D, 0.5f, 300.f, ERoadTier::DirtPath);
	TestTrue(TEXT("A-E connected after bridge"), Graph.IsConnected(A, E));

	// IsPointOnRoad: on the A-B chord vs far away.
	TestTrue(TEXT("point on road"), Graph.IsPointOnRoad(FVector(500, 100, 0)));
	TestFalse(TEXT("point off road"), Graph.IsPointOnRoad(FVector(500, 2000, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadGraphSerializeTest,
	"Realm.Roads.Serialize",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadGraphSerializeTest::RunTest(const FString& Parameters)
{
	FRoadGraph Graph;
	const FGuid A = Graph.AddNode(FVector(0, 0, 0));
	const FGuid B = Graph.AddNode(FVector(1000, 0, 0));
	const FGuid C = Graph.AddNode(FVector(1000, 1000, 25));
	const FGuid AB = Graph.AddEdge(A, B, 0.3f, 280.f, ERoadTier::Road);
	Graph.AddEdge(B, C, 0.7f, 300.f, ERoadTier::DirtPath);
	Graph.SplitEdge(AB, 0.5f);

	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes);
		Graph.Serialize(Writer);
	}

	FRoadGraph Loaded;
	{
		FMemoryReader Reader(Bytes);
		Loaded.Serialize(Reader);
	}

	TestEqual(TEXT("node count round-trips"), Loaded.NumNodes(), Graph.NumNodes());
	TestEqual(TEXT("edge count round-trips"), Loaded.NumEdges(), Graph.NumEdges());

	for (const auto& Pair : Graph.GetEdges())
	{
		const FRoadEdge* LoadedEdge = Loaded.FindEdge(Pair.Key);
		TestNotNull(TEXT("edge survives"), LoadedEdge);
		if (LoadedEdge)
		{
			TestEqual(TEXT("width round-trips"), LoadedEdge->Width, Pair.Value.Width);
			TestEqual(TEXT("curvature round-trips"), LoadedEdge->Curvature, Pair.Value.Curvature);
			TestEqual(TEXT("tier round-trips"), LoadedEdge->Tier, Pair.Value.Tier);

			// Geometry identical after load (same polylines).
			const TArray<FVector> Before = Graph.SampleEdgePolyline(Pair.Key, 75.f);
			const TArray<FVector> After = Loaded.SampleEdgePolyline(Pair.Key, 75.f);
			TestEqual(TEXT("polyline count round-trips"), After.Num(), Before.Num());
			for (int32 i = 0; i < FMath::Min(Before.Num(), After.Num()); ++i)
			{
				TestTrue(TEXT("polyline point round-trips"),
					Before[i].Equals(After[i], 0.01f));
			}
		}
	}
	return true;
}

// --- Road–building snapping (road_snapping_todos.md §9) ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadGraphClosestPointTest,
	"Realm.Roads.ClosestPoint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadGraphClosestPointTest::RunTest(const FString& Parameters)
{
	FRoadGraph Graph;
	const FGuid A = Graph.AddNode(FVector(0, 0, 0));
	const FGuid B = Graph.AddNode(FVector(1000, 0, 0));
	const FGuid AB = Graph.AddEdge(A, B, 0.f, 300.f, ERoadTier::DirtPath);   // straight

	// Within range: closest point projects onto the chord, tangent is +X.
	const FRoadClosestPoint Hit = Graph.FindClosestPointOnNetwork(FVector(500, 200, 0), 300.f);
	TestTrue(TEXT("hit found"), Hit.bValid);
	TestEqual(TEXT("hit on the edge"), Hit.EdgeId, AB);
	TestTrue(TEXT("closest point ~ (500,0)"),
		FMath::Abs(Hit.Point.X - 500.f) < 1.f && FMath::Abs(Hit.Point.Y) < 1.f);
	TestTrue(TEXT("tangent is +X"),
		FVector2D(Hit.Tangent.X, Hit.Tangent.Y).Equals(FVector2D(1, 0), 0.01f));
	TestTrue(TEXT("distance ~ 200"), FMath::Abs(Hit.Distance - 200.f) < 1.f);

	// Beyond MaxDistance: miss.
	TestFalse(TEXT("out of range misses"),
		Graph.FindClosestPointOnNetwork(FVector(500, 400, 0), 300.f).bValid);

	// Two edges (an L): the nearer one wins.
	const FGuid C = Graph.AddNode(FVector(1000, 1000, 0));
	const FGuid BC = Graph.AddEdge(B, C, 0.f, 300.f, ERoadTier::DirtPath);
	const FRoadClosestPoint Near = Graph.FindClosestPointOnNetwork(FVector(1100, 500, 0), 400.f);
	TestTrue(TEXT("L: hit found"), Near.bValid);
	TestEqual(TEXT("L: picks the vertical leg"), Near.EdgeId, BC);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadBuildingSnapMathTest,
	"Realm.Roads.BuildingSnap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadBuildingSnapMathTest::RunTest(const FString& Parameters)
{
	const FVector RoadPoint(0, 0, 0);
	const FVector Tangent(1, 0, 0);
	const float HalfDepth = 100.f, HalfWidth = 150.f, Gap = 10.f;
	const float Offset = HalfWidth + Gap + HalfDepth;   // 260

	// Cursor north (+Y): building lands north, -X wall faces the road, yaw 90.
	const RoadSnap::FBuildingSnap North = RoadSnap::ComputeBuildingSnap(
		RoadPoint, Tangent, FVector(0, 300, 0), HalfDepth, HalfWidth, Gap);
	TestTrue(TEXT("north pos"), North.Position.Equals(FVector(0, Offset, 0), 0.1f));
	TestTrue(TEXT("north yaw 90"), FMath::IsNearlyEqual(North.YawDegrees, 90.f, 0.1f));

	// Cursor south (-Y): mirror, yaw -90.
	const RoadSnap::FBuildingSnap South = RoadSnap::ComputeBuildingSnap(
		RoadPoint, Tangent, FVector(0, -300, 0), HalfDepth, HalfWidth, Gap);
	TestTrue(TEXT("south pos"), South.Position.Equals(FVector(0, -Offset, 0), 0.1f));
	TestTrue(TEXT("south yaw -90"), FMath::IsNearlyEqual(South.YawDegrees, -90.f, 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadWallSnapMathTest,
	"Realm.Roads.WallSnap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadWallSnapMathTest::RunTest(const FString& Parameters)
{
	// Building at yaw 37, footprint (depth 100, width 150). For each of the 4
	// walls, a query just outside its midpoint must snap to a line offset
	// (RoadHalfWidth + Gap) outward along that wall's normal.
	const float Yaw = 37.f, HalfWidth = 150.f, Gap = 10.f, Trigger = 300.f;
	const FVector2D Foot(100.f, 150.f);
	const FVector Center(400, -200, 0);

	const float Rad = FMath::DegreesToRadians(Yaw);
	const float S = FMath::Sin(Rad), C = FMath::Cos(Rad);
	auto Rot = [&](const FVector2D& V) {
		return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
	};
	const FVector2D LocalCorners[4] = {
		{  Foot.X,  Foot.Y }, { -Foot.X,  Foot.Y }, { -Foot.X, -Foot.Y }, {  Foot.X, -Foot.Y } };
	const FVector2D LocalNormals[4] = { { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 0 } };
	const FVector2D Origin(Center.X, Center.Y);

	for (int32 i = 0; i < 4; ++i)
	{
		const FVector2D E0 = Origin + Rot(LocalCorners[i]);
		const FVector2D E1 = Origin + Rot(LocalCorners[(i + 1) % 4]);
		const FVector2D M = Rot(LocalNormals[i]);
		const FVector2D Mid = (E0 + E1) * 0.5f;
		const FVector Query(Mid.X + M.X * 60.f, Mid.Y + M.Y * 60.f, 0.f);   // 60 cm out

		const RoadSnap::FWallSnap Snap = RoadSnap::ComputeWallSnap(
			Center, Foot, Yaw, Query, HalfWidth, Gap, Trigger);
		TestTrue(FString::Printf(TEXT("wall %d snapped"), i), Snap.bValid);

		// Perpendicular distance from the snapped point out to the wall == offset.
		const FVector2D Pt(Snap.Point.X, Snap.Point.Y);
		const float Perp = FVector2D::DotProduct(Pt - E0, M);
		TestTrue(FString::Printf(TEXT("wall %d offset"), i),
			FMath::Abs(Perp - (HalfWidth + Gap)) < 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoadCorridorOverlapTest,
	"Realm.Roads.CorridorOverlap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FRoadCorridorOverlapTest::RunTest(const FString& Parameters)
{
	FRoadGraph Graph;
	const FGuid A = Graph.AddNode(FVector(-1000, 0, 0));
	const FGuid B = Graph.AddNode(FVector(1000, 0, 0));
	Graph.AddEdge(A, B, 0.f, 300.f, ERoadTier::DirtPath);   // half-width 150
	const float HalfWidth = 150.f;

	// A building snapped flush at a 10 cm gap (computed the same way as placement):
	// its near face sits 160 cm from the centerline, 10 cm clear of the corridor.
	const RoadSnap::FBuildingSnap Snap = RoadSnap::ComputeBuildingSnap(
		FVector(0, 0, 0), FVector(1, 0, 0), FVector(0, 300, 0), 100.f, HalfWidth, 10.f);
	TestFalse(TEXT("flush 10cm gap does not overlap"),
		Graph.DoesCorridorOverlapOBB(Snap.Position, FVector2D(100, 150), Snap.YawDegrees, HalfWidth));

	// Same box pulled onto the centerline straddles the corridor.
	TestTrue(TEXT("straddling box overlaps"),
		Graph.DoesCorridorOverlapOBB(FVector(0, 100, 0), FVector2D(100, 150), 90.f, HalfWidth));

	// Far away: clear.
	TestFalse(TEXT("distant box is clear"),
		Graph.DoesCorridorOverlapOBB(FVector(0, 2000, 0), FVector2D(100, 150), 0.f, HalfWidth));

	// A rotated box clipping the corridor edge still registers.
	TestTrue(TEXT("rotated box clipping corridor overlaps"),
		Graph.DoesCorridorOverlapOBB(FVector(0, 200, 0), FVector2D(120, 120), 45.f, HalfWidth));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
