// Copyright Asamoto.

#include "Roads/SimNavRoadsBuilder.h"
#include "Roads/RoadNetworkSubsystem.h"
#include "Roads/RoadSettings.h"
#include "Core/SimSubsystem.h"
#include "Sim/NavRoads.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	constexpr float PathDrawZLift = 25.f;   // cm above the flat sim plane

	bool IsMovingState(EAgentState S)
	{
		return S == EAgentState::MovingToWork  || S == EAgentState::MovingToStore ||
		       S == EAgentState::MovingToPickup || S == EAgentState::MovingToDeliver;
	}

	// Overlay one villager's remaining route: a marker at the villager, then a
	// line from it through every remaining waypoint, coloured per segment
	// (grey = grass leg, green = road leg — bOnRoad describes the segment leading
	// INTO a waypoint), with a small node at each waypoint. One-frame draw
	// (lifetime -1), refreshed every tick while the overlay is on.
	void DrawPathOverlay(UWorld* World, const FVector& AgentPos, const FAgentPath& Path)
	{
		const FVector Here(AgentPos.X, AgentPos.Y, PathDrawZLift);
		DrawDebugSphere(World, Here, 28.f, 10, FColor::Cyan, false, -1.f, 0, 4.f);

		FVector Prev = Here;
		for (int32 i = Path.CurrentIdx; i < Path.Waypoints.Num(); ++i)
		{
			const FVector P(Path.Waypoints[i].Pos.X, Path.Waypoints[i].Pos.Y, PathDrawZLift);
			const FColor C = Path.Waypoints[i].bOnRoad ? FColor::Green : FColor(150, 150, 150);
			DrawDebugLine(World, Prev, P, C, false, -1.f, 0, 6.f);
			DrawDebugSphere(World, P, 16.f, 6, FColor::Yellow, false, -1.f, 0, 2.f);
			Prev = P;
		}
	}
}

// realm.Nav.DrawAgentPaths 0/1 — per-villager path overlay (§10). Each moving
// villager's chosen route is drawn from the villager through its waypoints,
// grey for grass legs and green for road legs.
static TAutoConsoleVariable<int32> CVarDrawAgentPaths(
	TEXT("realm.Nav.DrawAgentPaths"), 0,
	TEXT("Per-villager path overlay: 0 off, 1 on (cyan=villager, green=road, grey=grass)."),
	ECVF_Cheat);

// realm.DebugPath — convenience toggle for the per-villager path overlay. No
// world side effects (does not place roads or move villagers).
static FAutoConsoleCommand GRealmDebugPathCmd(
	TEXT("realm.DebugPath"),
	TEXT("Toggle the per-villager path overlay (alias for realm.Nav.DrawAgentPaths)."),
	FConsoleCommandDelegate::CreateLambda([]
	{
		const int32 New = CVarDrawAgentPaths.GetValueOnGameThread() != 0 ? 0 : 1;
		CVarDrawAgentPaths.AsVariable()->Set(New, ECVF_SetByConsole);
		UE_LOG(LogTemp, Log, TEXT("[RealmNav] Agent path overlay %s"),
			New ? TEXT("ON") : TEXT("OFF"));
	}));
#endif // !UE_BUILD_SHIPPING

void USimNavRoadsBuilder::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (URoadNetworkSubsystem* Network = InWorld.GetSubsystem<URoadNetworkSubsystem>())
	{
		Network->OnNetworkChanged.AddUObject(this, &USimNavRoadsBuilder::HandleNetworkChanged);
	}

	// Catch a road network committed before begin play (e.g. a load during
	// startup): bake once now so the sim starts with current nav (§8 load order).
	Rebake();
}

void USimNavRoadsBuilder::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (URoadNetworkSubsystem* Network = World->GetSubsystem<URoadNetworkSubsystem>())
		{
			Network->OnNetworkChanged.RemoveAll(this);
		}
	}
	Super::Deinitialize();
}

void USimNavRoadsBuilder::HandleNetworkChanged(const TArray<FGuid>& /*DirtyEdges*/)
{
	Rebake();   // full rebuild; the graph is tiny (§9)
}

