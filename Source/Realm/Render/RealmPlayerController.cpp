// Copyright Asamoto.

#include "RealmPlayerController.h"
#include "SBlueprintBar.h"
#include "Core/SimSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"

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
	}
}

void ARealmPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BlueprintBar.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(BlueprintBar.ToSharedRef());
	}
	BlueprintBar.Reset();

	Super::EndPlay(EndPlayReason);
}

void ARealmPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindAction("Place_Building", IE_Pressed,
			this, &ARealmPlayerController::OnPlaceBuilding);
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
	// No blueprint armed: clicks on the ground do nothing.
	if (SelectedBlueprint != EBlueprintKind::Building)
	{
		return;
	}

	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, /*bTraceComplex=*/false, Hit) || !Hit.bBlockingHit)
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	USimSubsystem* Sub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}

	FVector Loc = Hit.Location;
	Loc.Z = 0.f;   // sim lives on the ground plane

	FSimWorld& Sim = Sub->GetSim();
	Sim.PlaceBuilding(EBuildingType::Lumberyard, Loc);

	// Each building brings its own villagers, spread on a ring beside it.
	for (int32 i = 0; i < VillagersPerBuilding; ++i)
	{
		const float Angle = (2.f * PI * i) / FMath::Max(VillagersPerBuilding, 1);
		Sim.SpawnAgent(Loc + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 120.f);
	}

	// Blueprint stays armed so several buildings can be placed in a row.
	UE_LOG(LogTemp, Log, TEXT("[Realm] Lumberyard placed at %s (+%d villagers)."),
		*Loc.ToString(), VillagersPerBuilding);
}
