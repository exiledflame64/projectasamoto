// Copyright Asamoto.

#include "RealmSeeds.h"
#include "Render/Visuals/BuildingVisualSet.h"
#include "Render/Visuals/VegetationVisualSet.h"
#include "Core/SimSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"

namespace
{
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
}

void AResourceSeed::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Preview comes from the shared visual set, so the editor shows exactly
	// what the runtime proxy will look like. (Per-instance mesh edits are
	// intentionally overridden — author appearance in the VegetationVisualSet
	// asset.)
	// NOTE: Mesh is the ROOT component — never set its relative location or
	// (after the first run) scale here; both would overwrite the actor's
	// editor-authored transform on every construction.
	const UVegetationVisualSet* Set = UVegetationVisualSet::Resolve();
	Set->Tree.ApplyTo(Mesh, this, /*bApplyScale=*/false);   // per-Kind lookup when stone/iron arrive
	if (!bScaleInitialized)
	{
		Mesh->SetRelativeScale3D(Set->Tree.Scale);
		bScaleInitialized = true;
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
			// Per-instance editor scale carries through to the runtime proxy.
			Sub->GetSim().SpawnTree(Loc, GetActorScale3D());
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
}

void ABuildingSeed::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Same root-component caveat as AResourceSeed: appearance only, no transform.
	const FRealmMeshDef& Def = UBuildingVisualSet::Resolve()->BuildingDef(Type);
	Def.ApplyTo(Mesh, this, /*bApplyScale=*/false);
	if (!bScaleInitialized)
	{
		Mesh->SetRelativeScale3D(Def.Scale);
		bScaleInitialized = true;
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
		const FBuildingId Id = Sim.PlaceBuilding(Type, Loc, GetActorScale3D());
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
