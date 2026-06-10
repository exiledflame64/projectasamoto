// Copyright Asamoto.
// Input + build/workforce UI: the blueprint bar arms a blueprint, left-click
// ("Place_Building") ground-traces and places into the sim. Houses grant one
// idle villager each; the worker panel assigns idle villagers to buildings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BlueprintCatalog.h"
#include "RealmPlayerController.generated.h"

class SBlueprintBar;
class SResourcePanel;
class SWorkerPanel;

UCLASS()
class REALM_API ARealmPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaSeconds) override;   // ghost preview + UI refresh

	// Food stocked into the warehouse when it is placed (the starvation
	// countdown starts ticking the moment the first villager arrives).
	UPROPERTY(EditAnywhere, Category = "Realm", meta = (ClampMin = "0"))
	int32 WarehouseStartingFood = 10;

private:
	void OnPlaceBuilding();
	void OnTogglePause();
	void HandleBlueprintClicked(EBlueprintKind Kind);
	void HandleAssignWorker(int32 BuildingIndex);
	void HandleUnassignWorker(int32 BuildingIndex);
	bool TraceCursorToGround(FVector& OutLoc) const;

	TSharedPtr<SBlueprintBar> BlueprintBar;
	TSharedPtr<SResourcePanel> ResourcePanel;
	TSharedPtr<SWorkerPanel> WorkerPanel;
	EBlueprintKind SelectedBlueprint = EBlueprintKind::None;
};
