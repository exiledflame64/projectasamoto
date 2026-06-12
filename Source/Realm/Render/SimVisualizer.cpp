// Copyright Asamoto.

#include "SimVisualizer.h"
#include "AgentVisual.h"
#include "Render/Visuals/BuildingVisualSet.h"
#include "Render/Visuals/VegetationVisualSet.h"
#include "Core/SimSubsystem.h"

#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

ASimVisualizer::ASimVisualizer()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASimVisualizer::BeginPlay()
{
	Super::BeginPlay();
	BuildingSet = UBuildingVisualSet::Resolve();
	VegetationSet = UVegetationVisualSet::Resolve();
}

AStaticMeshActor* ASimVisualizer::SpawnVisual(const FRealmMeshDef& Def)
{
	if (!GetWorld())
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
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Def.ApplyTo(Comp, this);
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
	if (!Sub || !BuildingSet || !VegetationSet)
	{
		return;
	}

	const FSimSnapshot& Snap = Sub->GetSnapshot();
	const FVector CamLoc = GetCameraLocation();

	// --- Buildings ---
	for (int32 i = 0; i < Snap.Buildings.Num(); ++i)
	{
		const EBuildingType Type = Snap.Buildings[i].Type;
		const FRealmMeshDef& Def = BuildingSet->BuildingDef(Type);

		// Respawn when the type at this index changed (happens after a load).
		if (BuildingVisuals.IsValidIndex(i) && BuildingVisualTypes[i] != Type)
		{
			if (BuildingVisuals[i])
			{
				BuildingVisuals[i]->Destroy();
			}
			if (FieldVisuals.IsValidIndex(i) && FieldVisuals[i])
			{
				FieldVisuals[i]->Destroy();
				FieldVisuals[i] = nullptr;
			}
			BuildingVisuals[i]     = SpawnVisual(Def);
			BuildingVisualTypes[i] = Type;
			if (Type == EBuildingType::Farm)
			{
				FieldVisuals[i] = SpawnVisual(BuildingSet->FieldPlot);
			}
		}
		if (i >= BuildingVisuals.Num())
		{
			BuildingVisuals.Add(SpawnVisual(Def));
			BuildingVisualTypes.Add(Type);
			FieldVisuals.Add(Type == EBuildingType::Farm
				? SpawnVisual(BuildingSet->FieldPlot) : nullptr);
		}
		if (AStaticMeshActor* A = BuildingVisuals[i])
		{
			// Seeds may carry a per-instance scale; zero means the def's own.
			const FVector EffScale = Def.EffectiveScale(Snap.Buildings[i].VisualScale);
			A->SetActorScale3D(EffScale);
			A->SetActorLocation(Snap.Buildings[i].Position
				+ FVector(0.f, 0.f, Def.GroundLiftFor(EffScale)));
		}
		// Field plot (the farm's sub-building) sits beside the farm.
		if (FieldVisuals.IsValidIndex(i) && FieldVisuals[i])
		{
			FieldVisuals[i]->SetActorLocation(Snap.Buildings[i].Position
				+ FVector(FarmFieldOffset, 0.f, BuildingSet->FieldPlot.GroundLift() + 1.f));
		}
	}

	// --- Trees (hidden once chopped out or built over) ---
	for (int32 i = 0; i < Snap.Trees.Num(); ++i)
	{
		if (i >= TreeVisuals.Num())
		{
			TreeVisuals.Add(SpawnVisual(VegetationSet->Tree));
		}
		if (AStaticMeshActor* A = TreeVisuals[i])
		{
			const FVector EffScale = VegetationSet->Tree.EffectiveScale(Snap.Trees[i].VisualScale);
			A->SetActorScale3D(EffScale);
			A->SetActorLocation(Snap.Trees[i].Position
				+ FVector(0.f, 0.f, VegetationSet->Tree.GroundLiftFor(EffScale)));
			A->SetActorHiddenInGame(Snap.Trees[i].Remaining <= 0);
		}
	}

	// --- Agents (villager + state label) ---
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
			V->UpdateVisual(Snap.Agents[i], CamLoc);
		}
	}

	// --- Prune proxies beyond the snapshot (arrays shrink after a load) ---
	while (FieldVisuals.Num() > Snap.Buildings.Num())
	{
		if (FieldVisuals.Last())
		{
			FieldVisuals.Last()->Destroy();
		}
		FieldVisuals.Pop();
	}
	PruneTo(Snap.Buildings.Num(), BuildingVisuals, &BuildingVisualTypes);
	PruneTo(Snap.Trees.Num(), TreeVisuals, nullptr);
	while (AgentVisuals.Num() > Snap.Agents.Num())
	{
		if (AgentVisuals.Last())
		{
			AgentVisuals.Last()->Destroy();
		}
		AgentVisuals.Pop();
	}
}

void ASimVisualizer::PruneTo(int32 Count, TArray<TObjectPtr<AStaticMeshActor>>& Visuals,
	TArray<EBuildingType>* Types)
{
	while (Visuals.Num() > Count)
	{
		if (Visuals.Last())
		{
			Visuals.Last()->Destroy();
		}
		Visuals.Pop();
		if (Types)
		{
			Types->Pop();
		}
	}
}
