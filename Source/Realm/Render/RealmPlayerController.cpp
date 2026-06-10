// Copyright Asamoto.

#include "RealmPlayerController.h"
#include "SBlueprintBar.h"
#include "SResourcePanel.h"
#include "Core/SimSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "DrawDebugHelpers.h"

void ARealmPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// CaptureDuringMouseDown processes the initial click on an unfocused
	// viewport; NoCapture swallows it as focus-only, forcing double-clicks.
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	if (GEngine && GEngine->GameViewport)
	{
		SAssignNew(BlueprintBar, SBlueprintBar)
			.OnBlueprintClicked(SBlueprintBar::FOnBlueprintClicked::CreateUObject(
				this, &ARealmPlayerController::HandleBlueprintClicked));
		GEngine->GameViewport->AddViewportWidgetContent(BlueprintBar.ToSharedRef(), /*ZOrder=*/10);

		// Resource readout polls the snapshot through weak attribute lambdas, so
		// it stays a pure reader of sim state (hard rule #2).
		TWeakObjectPtr<UGameInstance> WeakGI = GetGameInstance();
		const auto GetSnapshot = [WeakGI]() -> const FSimSnapshot*
		{
			const UGameInstance* GI = WeakGI.Get();
			const USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
			return Sub ? &Sub->GetSnapshot() : nullptr;
		};

		SAssignNew(ResourcePanel, SResourcePanel)
			.ResourceText(TAttribute<FText>::CreateLambda([GetSnapshot]
			{
				if (const FSimSnapshot* S = GetSnapshot())
				{
					return FText::Format(
						NSLOCTEXT("Realm", "ResourceReadout",
							"Wood: {0}   Planks: {1}   Food: {2}   Population: {3}"),
						S->LogCount, S->PlankCount, S->FoodCount, S->Population);
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
	}
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
	}
	BlueprintBar.Reset();
	ResourcePanel.Reset();

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
	}
}

void ARealmPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Placement ghost: with a blueprint armed, show the footprint under the
	// cursor, green when the spot is valid, red when the sim would refuse it.
	const FBlueprintDef* Def = FindBlueprintDef(SelectedBlueprint);
	if (!Def || !Def->bAvailable)
	{
		return;
	}

	FVector Loc;
	if (!TraceCursorToGround(Loc))
	{
		return;
	}

	const UGameInstance* GI = GetGameInstance();
	const USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}

	// Generic footprint extent (debug-art scale; per-type sizes live render-side).
	const FVector Half(120.f, 120.f, 80.f);
	const FVector Center = Loc + FVector(0.f, 0.f, Half.Z);
	const bool bValid = Sub->GetSim().CanPlaceBuilding(Loc);

	const FColor Fill = bValid ? FColor(30, 200, 80, 70) : FColor(220, 45, 30, 70);
	const FColor Line = bValid ? FColor(40, 230, 100)    : FColor(255, 60, 40);
	DrawDebugSolidBox(GetWorld(), Center, Half, Fill);
	DrawDebugBox(GetWorld(), Center, Half, Line);
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

	// The clicked button took Slate focus; hand it back so the next viewport
	// click acts immediately instead of spending itself on refocusing.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void ARealmPlayerController::OnPlaceBuilding()
{
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
	if (Sim.PlaceBuilding(Def->BuildingType, Loc) == INVALID_ID)
	{
		return;   // invalid spot (ghost was red); keep the blueprint armed
	}

	// Each building brings its own villagers, spread on a ring beside it.
	for (int32 i = 0; i < VillagersPerBuilding; ++i)
	{
		const float Angle = (2.f * PI * i) / FMath::Max(VillagersPerBuilding, 1);
		Sim.SpawnAgent(Loc + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 120.f);
	}

	// Blueprint stays armed so several buildings can be placed in a row.
	UE_LOG(LogTemp, Log, TEXT("[Realm] %s placed at %s (+%d villagers)."),
		*Def->DisplayName.ToString(), *Loc.ToString(), VillagersPerBuilding);
}
