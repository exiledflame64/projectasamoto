// Copyright Asamoto.

#include "SimVisualizer.h"
#include "AgentVisual.h"
#include "Core/SimSubsystem.h"

#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

namespace
{
	// Basic cube/cylinder meshes are 100 cm tall and centred on the origin, so a
	// shape sits on the ground when lifted by half its scaled height.
	float GroundLift(const AActor* A) { return 50.f * A->GetActorScale3D().Z; }

	constexpr uint64 StorageMsgKey = 1001;
}

ASimVisualizer::ASimVisualizer()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASimVisualizer::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh      = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh  = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ShapeMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

AStaticMeshActor* ASimVisualizer::SpawnShape(UStaticMesh* Mesh, const FVector& Scale,
	const FLinearColor& Color)
{
	if (!Mesh || !GetWorld())
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), FTransform::Identity, Params);
	if (!Actor)
	{
		return nullptr;
	}

	UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Actor->SetActorScale3D(Scale);

	// Tint via a dynamic instance of BasicShapeMaterial (has a "Color" param).
	UMaterialInterface* Base = ShapeMaterial ? ShapeMaterial.Get() : Comp->GetMaterial(0);
	if (Base)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		Comp->SetMaterial(0, MID);
	}
	return Actor;
}

FVector ASimVisualizer::GetCameraLocation() const
{
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->PlayerCameraManager)
		{
			return PC->PlayerCameraManager->GetCameraLocation();
		}
	}
	return FVector::ZeroVector;
}

void ASimVisualizer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UGameInstance* GI = GetGameInstance();
	const USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}

	const FSimSnapshot& Snap = Sub->GetSnapshot();
	const FVector CamLoc = GetCameraLocation();

	// --- Buildings (cube: brown lumberyard / blue storage) ---
	for (int32 i = 0; i < Snap.Buildings.Num(); ++i)
	{
		if (i >= BuildingVisuals.Num())
		{
			const bool bStorage = (Snap.Buildings[i].Type == EBuildingType::Storage);
			const FVector Scale  = bStorage ? FVector(2.5f, 2.5f, 2.0f) : FVector(2.0f, 2.0f, 1.5f);
			const FLinearColor Color = bStorage
				? FLinearColor(0.15f, 0.4f, 0.9f)   // storage blue
				: FLinearColor(0.5f, 0.3f, 0.12f);  // lumberyard brown
			BuildingVisuals.Add(SpawnShape(CubeMesh, Scale, Color));
		}
		if (AStaticMeshActor* A = BuildingVisuals[i])
		{
			A->SetActorLocation(Snap.Buildings[i].Position + FVector(0.f, 0.f, GroundLift(A)));
		}
	}

	// --- Trees (green cylinder; hidden once chopped out) ---
	for (int32 i = 0; i < Snap.Trees.Num(); ++i)
	{
		if (i >= TreeVisuals.Num())
		{
			TreeVisuals.Add(SpawnShape(CylinderMesh,
				FVector(0.6f, 0.6f, 3.0f), FLinearColor(0.1f, 0.55f, 0.15f)));
		}
		if (AStaticMeshActor* A = TreeVisuals[i])
		{
			A->SetActorLocation(Snap.Trees[i].Position + FVector(0.f, 0.f, GroundLift(A)));
			A->SetActorHiddenInGame(Snap.Trees[i].Remaining <= 0);
		}
	}

	// --- Agents (cube villager + state label) ---
	for (int32 i = 0; i < Snap.Agents.Num(); ++i)
	{
		if (i >= AgentVisuals.Num())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AgentVisuals.Add(GetWorld()->SpawnActor<AAgentVisual>(
				AAgentVisual::StaticClass(), FTransform::Identity, Params));
		}
		if (AAgentVisual* V = AgentVisuals[i])
		{
			const FAgentSnapshot& A = Snap.Agents[i];
			V->UpdateVisual(A.Position, A.State, A.CarriedAmount, CamLoc);
		}
	}

	// --- Storage readout ---
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(StorageMsgKey, 0.f, FColor::Cyan,
			FString::Printf(TEXT("Storage logs: %d   (sim tick %lld)"),
				Snap.StorageLogCount, Snap.TickNumber));
	}
}
