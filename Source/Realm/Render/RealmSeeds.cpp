// Copyright Asamoto.

#include "RealmSeeds.h"
#include "Core/SimSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Preview look per building type; keep in sync with SimVisualizer::LookFor.
	void GetBuildingLook(EBuildingType Type, FVector& OutScale, FLinearColor& OutColor)
	{
		switch (Type)
		{
		case EBuildingType::Warehouse:  OutScale = FVector(2.5f, 2.5f, 2.0f); OutColor = FLinearColor(0.15f, 0.4f, 0.9f);  break;
		case EBuildingType::Sawmill:    OutScale = FVector(2.2f, 2.2f, 1.8f); OutColor = FLinearColor(0.85f, 0.45f, 0.1f); break;
		case EBuildingType::Farm:       OutScale = FVector(2.4f, 2.4f, 1.2f); OutColor = FLinearColor(0.55f, 0.7f, 0.15f); break;
		case EBuildingType::House:      OutScale = FVector(1.5f, 1.5f, 1.2f); OutColor = FLinearColor(0.85f, 0.8f, 0.7f);  break;
		case EBuildingType::Lumberyard:
		default:                        OutScale = FVector(2.0f, 2.0f, 1.5f); OutColor = FLinearColor(0.5f, 0.3f, 0.12f);  break;
		}
	}

	USimSubsystem* GetSimSubsystem(const AActor* Actor)
	{
		const UGameInstance* GI = Actor->GetGameInstance();
		return GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	}
}

// --- AResourceSeed ---

AResourceSeed::AResourceSeed()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Tree preview: cylinder is 100 cm tall, origin-centred; lift to stand on origin.
	Mesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 3.0f));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 150.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CylinderFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderFinder.Object);
	}
	if (MatFinder.Succeeded())
	{
		Mesh->SetMaterial(0, MatFinder.Object);
	}
}

void AResourceSeed::BeginPlay()
{
	Super::BeginPlay();

	if (USimSubsystem* Sub = GetSimSubsystem(this))
	{
		FVector Loc = GetActorLocation();
		Loc.Z = 0.f;   // sim lives on the ground plane

		switch (Kind)
		{
		case EResourceNodeKind::Tree:
		default:
			Sub->GetSim().SpawnTree(Loc);
			break;
		}
	}

	// The visualizer owns the runtime proxy from here on.
	SetActorHiddenInGame(true);
}

// --- ABuildingSeed ---

ABuildingSeed::ABuildingSeed()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CubeFinder.Object);
	}
	if (MatFinder.Succeeded())
	{
		Mesh->SetMaterial(0, MatFinder.Object);
	}
}

void ABuildingSeed::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	FVector Scale;
	FLinearColor Color;
	GetBuildingLook(Type, Scale, Color);

	// Cube is 100 cm, origin-centred: lift by half height to stand on the origin.
	Mesh->SetRelativeScale3D(Scale);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 50.f * Scale.Z));

	// OnConstruction re-runs on every property edit; don't stack MIDs on MIDs.
	UMaterialInterface* Base = Mesh->GetMaterial(0);
	if (const UMaterialInstanceDynamic* AsMID = Cast<UMaterialInstanceDynamic>(Base))
	{
		Base = AsMID->Parent;
	}
	if (Base)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		Mesh->SetMaterial(0, MID);
	}
}

void ABuildingSeed::BeginPlay()
{
	Super::BeginPlay();

	if (USimSubsystem* Sub = GetSimSubsystem(this))
	{
		FVector Loc = GetActorLocation();
		Loc.Z = 0.f;

		FSimWorld& Sim = Sub->GetSim();
		const FBuildingId Id = Sim.PlaceBuilding(Type, Loc);
		if (Id == INVALID_ID)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Realm] BuildingSeed '%s' rejected (too close to another building, or duplicate warehouse)."),
				*GetName());
		}
		else
		{
			if (StartingFood > 0)
			{
				Sim.AddResource(Id, EResource::Food, StartingFood);
			}
			for (int32 i = 0; i < Villagers; ++i)
			{
				const float Angle = (2.f * PI * i) / FMath::Max(Villagers, 1);
				Sim.SpawnAgent(Loc + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 120.f);
			}
		}
	}

	SetActorHiddenInGame(true);
}
