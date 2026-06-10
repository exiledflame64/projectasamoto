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

namespace
{
	// Basic cube/cylinder meshes are 100 cm tall and centred on the origin, so a
	// shape sits on the ground when lifted by half its scaled height.
	float GroundLift(const AActor* A) { return 50.f * A->GetActorScale3D().Z; }

	struct FBuildingLook
	{
		FVector      Scale;
		FLinearColor Color;
	};

	FBuildingLook LookFor(EBuildingType Type)
	{
		switch (Type)
		{
		case EBuildingType::Warehouse:  return { FVector(2.5f, 2.5f, 2.0f), FLinearColor(0.15f, 0.4f, 0.9f)  };
		case EBuildingType::Sawmill:    return { FVector(2.2f, 2.2f, 1.8f), FLinearColor(0.85f, 0.45f, 0.1f) };
		case EBuildingType::Farm:       return { FVector(2.4f, 2.4f, 1.2f), FLinearColor(0.55f, 0.7f, 0.15f) };
		case EBuildingType::Lumberyard:
		default:                        return { FVector(2.0f, 2.0f, 1.5f), FLinearColor(0.5f, 0.3f, 0.12f)  };
		}
	}
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

	// --- Buildings (tinted cubes per type) ---
	for (int32 i = 0; i < Snap.Buildings.Num(); ++i)
	{
		const EBuildingType Type = Snap.Buildings[i].Type;

		// Respawn when the type at this index changed (happens after a load).
		if (BuildingVisuals.IsValidIndex(i) && BuildingVisualTypes[i] != Type)
		{
			if (BuildingVisuals[i])
			{
				BuildingVisuals[i]->Destroy();
			}
			const FBuildingLook Look = LookFor(Type);
			BuildingVisuals[i]      = SpawnShape(CubeMesh, Look.Scale, Look.Color);
			BuildingVisualTypes[i]  = Type;
		}
		if (i >= BuildingVisuals.Num())
		{
			const FBuildingLook Look = LookFor(Type);
			BuildingVisuals.Add(SpawnShape(CubeMesh, Look.Scale, Look.Color));
			BuildingVisualTypes.Add(Type);
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
			V->UpdateVisual(Snap.Agents[i], CamLoc);
		}
	}

	// --- Prune proxies beyond the snapshot (arrays shrink after a load) ---
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
