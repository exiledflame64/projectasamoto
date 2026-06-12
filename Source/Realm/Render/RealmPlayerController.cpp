// Copyright Asamoto.

#include "RealmPlayerController.h"
#include "SBlueprintBar.h"
#include "SIntroPanel.h"
#include "SResourcePanel.h"
#include "SWorkerPanel.h"
#include "Core/SimSubsystem.h"
#include "Roads/RoadBuildTool.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "DrawDebugHelpers.h"

ARealmPlayerController::ARealmPlayerController()
{
	RoadTool = CreateDefaultSubobject<URoadBuildToolComponent>(TEXT("RoadBuildTool"));
}

void ARealmPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// CaptureDuringMouseDown processes the initial click on an unfocused
	// viewport; NoCapture swallows it as focus-only, forcing double-clicks.
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}

	// Snapshot access for the polling widgets: always through the subsystem,
	// so the UI stays a pure reader of sim state (hard rule #2).
	TWeakObjectPtr<UGameInstance> WeakGI = GetGameInstance();
	const auto GetSnapshot = [WeakGI]() -> const FSimSnapshot*
	{
		const UGameInstance* GI = WeakGI.Get();
		const USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
		return Sub ? &Sub->GetSnapshot() : nullptr;
	};

	SAssignNew(BlueprintBar, SBlueprintBar)
		.OnBlueprintClicked(SBlueprintBar::FOnBlueprintClicked::CreateUObject(
			this, &ARealmPlayerController::HandleBlueprintClicked));
	GEngine->GameViewport->AddViewportWidgetContent(BlueprintBar.ToSharedRef(), /*ZOrder=*/10);

	SAssignNew(ResourcePanel, SResourcePanel)
		.ResourceText(TAttribute<FText>::CreateLambda([GetSnapshot]
		{
			if (const FSimSnapshot* S = GetSnapshot())
			{
				return FText::Format(
					NSLOCTEXT("Realm", "ResourceReadout",
						"Wood: {0}   Planks: {1}   Food: {2}   Population: {3} (idle {4})"),
					S->LogCount, S->PlankCount, S->FoodCount, S->Population, S->IdleVillagers);
			}
			return FText::GetEmpty();
		}))
		.GameOverVisibility(TAttribute<EVisibility>::CreateLambda([GetSnapshot]
		{
			const FSimSnapshot* S = GetSnapshot();
			return (S && S->bGameOver) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		}))
		.PausedVisibility(TAttribute<EVisibility>::CreateLambda([WeakGI]
		{
			const UGameInstance* GI = WeakGI.Get();
			const USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
			return (Sub && Sub->IsSimPaused())
				? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		}));
	GEngine->GameViewport->AddViewportWidgetContent(ResourcePanel.ToSharedRef(), /*ZOrder=*/11);

	SAssignNew(WorkerPanel, SWorkerPanel)
		.GetSnapshot(GetSnapshot)
		.OnAssign(SWorkerPanel::FOnWorkerChange::CreateUObject(
			this, &ARealmPlayerController::HandleAssignWorker))
		.OnUnassign(SWorkerPanel::FOnWorkerChange::CreateUObject(
			this, &ARealmPlayerController::HandleUnassignWorker))
		.OnUpgradeHouse(SWorkerPanel::FOnHouseUpgrade::CreateUObject(
			this, &ARealmPlayerController::HandleUpgradeHouse))
		.OnDowngradeHouse(SWorkerPanel::FOnWorkerChange::CreateUObject(
			this, &ARealmPlayerController::HandleDowngradeHouse));
	GEngine->GameViewport->AddViewportWidgetContent(WorkerPanel.ToSharedRef(), /*ZOrder=*/12);

	// TEMP intro window (left side, under the resource readout).
	SAssignNew(IntroPanel, SIntroPanel);
	GEngine->GameViewport->AddViewportWidgetContent(IntroPanel.ToSharedRef(), /*ZOrder=*/13);
}

void ARealmPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GEngine && GEngine->GameViewport)
	{
		if (BlueprintBar.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(BlueprintBar.ToSharedRef());
		}
		if (ResourcePanel.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(ResourcePanel.ToSharedRef());
		}
		if (WorkerPanel.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(WorkerPanel.ToSharedRef());
		}
		// TEMP intro window
		if (IntroPanel.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(IntroPanel.ToSharedRef());
		}
	}
	BlueprintBar.Reset();
	ResourcePanel.Reset();
	WorkerPanel.Reset();
	IntroPanel.Reset();

	Super::EndPlay(EndPlayReason);
}

void ARealmPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindAction("Place_Building", IE_Pressed,
			this, &ARealmPlayerController::OnPlaceBuilding);
		InputComponent->BindAction("Toggle_Pause", IE_Pressed,
			this, &ARealmPlayerController::OnTogglePause);
		InputComponent->BindAction("Road_Undo", IE_Pressed,
			this, &ARealmPlayerController::OnRoadUndo);
		InputComponent->BindAction("Road_Commit", IE_Pressed,
			this, &ARealmPlayerController::OnRoadCommit);
		InputComponent->BindAction("Road_CurveInc", IE_Pressed,
			this, &ARealmPlayerController::OnRoadCurveInc);
		InputComponent->BindAction("Road_CurveDec", IE_Pressed,
			this, &ARealmPlayerController::OnRoadCurveDec);
	}
}

void ARealmPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UGameInstance* GI = GetGameInstance();
	const USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}
	const FSimSnapshot& Snap = Sub->GetSnapshot();

	// Workforce rows track the building list; warehouse blueprint greys out
	// while one exists (hard limit of 1, also enforced by the sim).
	if (WorkerPanel.IsValid())
	{
		WorkerPanel->Refresh();
	}
	if (BlueprintBar.IsValid())
	{
		bool bHasWarehouse = false;
		for (const FBuildingSnapshot& B : Snap.Buildings)
		{
			if (B.Type == EBuildingType::Warehouse)
			{
				bHasWarehouse = true;
				break;
			}
		}
		BlueprintBar->SetEntryEnabled(EBlueprintKind::Warehouse, !bHasWarehouse);
	}

	// Placement ghost: with a blueprint armed, show the footprint under the
	// cursor, green when the spot is valid, red when the sim would refuse it.
	// The road tool draws its own ribbon preview instead.
	const FBlueprintDef* Def = FindBlueprintDef(SelectedBlueprint);
	if (!Def || !Def->bAvailable || SelectedBlueprint == EBlueprintKind::Road)
	{
		return;
	}

	FVector Loc;
	if (!TraceCursorToGround(Loc))
	{
		return;
	}

	// Generic footprint extent (debug-art scale; per-type sizes live render-side).
	const FVector Half(120.f, 120.f, 80.f);
	const FVector Center = Loc + FVector(0.f, 0.f, Half.Z);
	const bool bValid = Sub->GetSim().CanPlaceBuilding(Def->BuildingType, Loc);

	const FColor Fill = bValid ? FColor(30, 200, 80, 70) : FColor(220, 45, 30, 70);
	const FColor Line = bValid ? FColor(40, 230, 100)    : FColor(255, 60, 40);
	DrawDebugSolidBox(GetWorld(), Center, Half, Fill);
	DrawDebugBox(GetWorld(), Center, Half, Line);

	// A farm also claims its field plot — preview it so the red/green verdict
	// is legible.
	if (Def->BuildingType == EBuildingType::Farm)
	{
		const FVector FieldHalf(FarmFieldHalfSize, FarmFieldHalfSize, 6.f);
		const FVector FieldCenter = Loc + FVector(FarmFieldOffset, 0.f, FieldHalf.Z);
		DrawDebugSolidBox(GetWorld(), FieldCenter, FieldHalf, Fill);
		DrawDebugBox(GetWorld(), FieldCenter, FieldHalf, Line);
	}
}

bool ARealmPlayerController::TraceCursorToGround(FVector& OutLoc) const
{
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex=*/false, Hit) || !Hit.bBlockingHit)
	{
		return false;
	}
	OutLoc = Hit.Location;
	OutLoc.Z = 0.f;   // sim lives on the ground plane
	return true;
}

void ARealmPlayerController::OnTogglePause()
{
	UGameInstance* GI = GetGameInstance();
	if (USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr)
	{
		Sub->SetSimPaused(!Sub->IsSimPaused());
	}
}

