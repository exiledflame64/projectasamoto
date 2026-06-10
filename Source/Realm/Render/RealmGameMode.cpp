// Copyright Asamoto.

#include "RealmGameMode.h"
#include "RTSCameraPawn.h"
#include "RealmPlayerController.h"
#include "RealmSeeds.h"
#include "SimVisualizer.h"
#include "Core/SimSubsystem.h"
#include "EngineUtils.h"
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

	// A level that contains seed actors authors its own starting layout (each
	// seed registers itself into the sim); procedural seeding is only the
	// fallback for empty maps.
	if (TActorIterator<AResourceSeed>(GetWorld()) || TActorIterator<ABuildingSeed>(GetWorld()))
	{
		UE_LOG(LogTemp, Log, TEXT("[Realm] Level provides seed actors; skipping procedural seeding."));
		return;
	}

	FSimWorld& Sim = Sub->GetSim();

	// A ring of trees to chop. Everything else — warehouse included — is
	// placed by the player (warehouse first; houses bring the villagers).
	for (int32 i = 0; i < NumTrees; ++i)
	{
		const float Angle = (2.f * PI * i) / FMath::Max(NumTrees, 1);
		const FVector Pos(FMath::Cos(Angle) * TreeRingRadius,
			FMath::Sin(Angle) * TreeRingRadius, 0.f);
		Sim.SpawnTree(Pos);
	}

	UE_LOG(LogTemp, Log, TEXT("[Realm] Seeded sim: %d trees. Place a warehouse, then houses for villagers."),
		NumTrees);
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

	// A level can place its own ground (tag an actor "RealmGround").
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("RealmGround")))
		{
			return;
		}
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
