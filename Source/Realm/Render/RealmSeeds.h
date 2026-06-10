// Copyright Asamoto.
// Editor-placeable seed actors: visible placeholder meshes you can arrange in
// the level viewport; at BeginPlay each registers itself into the sim world and
// hides (the SimVisualizer then owns the runtime proxy). This keeps the sim the
// single source of truth while letting the starting layout live in the .umap.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/SimTypes.h"
#include "RealmSeeds.generated.h"

class UStaticMeshComponent;

// Kinds of harvestable nodes a map can scatter. Only trees exist in the sim so
// far; stone/iron deposits land here when mining arrives.
UENUM(BlueprintType)
enum class EResourceNodeKind : uint8
{
	Tree
	// Stone, Iron, ... later
};

// A harvestable resource node (tree for now; stone/iron later).
UCLASS()
class REALM_API AResourceSeed : public AActor
{
	GENERATED_BODY()

public:
	AResourceSeed();

	UPROPERTY(EditAnywhere, Category = "Realm")
	EResourceNodeKind Kind = EResourceNodeKind::Tree;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Realm")
	TObjectPtr<UStaticMeshComponent> Mesh;
};

// A pre-built building. Optionally pre-stocked with food (warehouse) and/or
// accompanied by idle villagers spawned beside it.
UCLASS()
class REALM_API ABuildingSeed : public AActor
{
	GENERATED_BODY()

public:
	ABuildingSeed();

	UPROPERTY(EditAnywhere, Category = "Realm")
	EBuildingType Type = EBuildingType::Warehouse;

	UPROPERTY(EditAnywhere, Category = "Realm", meta = (ClampMin = "0"))
	int32 StartingFood = 0;

	// Idle villagers spawned beside the building (assign them via the worker UI).
	UPROPERTY(EditAnywhere, Category = "Realm", meta = (ClampMin = "0"))
	int32 Villagers = 0;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;   // editor preview tint/scale

	UPROPERTY(VisibleAnywhere, Category = "Realm")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
