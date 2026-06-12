// Copyright Asamoto.

#include "Roads/RoadRendererSubsystem.h"
#include "Roads/RoadNetworkSubsystem.h"
#include "Roads/RoadSettings.h"
#include "Roads/TerrainHeight.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FIndex3i;

namespace
{
	// Append one feathered ribbon strip for a polyline into Mesh. 4 verts per
	// sample: outer pair fades to alpha 0 (the road material erodes its edge
	// from it), inner pair spans the actual width. Banked: right vector
	// projected to the terrain normal so the ribbon leans with slopes instead
	// of staying world-flat.
	void AppendRibbon(FDynamicMesh3& Mesh, const TArray<FVector>& Polyline,
		float Width, float ZOffset, float FeatherFraction, float TilingLength,
		const UTerrainHeightSubsystem* Terrain)
	{
		const int32 N = Polyline.Num();
		if (N < 2)
		{
			return;
		}

		UE::Geometry::FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV();
		UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		UE::Geometry::FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();

		const float Half = Width * 0.5f;
		const float Outer = Half * (1.f + FeatherFraction);
		// U across the full (feather-to-feather) extent.
		const float UInner0 = (Outer - Half) / (2.f * Outer);
		const float UInner1 = (Outer + Half) / (2.f * Outer);

		TArray<int32> Vids, UVIds, NIds, CIds;
		Vids.Reserve(N * 4); UVIds.Reserve(N * 4); NIds.Reserve(N * 4); CIds.Reserve(N * 4);

		float ArcLength = 0.f;
		FVector PrevRight = FVector::ZeroVector;

		for (int32 i = 0; i < N; ++i)
		{
			const FVector Tangent =
				(Polyline[FMath::Min(i + 1, N - 1)] - Polyline[FMath::Max(i - 1, 0)]).GetSafeNormal();

			FVector Up = FVector::UpVector;
			if (Terrain)
			{
				Terrain->GetNormalAt(FVector2D(Polyline[i].X, Polyline[i].Y), Up);
			}

			FVector Right = FVector::CrossProduct(Tangent, Up).GetSafeNormal();
			if (Right.IsNearlyZero())
			{
				Right = FVector::CrossProduct(Tangent, FVector::UpVector).GetSafeNormal();
			}
			// Sharp turns flip the frame (self-intersection); clamp by keeping
			// frame continuity — the build tool already rejects reversals.
			if (i > 0 && FVector::DotProduct(Right, PrevRight) < 0.f)
			{
				Right = -Right;
			}
			PrevRight = Right;

			if (i > 0)
			{
				ArcLength += FVector::Dist(Polyline[i - 1], Polyline[i]);
			}
			const float V = ArcLength / FMath::Max(TilingLength, 1.f);

			const FVector Center = Polyline[i] + FVector(0, 0, ZOffset);
			const FVector Pos[4] = {
				Center - Right * Outer, Center - Right * Half,
				Center + Right * Half,  Center + Right * Outer };
			const float U[4] = { 0.f, UInner0, UInner1, 1.f };
			const float Alpha[4] = { 0.f, 1.f, 1.f, 0.f };

			for (int32 c = 0; c < 4; ++c)
			{
				Vids.Add(Mesh.AppendVertex(FVector3d(Pos[c])));
				UVIds.Add(UVs->AppendElement(FVector2f(U[c], V)));
				NIds.Add(Normals->AppendElement(FVector3f(Up)));
				CIds.Add(Colors->AppendElement(FVector4f(1.f, 1.f, 1.f, Alpha[c])));
			}
		}

		for (int32 i = 0; i + 1 < N; ++i)
		{
			const int32 Row = i * 4;
			const int32 Next = Row + 4;
			for (int32 c = 0; c < 3; ++c)
			{
				const int32 Quad[4] = { Row + c, Row + c + 1, Next + c, Next + c + 1 };
				const FIndex3i TriA(Quad[0], Quad[2], Quad[1]);
				const FIndex3i TriB(Quad[1], Quad[2], Quad[3]);
				for (const FIndex3i& Tri : { TriA, TriB })
				{
					const int32 Tid = Mesh.AppendTriangle(
						Vids[Tri.A], Vids[Tri.B], Vids[Tri.C]);
					if (Tid >= 0)
					{
						UVs->SetTriangle(Tid, FIndex3i(UVIds[Tri.A], UVIds[Tri.B], UVIds[Tri.C]));
						Normals->SetTriangle(Tid, FIndex3i(NIds[Tri.A], NIds[Tri.B], NIds[Tri.C]));
						Colors->SetTriangle(Tid, FIndex3i(CIds[Tri.A], CIds[Tri.B], CIds[Tri.C]));
					}
				}
			}
		}
	}

