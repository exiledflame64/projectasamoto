// Copyright Asamoto.
// Phase 0 game mode: installs the RTS camera pawn and spawns a flat ground plane
// to move over. No gameplay yet.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RealmGameMode.generated.h"

UCLASS()
class REALM_API ARealmGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARealmGameMode();

protected:
	virtual void BeginPlay() override;

	// Half-extent of the spawned ground plane, in centimetres.
	UPROPERTY(EditDefaultsOnly, Category = "Realm")
	float GroundHalfSize = 10000.f;

	// Starting world: trees on a ring around a central stocked warehouse.
	UPROPERTY(EditDefaultsOnly, Category = "Realm")
	int32 NumTrees = 10;

	UPROPERTY(EditDefaultsOnly, Category = "Realm")
	float TreeRingRadius = 1500.f;

	// Phase 2: food in the warehouse at game start (the starvation countdown).
	UPROPERTY(EditDefaultsOnly, Category = "Realm")
	int32 StartingFood = 10;

private:
	void SpawnGroundPlane();
	void SeedSimWorld();
	void SpawnVisualizer();
};