void ARealmPlayerController::HandleBlueprintClicked(EBlueprintKind Kind)
{
	// Click toggles: re-clicking the armed blueprint disarms it.
	SelectedBlueprint = (SelectedBlueprint == Kind) ? EBlueprintKind::None : Kind;
	if (BlueprintBar.IsValid())
	{
		BlueprintBar->SetSelected(SelectedBlueprint);
	}

	if (RoadTool)
	{
		RoadTool->SetToolActive(SelectedBlueprint == EBlueprintKind::Road);
	}

	// The clicked button took Slate focus; hand it back so the next viewport
	// click acts immediately instead of spending itself on refocusing.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ARealmPlayerController::HandleAssignWorker(int32 BuildingIndex)
{
	UGameInstance* GI = GetGameInstance();
	if (USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr)
	{
		Sub->GetSim().AssignWorkerTo(BuildingIndex);
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ARealmPlayerController::HandleUnassignWorker(int32 BuildingIndex)
{
	UGameInstance* GI = GetGameInstance();
	if (USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr)
	{
		Sub->GetSim().UnassignWorkerFrom(BuildingIndex);
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ARealmPlayerController::HandleUpgradeHouse(int32 BuildingIndex, ETier Target)
{
	UGameInstance* GI = GetGameInstance();
	if (USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr)
	{
		Sub->GetSim().UpgradeHouse(BuildingIndex, Target);
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ARealmPlayerController::HandleDowngradeHouse(int32 BuildingIndex)
{
	UGameInstance* GI = GetGameInstance();
	if (USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr)
	{
		Sub->GetSim().DowngradeHouse(BuildingIndex);
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ARealmPlayerController::OnRoadUndo()
{
	// RMB doubles as "cancel placement": with a building blueprint armed,
	// disarm it (same as re-clicking its bar button).
	if (!RoadTool || !RoadTool->IsToolActive())
	{
		if (SelectedBlueprint != EBlueprintKind::None)
		{
			HandleBlueprintClicked(SelectedBlueprint);
		}
		return;
	}
	if (!RoadTool->HandleUndo())
	{
		// Nothing left to pop: exit road mode entirely (disarm the blueprint).
		HandleBlueprintClicked(EBlueprintKind::Road);
	}
}

void ARealmPlayerController::OnRoadCommit()
{
	if (RoadTool && RoadTool->IsToolActive())
	{
		RoadTool->HandleCommit();
	}
}

void ARealmPlayerController::OnRoadCurveInc()
{
	if (RoadTool && RoadTool->IsToolActive())
	{
		RoadTool->AdjustCurvature(+1.f);
	}
}

void ARealmPlayerController::OnRoadCurveDec()
{
	if (RoadTool && RoadTool->IsToolActive())
	{
		RoadTool->AdjustCurvature(-1.f);
	}
}

void ARealmPlayerController::OnPlaceBuilding()
{
	// Road mode: clicks append road points instead of placing a building.
	if (SelectedBlueprint == EBlueprintKind::Road)
	{
		if (RoadTool)
		{
			RoadTool->HandleClick();
		}
		return;
	}

	const FBlueprintDef* Def = FindBlueprintDef(SelectedBlueprint);
	if (!Def || !Def->bAvailable)
	{
		return;   // no blueprint armed: clicks on the ground do nothing
	}

	FVector Loc;
	if (!TraceCursorToGround(Loc))
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}

	FSimWorld& Sim = Sub->GetSim();
	const FBuildingId Id = Sim.PlaceBuilding(Def->BuildingType, Loc);
	if (Id == INVALID_ID)
	{
		return;   // invalid spot (ghost was red); keep the blueprint armed
	}

	switch (Def->BuildingType)
	{
	case EBuildingType::House:
		// A house brings exactly one villager, idle until assigned. The home
		// link is what gives the villager its tier (the house's ResidentTier).
		Sim.SpawnAgent(Loc + FVector(120.f, 0.f, 0.f), Id);
		break;

	case EBuildingType::Warehouse:
		// Stock the (single) warehouse and disarm — there is nothing more to place.
		Sim.AddResource(Id, EResource::Food, WarehouseStartingFood);
		SelectedBlueprint = EBlueprintKind::None;
		if (BlueprintBar.IsValid())
		{
			BlueprintBar->SetSelected(SelectedBlueprint);
		}
		break;

	default:
		break;   // resource buildings: stays armed for repeat placement
	}

	UE_LOG(LogTemp, Log, TEXT("[Realm] %s placed at %s."),
		*Def->DisplayName.ToString(), *Loc.ToString());
}
