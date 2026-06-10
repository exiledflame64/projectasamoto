// Copyright Asamoto.
// Phase 1 input + build UI: a Slate blueprint bar arms a blueprint; only then
// does left-click ("Place_Building") ground-trace and place into the sim.
// Placing a building auto-spawns its villagers (count is tunable).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BlueprintCatalog.h"
#include "RealmPlayerController.generated.h"

class SBlueprintBar;

UCLASS()
class REALM_API ARealmPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;

	// Villagers spawned automatically beside each placed building.
	UPROPERTY(EditAnywhere, Category = "Realm", meta = (ClampMin = "0"))
	int32 VillagersPerBuilding = 1;

private:
	void OnPlaceBuilding();
	void HandleBlueprintClicked(EBlueprintKind Kind);

	TSharedPtr<SBlueprintBar> BlueprintBar;
	EBlueprintKind SelectedBlueprint = EBlueprintKind::None;
};
