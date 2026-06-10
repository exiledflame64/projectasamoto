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
class SResourcePanel;

UCLASS()
class REALM_API ARealmPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaSeconds) override;   // placement ghost preview

	// Villagers spawned automatically beside each placed building. Phase 2 runs
	// a multi-villager economy, so each building brings a small crew.
	UPROPERTY(EditAnywhere, Category = "Realm", meta = (ClampMin = "0"))
	int32 VillagersPerBuilding = 3;

private:
	void OnPlaceBuilding();
	void OnTogglePause();
	void HandleBlueprintClicked(EBlueprintKind Kind);
	bool TraceCursorToGround(FVector& OutLoc) const;

	TSharedPtr<SBlueprintBar> BlueprintBar;
	TSharedPtr<SResourcePanel> ResourcePanel;
	EBlueprintKind SelectedBlueprint = EBlueprintKind::None;
};