	// Feathered disc (junction stamp): center + full-alpha ring + zero-alpha rim.
	void AppendDisc(FDynamicMesh3& Mesh, const FVector& Center, float Radius,
		float ZOffset, float FeatherFraction, float TilingLength,
		const UTerrainHeightSubsystem* Terrain)
	{
		constexpr int32 Segments = 24;
		UE::Geometry::FDynamicMeshUVOverlay* UVs = Mesh.Attributes()->PrimaryUV();
		UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		UE::Geometry::FDynamicMeshColorOverlay* Colors = Mesh.Attributes()->PrimaryColors();

		auto GroundedPoint = [&](const FVector& XY)
		{
			FVector P = XY;
			float Z;
			if (Terrain && Terrain->GetHeightAt(FVector2D(P.X, P.Y), Z))
			{
				P.Z = Z;
			}
			P.Z += ZOffset;
			return P;
		};
		auto AddVert = [&](const FVector& P, float Alpha)
		{
			const int32 Vid = Mesh.AppendVertex(FVector3d(P));
			UVs->AppendElement(FVector2f(P.X / TilingLength, P.Y / TilingLength));
			Normals->AppendElement(FVector3f(FVector::UpVector));
			Colors->AppendElement(FVector4f(1.f, 1.f, 1.f, Alpha));
			return Vid;
		};
		auto AddTri = [&](int32 A, int32 B, int32 C)
		{
			const int32 Tid = Mesh.AppendTriangle(A, B, C);
			if (Tid >= 0)
			{
				// Overlay elements were appended in vertex order.
				UVs->SetTriangle(Tid, FIndex3i(A, B, C));
				Normals->SetTriangle(Tid, FIndex3i(A, B, C));
				Colors->SetTriangle(Tid, FIndex3i(A, B, C));
			}
		};

		const int32 CenterVid = AddVert(GroundedPoint(Center), 1.f);
		TArray<int32> Inner, OuterRing;
		for (int32 s = 0; s < Segments; ++s)
		{
			const float Angle = 2.f * PI * s / Segments;
			const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
			Inner.Add(AddVert(GroundedPoint(Center + Dir * Radius), 1.f));
			OuterRing.Add(AddVert(GroundedPoint(Center + Dir * Radius * (1.f + FeatherFraction)), 0.f));
		}
		for (int32 s = 0; s < Segments; ++s)
		{
			const int32 SNext = (s + 1) % Segments;
			AddTri(CenterVid, Inner[s], Inner[SNext]);
			AddTri(Inner[s], OuterRing[s], Inner[SNext]);
			AddTri(Inner[SNext], OuterRing[s], OuterRing[SNext]);
		}
	}

	void PrepareMeshAttributes(FDynamicMesh3& Mesh)
	{
		Mesh.EnableAttributes();
		Mesh.Attributes()->EnablePrimaryColors();
	}

	// UE renders front faces by winding; make the strip face up regardless of
	// the polyline's direction (overlay normals stay as authored).
	void EnsureUpFacing(FDynamicMesh3& Mesh)
	{
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriNormal(Tid).Z < 0.0)
			{
				Mesh.ReverseOrientation(/*bFlipNormals=*/false);
			}
			break;
		}
	}

	UMaterialInterface* MakeTintedFallback(UObject* Outer, const FLinearColor& Tint)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Base)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
		MID->SetVectorParameterValue(TEXT("Color"), Tint);
		return MID;
	}
}

void URoadRendererSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const URoadSettings* Settings = URoadSettings::Get();
	CommittedMaterial = Settings->RoadMaterial.LoadSynchronous();
	if (!CommittedMaterial)
	{
		UE_LOG(LogTemp, Log, TEXT("[Realm] Road material not authored yet "
			"(run Tools/setup_road_assets.py); using tinted fallback."));
		CommittedMaterial = MakeTintedFallback(this, FLinearColor(0.28f, 0.19f, 0.11f));   // packed dirt
	}

	if (UMaterialInterface* PreviewBase = Settings->PreviewMaterial.LoadSynchronous())
	{
		UMaterialInstanceDynamic* Valid = UMaterialInstanceDynamic::Create(PreviewBase, this);
		Valid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.1f, 0.85f, 0.3f, 0.5f));
		PreviewMaterialValid = Valid;
		UMaterialInstanceDynamic* Invalid = UMaterialInstanceDynamic::Create(PreviewBase, this);
		Invalid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.9f, 0.15f, 0.1f, 0.5f));
		PreviewMaterialInvalid = Invalid;
	}
	else
	{
		PreviewMaterialValid = MakeTintedFallback(this, FLinearColor(0.1f, 0.85f, 0.3f));
		PreviewMaterialInvalid = MakeTintedFallback(this, FLinearColor(0.9f, 0.15f, 0.1f));
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	MeshRoot = InWorld.SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	USceneComponent* Root = NewObject<USceneComponent>(MeshRoot, TEXT("Root"));
	MeshRoot->SetRootComponent(Root);
	Root->RegisterComponent();

	if (URoadNetworkSubsystem* Network = InWorld.GetSubsystem<URoadNetworkSubsystem>())
	{
		Network->OnNetworkChanged.AddUObject(this, &URoadRendererSubsystem::HandleNetworkChanged);

		// Catch state committed before BeginPlay (e.g. a load during startup).
		TArray<FGuid> AllEdges;
		Network->GetGraph().GetEdges().GenerateKeyArray(AllEdges);
		if (!AllEdges.IsEmpty())
		{
			HandleNetworkChanged(AllEdges);
		}
	}
}

void URoadRendererSubsystem::Deinitialize()
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

void URoadRendererSubsystem::HandleNetworkChanged(const TArray<FGuid>& DirtyEdges)
{
	URoadNetworkSubsystem* Network = GetWorld()->GetSubsystem<URoadNetworkSubsystem>();
	if (!Network || !MeshRoot)
	{
		return;
	}
	const FRoadGraph& Graph = Network->GetGraph();

	TSet<FGuid> DirtyNodes;
	for (const FGuid& EdgeId : TSet<FGuid>(DirtyEdges))
	{
		if (const FRoadEdge* Edge = Graph.FindEdge(EdgeId))
		{
			RebuildEdge(EdgeId);
			DirtyNodes.Add(Edge->NodeA);
			DirtyNodes.Add(Edge->NodeB);
		}
	}

	PruneStaleComponents();

	for (const FGuid& NodeId : DirtyNodes)
	{
		RebuildJunction(NodeId);
	}
}

void URoadRendererSubsystem::RebuildEdge(const FGuid& EdgeId)
{
	URoadNetworkSubsystem* Network = GetWorld()->GetSubsystem<URoadNetworkSubsystem>();
	const FRoadEdge* Edge = Network ? Network->GetGraph().FindEdge(EdgeId) : nullptr;
	if (!Edge)
	{
		return;
	}

	const TArray<FVector> Polyline = Network->GetEdgePolyline(EdgeId);
	if (Polyline.Num() < 2)
	{
		return;
	}

	TObjectPtr<UDynamicMeshComponent>& Comp = EdgeMeshes.FindOrAdd(EdgeId);
	if (!Comp)
	{
		Comp = CreateRoadMeshComponent(TEXT("RoadEdge"), CommittedMaterial);
	}

	const URoadSettings* S = URoadSettings::Get();
	FDynamicMesh3 Mesh;
	PrepareMeshAttributes(Mesh);
	AppendRibbon(Mesh, Polyline, Edge->Width, S->RibbonZOffset, S->FeatherFraction,
		S->TilingLength, GetWorld()->GetSubsystem<UTerrainHeightSubsystem>());
	EnsureUpFacing(Mesh);
	Comp->SetMesh(MoveTemp(Mesh));
}