void USimNavRoadsBuilder::Rebake()
{
	UWorld* World = GetWorld();
	URoadNetworkSubsystem* Network = World ? World->GetSubsystem<URoadNetworkSubsystem>() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	USimSubsystem* SimSub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	if (!Network || !SimSub)
	{
		return;
	}

	const FRoadGraph& Graph = Network->GetGraph();
	const URoadSettings* S = URoadSettings::Get();

	FSimNavRoads Nav;
	Nav.HalfWidth = S->DefaultWidth * 0.5f;
	Nav.SampleHash.Reset(/*CellSize=*/500.f);

	// Stable node index per graph node id.
	TMap<FGuid, int32> NodeIndex;
	NodeIndex.Reserve(Graph.NumNodes());
	for (const TPair<FGuid, FRoadNode>& Pair : Graph.GetNodes())
	{
		const int32 Idx = Nav.Nodes.Add({ FVector2D(Pair.Value.Position.X, Pair.Value.Position.Y) });
		NodeIndex.Add(Pair.Key, Idx);
	}
	Nav.Adjacency.SetNum(Nav.Nodes.Num());

	// Each edge: reuse the renderer's arc-length resampled polyline so nav and
	// visuals trace the same path (§9). Drop Z — the sim is 2D.
	for (const TPair<FGuid, FRoadEdge>& Pair : Graph.GetEdges())
	{
		const int32* A = NodeIndex.Find(Pair.Value.NodeA);
		const int32* B = NodeIndex.Find(Pair.Value.NodeB);
		if (!A || !B)
		{
			continue;   // dangling edge (shouldn't happen; defensive)
		}

		const TArray<FVector> Polyline = Network->GetEdgePolyline(Pair.Key);
		if (Polyline.Num() < 2)
		{
			continue;
		}

		FSimNavEdge Edge;
		Edge.NodeA = *A;
		Edge.NodeB = *B;
		Edge.Samples.Reserve(Polyline.Num());
		Edge.CumLen.Reserve(Polyline.Num());
		float Cum = 0.f;
		for (int32 i = 0; i < Polyline.Num(); ++i)
		{
			const FVector2D P(Polyline[i].X, Polyline[i].Y);
			if (i > 0)
			{
				Cum += FVector2D::Distance(Edge.Samples[i - 1], P);
			}
			Edge.Samples.Add(P);
			Edge.CumLen.Add(Cum);
		}
		Edge.Length = Cum;

		const int32 EdgeIdx = Nav.Edges.Add(MoveTemp(Edge));
		Nav.Adjacency[*A].Add(EdgeIdx);
		Nav.Adjacency[*B].Add(EdgeIdx);

		// Index the samples for ProjectToRoad.
		const FSimNavEdge& Stored = Nav.Edges[EdgeIdx];
		for (int32 i = 0; i < Stored.Samples.Num(); ++i)
		{
			Nav.SampleHash.Insert(Stored.Samples[i], EdgeIdx, i);
		}
	}

	FSimNavParams Params;
	Params.RoadSpeedFactor   = S->RoadSpeedFactor;
	Params.RoadCostMul       = S->RoadCostMul;
	Params.MaxAttachDist     = S->MaxAttachDist;
	Params.WaypointReachDist = S->WaypointReachDist;

	// Direct apply is safe: OnNetworkChanged only fires on the game thread
	// outside FSimWorld::Tick, so the swap is never mid-phase (§9 / §3).
	SimSub->GetSim().SetNavRoads(MoveTemp(Nav), Params);

	UE_LOG(LogTemp, Log, TEXT("[RealmNav] Baked nav: %d nodes, %d edges (v%u)."),
		SimSub->GetSim().GetNavRoads().Nodes.Num(),
		SimSub->GetSim().GetNavRoads().Edges.Num(),
		SimSub->GetSim().GetNavVersion());
}

void USimNavRoadsBuilder::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

#if !UE_BUILD_SHIPPING
	if (CVarDrawAgentPaths.GetValueOnGameThread() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	USimSubsystem* SimSub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	if (!SimSub)
	{
		return;
	}
	const FSimWorld& Sim = SimSub->GetSim();
	const FSimSnapshot& Snap = SimSub->GetSnapshot();
	for (int32 i = 0; i < Snap.Agents.Num(); ++i)
	{
		const FAgentSnapshot& A = Snap.Agents[i];
		if (!IsMovingState(A.State))
		{
			continue;   // only walking villagers have an active route
		}
		FAgentPath Path;
		if (Sim.GetAgentPath(i, Path) && !Path.IsConsumed())
		{
			DrawPathOverlay(World, A.Position, Path);   // refreshed each tick
		}
	}
#endif
}
