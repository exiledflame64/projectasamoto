// Copyright Asamoto.

#include "RealmGameMode.h"
#include "RTSCameraPawn.h"
#include "RealmPlayerController.h"
#include "SimVisualizer.h"
#include "Core/SimSubsystem.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ARealmGameMode::ARealmGameMode()
{
	DefaultPawnClass      = ARTSCameraPawn::StaticClass();
	PlayerControllerClass = ARealmPlayerController::StaticClass();
}

void ARealmGameMode::BeginPlay()
{
	Super::BeginPlay();
	SpawnGroundPlane();
	SeedSimWorld();
	SpawnVisualizer();
}

void ARealmGameMode::SeedSimWorld()
{
	UGameInstance* GI = GetGameInstance();
	USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	if (!Sub)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Realm] No SimSubsystem; cannot seed world."));
		return;
	}

	FSimWorld& Sim = Sub->GetSim();

	// Central warehouse, stocked with starting food: the Phase 2 pressure clock —
	// build a farm before the pantry empties or the settlement starves.
	const FBuildingId WarehouseId = Sim.PlaceBuilding(EBuildingType::Warehouse, FVector::ZeroVector);
	Sim.AddResource(WarehouseId, EResource::Food, StartingFood);

	// A ring of trees to chop. Lumberyards + villagers arrive when the player builds.
	for (int32 i = 0; i < NumTrees; ++i)
	{
		const float Angle = (2.f * PI * i) / FMath::Max(NumTrees, 1);
		const FVector Pos(FMath::Cos(Angle) * TreeRingRadius,
			FMath::Sin(Angle) * TreeRingRadius, 0.f);
		Sim.SpawnTree(Pos);
	}

	UE_LOG(LogTemp, Log, TEXT("[Realm] Seeded sim: 1 warehouse (%d food) + %d trees. Pick a blueprint, then click the ground."),
		StartingFood, NumTrees);
}

void ARealmGameMode::SpawnVisualizer()
{
	if (UWorld* World = GetWorld())
	{
		World->SpawnActor<ASimVisualizer>(ASimVisualizer::StaticClass(),
			FTransform::Identity);
	}
}

void ARealmGameMode::SpawnGroundPlane()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Engine's 1x1 m basic plane (100 uu). Scale up to the desired half-size.
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!PlaneMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Realm] Could not load /Engine/BasicShapes/Plane."));
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* Ground = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), FTransform::Identity, Params);
	if (!Ground)
	{
		return;
	}

	UStaticMeshComponent* MeshComp = Ground->GetStaticMeshComponent();
	MeshComp->SetMobility(EComponentMobility::Movable);
	MeshComp->SetStaticMesh(PlaneMesh);

	// Basic plane is 100 uu across; scale so half-size == GroundHalfSize.
	const float Scale = (GroundHalfSize * 2.f) / 100.f;
	Ground->SetActorScale3D(FVector(Scale, Scale, 1.f));
	Ground->SetActorLocation(FVector::ZeroVector);

	UE_LOG(LogTemp, Log, TEXT("[Realm] Ground plane spawned (half-size %.0f cm)."),
		GroundHalfSize);
}