void URoadRendererSubsystem::RebuildJunction(const FGuid& NodeId)
{
	URoadNetworkSubsystem* Network = GetWorld()->GetSubsystem<URoadNetworkSubsystem>();
	const FRoadGraph& Graph = Network->GetGraph();
	const FRoadNode* Node = Graph.FindNode(NodeId);

	// Discs only where ribbons actually overlap: degree >= 3.
	if (!Node || Node->Edges.Num() < 3)
	{
		TObjectPtr<UDynamicMeshComponent> Comp;
		if (JunctionMeshes.RemoveAndCopyValue(NodeId, Comp) && Comp)
		{
			Comp->DestroyComponent();
		}
		return;
	}

	float MaxWidth = 0.f;
	for (const FGuid& EdgeId : Node->Edges)
	{
		if (const FRoadEdge* Edge = Graph.FindEdge(EdgeId))
		{
			MaxWidth = FMath::Max(MaxWidth, Edge->Width);
		}
	}

	TObjectPtr<UDynamicMeshComponent>& Comp = JunctionMeshes.FindOrAdd(NodeId);
	if (!Comp)
	{
		Comp = CreateRoadMeshComponent(TEXT("RoadJunction"), CommittedMaterial);
	}

	const URoadSettings* S = URoadSettings::Get();
	const float Radius = MaxWidth * S->JunctionDiscRadiusFactor;
	FDynamicMesh3 Mesh;
	PrepareMeshAttributes(Mesh);
	// Junction sits a hair above the ribbons so the overlap never flickers.
	AppendDisc(Mesh, Node->Position, Radius, S->RibbonZOffset + 1.f, S->FeatherFraction,
		S->TilingLength, GetWorld()->GetSubsystem<UTerrainHeightSubsystem>());
	EnsureUpFacing(Mesh);
	Comp->SetMesh(MoveTemp(Mesh));
}

void URoadRendererSubsystem::PruneStaleComponents()
{
	const URoadNetworkSubsystem* Network = GetWorld()->GetSubsystem<URoadNetworkSubsystem>();
	if (!Network)
	{
		return;
	}
	const FRoadGraph& Graph = Network->GetGraph();

	for (auto It = EdgeMeshes.CreateIterator(); It; ++It)
	{
		if (!Graph.FindEdge(It->Key))
		{
			if (It->Value)
			{
				It->Value->DestroyComponent();
			}
			It.RemoveCurrent();
		}
	}
	for (auto It = JunctionMeshes.CreateIterator(); It; ++It)
	{
		const FRoadNode* Node = Graph.FindNode(It->Key);
		if (!Node || Node->Edges.Num() < 3)
		{
			if (It->Value)
			{
				It->Value->DestroyComponent();
			}
			It.RemoveCurrent();
		}
	}
}

UDynamicMeshComponent* URoadRendererSubsystem::CreateRoadMeshComponent(
	FName BaseName, UMaterialInterface* Material)
{
	UDynamicMeshComponent* Comp = NewObject<UDynamicMeshComponent>(
		MeshRoot, MakeUniqueObjectName(MeshRoot, UDynamicMeshComponent::StaticClass(), BaseName));
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCastShadow(false);
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	Comp->RegisterComponent();
	Comp->AttachToComponent(MeshRoot->GetRootComponent(),
		FAttachmentTransformRules::KeepWorldTransform);
	return Comp;
}

void URoadRendererSubsystem::SetPreview(const TArray<FRoadPreviewSegment>& Segments, float Width)
{
	if (!MeshRoot)
	{
		return;
	}
	if (!PreviewValidMesh)
	{
		PreviewValidMesh = CreateRoadMeshComponent(TEXT("RoadPreviewValid"),
			PreviewMaterialValid);
		PreviewInvalidMesh = CreateRoadMeshComponent(TEXT("RoadPreviewInvalid"),
			PreviewMaterialInvalid);
	}

	const URoadSettings* S = URoadSettings::Get();
	const UTerrainHeightSubsystem* Terrain = GetWorld()->GetSubsystem<UTerrainHeightSubsystem>();

	// Preview floats slightly above committed ribbons so it stays legible
	// while re-tracing an existing road.
	const float PreviewZ = S->RibbonZOffset + 2.f;

	FDynamicMesh3 ValidMesh, InvalidMesh;
	PrepareMeshAttributes(ValidMesh);
	PrepareMeshAttributes(InvalidMesh);
	for (const FRoadPreviewSegment& Segment : Segments)
	{
		AppendRibbon(Segment.bValid ? ValidMesh : InvalidMesh, Segment.Polyline,
			Width, PreviewZ, S->FeatherFraction, S->TilingLength, Terrain);
	}
	EnsureUpFacing(ValidMesh);
	EnsureUpFacing(InvalidMesh);
	PreviewValidMesh->SetMesh(MoveTemp(ValidMesh));
	PreviewInvalidMesh->SetMesh(MoveTemp(InvalidMesh));
}

void URoadRendererSubsystem::ClearPreview()
{
	if (PreviewValidMesh)
	{
		PreviewValidMesh->SetMesh(FDynamicMesh3());
	}
	if (PreviewInvalidMesh)
	{
		PreviewInvalidMesh->SetMesh(FDynamicMesh3());
	}
}
