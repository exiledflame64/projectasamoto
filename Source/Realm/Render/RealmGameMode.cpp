// Copyright Asamoto.

#include "RealmGameMode.h"
#include "RTSCameraPawn.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"

ARealmGameMode::ARealmGameMode()
{
	DefaultPawnClass      = ARTSCameraPawn::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
}

void ARealmGameMode::BeginPlay()
{
	Super::BeginPlay();
	SpawnGroundPlane();
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
