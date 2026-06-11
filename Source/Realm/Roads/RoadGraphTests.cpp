// Copyright Asamoto.
// Unit tests for FRoadGraph (road_todos.md Phase 1): graph ops, edge
// splitting, arc-length resampling determinism. Pure data — no world needed.
// Run: Automation RunTests Realm.Roads

#include "Roads/RoadGraph.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
