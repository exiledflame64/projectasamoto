// Copyright Asamoto.

#include "Roads/RoadNetworkSubsystem.h"
#include "Roads/RoadSettings.h"
#include "Roads/TerrainHeight.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "TimerManager.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING
// Dev verification without clicking: commits a curved test road and grabs a
// screenshot a few seconds later (Saved/Screenshots/.../RoadDebug.png), so
// the RVT composite can be checked from a headless -game run.
static FAutoConsoleCommandWithWorld GRealmDebugRoadCmd(
	TEXT("realm.DebugRoad"),
	TEXT("Commit a test road near the origin and screenshot the result."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		URoadNetworkSubsystem* Roads = World ? World->GetSubsystem<URoadNetworkSubsystem>() : nullptr;
		if (!Roads)
		{
			return;
		}
		TArray<FRoadCommitPoint> Points;
		Points.Add({ FVector(-3000, -2500, 0), 0.5f });
		Points.Add({ FVector(-500, -2500, 0), 0.5f });
		Points.Add({ FVector(2500, -500, 0), 0.8f });
		const TArray<FGuid> Edges = Roads->CommitPolyline(Points,
			URoadSettings::Get()->DefaultWidth, ERoadTier::DirtPath);
		UE_LOG(LogTemp, Log, TEXT("[Realm] realm.DebugRoad committed %d edges."), Edges.Num());

		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([]
		{
			FScreenshotRequest::RequestScreenshot(TEXT("RoadDebug"), false, false);
		}), 5.f, false);
	}));
#endif // !UE_BUILD_SHIPPING

FRoadGraph::FHeightFn URoadNetworkSubsystem::MakeHeightFn() const
{
	TWeakObjectPtr<const UWorld> WeakWorld = GetWorld();
	return [WeakWorld](const FVector2D& XY, float& OutZ) -> bool
	{
		const UWorld* World = WeakWorld.Get();
		const UTerrainHeightSubsystem* Terrain =
			World ? World->GetSubsystem<UTerrainHeightSubsystem>() : nullptr;
		return Terrain && Terrain->GetHeightAt(XY, OutZ);
	};
}

FGuid URoadNetworkSubsystem::ResolveCommitNode(const FVector& Position,
	float SnapRadius, TArray<FGuid>& DirtyEdges)
{
	// 1) Existing node wins.
	const FGuid Nearest = Graph.FindNearestNode(Position, SnapRadius);
	if (Nearest.IsValid())
	{
		return Nearest;
	}

	// 2) Point on an existing edge: split it, connect at the new junction.
	FGuid EdgeId;
	float T;
	if (Graph.FindNearestPointOnAnyEdge(Position, SnapRadius, EdgeId, T))
	{
		return Graph.SplitEdge(EdgeId, T, &DirtyEdges);
	}

	// 3) Free point, Z from the height provider.
	FVector Snapped = Position;
	float Z;
	if (MakeHeightFn()(FVector2D(Position.X, Position.Y), Z))
	{
		Snapped.Z = Z;
	}
	return Graph.AddNode(Snapped, ERoadNodeType::Free);
}

TArray<FGuid> URoadNetworkSubsystem::CommitPolyline(
	const TArray<FRoadCommitPoint>& Points, float Width, ERoadTier Tier)
{
	TArray<FGuid> NewEdges;
	if (Points.Num() < 2)
	{
		return NewEdges;
	}

	const float SnapRadius = URoadSettings::Get()->SnapRadius;
	TArray<FGuid> DirtyEdges;

	FGuid PrevNode = ResolveCommitNode(Points[0].Position, SnapRadius, DirtyEdges);
	for (int32 i = 1; i < Points.Num(); ++i)
	{
		const FGuid Node = ResolveCommitNode(Points[i].Position, SnapRadius, DirtyEdges);
		if (Node.IsValid() && PrevNode.IsValid() && Node != PrevNode)
		{
			const FGuid Edge = Graph.AddEdge(PrevNode, Node,
				Points[i].Curvature, Width, Tier);
			if (Edge.IsValid())
			{
				NewEdges.Add(Edge);
				DirtyEdges.Add(Edge);

				// A new edge changes the continuation tangents of its
				// neighbors — mark them dirty so their ribbons re-blend.
				for (const FGuid& NodeId : { PrevNode, Node })
				{
					if (const FRoadNode* N = Graph.FindNode(NodeId))
					{
						DirtyEdges.Append(N->Edges);
					}
				}
			}
		}
		PrevNode = Node;
	}

	if (!DirtyEdges.IsEmpty())
	{
		OnNetworkChanged.Broadcast(DirtyEdges);
	}
	return NewEdges;
}

bool URoadNetworkSubsystem::RemoveEdge(const FGuid& EdgeId)
{
	// Capture neighbors before removal: their tangents change too.
	TArray<FGuid> DirtyEdges;
	DirtyEdges.Add(EdgeId);
	if (const FRoadEdge* Edge = Graph.FindEdge(EdgeId))
	{
		for (const FGuid& NodeId : { Edge->NodeA, Edge->NodeB })
		{
			if (const FRoadNode* Node = Graph.FindNode(NodeId))
			{
				DirtyEdges.Append(Node->Edges);
			}
		}
	}

	if (!Graph.RemoveEdge(EdgeId))
	{
		return false;
	}
	OnNetworkChanged.Broadcast(DirtyEdges);
	return true;
}

TArray<FVector> URoadNetworkSubsystem::GetEdgePolyline(const FGuid& EdgeId) const
{
	return Graph.SampleEdgePolyline(EdgeId,
		URoadSettings::Get()->SampleSpacing, MakeHeightFn());
}

void URoadNetworkSubsystem::SerializeToBytes(TArray<uint8>& OutBytes)
{
	OutBytes.Reset();
	FMemoryWriter Writer(OutBytes);
	Graph.Serialize(Writer);
}

void URoadNetworkSubsystem::LoadFromBytes(const TArray<uint8>& Bytes)
{
	if (Bytes.IsEmpty())
	{
		Graph.Reset();
	}
	else
	{
		FMemoryReader Reader(Bytes);
		Graph.Serialize(Reader);
	}
	BroadcastAllEdgesDirty();
}

void URoadNetworkSubsystem::BroadcastAllEdgesDirty()
{
	// The renderer prunes components whose edge id no longer exists, so a
	// full-rebuild broadcast is just "every current edge is dirty".
	TArray<FGuid> AllEdges;
	Graph.GetEdges().GenerateKeyArray(AllEdges);
	OnNetworkChanged.Broadcast(AllEdges);
}
