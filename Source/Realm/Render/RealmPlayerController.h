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
class SIntroPanel;
class SResourcePanel;
class SWorkerPanel;
class URoadBuildToolComponent;

UCLASS()
class REALM_API ARealmPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARealmPlayerController();

	// True while a *building* blueprint (not None, not Road) is armed. The camera
	// pawn queries this to suppress wheel zoom (the wheel rotates the ghost then).
	bool IsBuildingGhostArmed() const;

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
	void OnRoadUndo();
	void OnRoadCommit();
	void OnRoadCurveInc();
	void OnRoadCurveDec();
	void OnBuildRotateCW();   // wheel down: rotate the ghost clockwise (from above)
	void OnBuildRotateCCW();  // wheel up: rotate counterclockwise
	void HandleBlueprintClicked(EBlueprintKind Kind);
	void HandleAssignWorker(int32 BuildingIndex);
	void HandleUnassignWorker(int32 BuildingIndex);
	void HandleUpgradeHouse(int32 BuildingIndex, ETier Target);
	void HandleDowngradeHouse(int32 BuildingIndex);
	bool TraceCursorToGround(FVector& OutLoc) const;

	// Resolves the armed building's ghost: cursor → ground, optional road snap
	// (unless Alt/disabled), and the dual validity verdict (sim spacing + no road
	// overlap). Shared by the Tick ghost render and the LMB commit so they agree.
	bool ComputeBuildingPlacement(EBuildingType Type, FVector& OutPos,
		float& OutYaw, bool& bOutValid) const;

	// Road drawing state machine (Roads/RoadBuildTool.h); armed while the
	// "Road" blueprint is selected.
	UPROPERTY()
	TObjectPtr<URoadBuildToolComponent> RoadTool;

	TSharedPtr<SBlueprintBar> BlueprintBar;
	TSharedPtr<SResourcePanel> ResourcePanel;
	TSharedPtr<SWorkerPanel> WorkerPanel;
	TSharedPtr<SIntroPanel> IntroPanel;   // TEMP intro window

	EBlueprintKind SelectedBlueprint = EBlueprintKind::None;

	// Manual ghost yaw (mouse wheel), reset whenever a blueprint is (dis)armed.
	// Snapping to a road overrides this; hold Alt for full manual control.
	float ManualYawDegrees = 0.f;
};
