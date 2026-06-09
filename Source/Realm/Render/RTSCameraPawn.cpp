// Copyright Asamoto.

#include "RTSCameraPawn.h"
#include "Components/SceneComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"

#include "Core/SimSubsystem.h"
#include "Save/RealmSaveGame.h"

const FString ARTSCameraPawn::SaveSlotName = TEXT("RealmPhase0");

ARTSCameraPawn::ARTSCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(SceneRoot);
	SpringArm->TargetArmLength = TargetArmLength;
	SpringArm->SetRelativeRotation(FRotator(-55.f, 0.f, 0.f)); // look down at the ground
	SpringArm->bDoCollisionTest = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 10.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
}

void ARTSCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	TargetArmLength = SpringArm->TargetArmLength;

	// Take over input so the pawn receives axis/action mappings.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = true;
		PC->bEnableClickEvents = true;
		PC->bEnableMouseOverEvents = true;
	}
}

void ARTSCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis("Cam_MoveForward", this, &ARTSCameraPawn::OnMoveForward);
	PlayerInputComponent->BindAxis("Cam_MoveRight",   this, &ARTSCameraPawn::OnMoveRight);
	PlayerInputComponent->BindAxis("Cam_Zoom",        this, &ARTSCameraPawn::OnZoom);

	PlayerInputComponent->BindAction("Cam_RotateLeft",  IE_Pressed,  this, &ARTSCameraPawn::OnRotateLeftPressed);
	PlayerInputComponent->BindAction("Cam_RotateLeft",  IE_Released, this, &ARTSCameraPawn::OnRotateLeftReleased);
	PlayerInputComponent->BindAction("Cam_RotateRight", IE_Pressed,  this, &ARTSCameraPawn::OnRotateRightPressed);
	PlayerInputComponent->BindAction("Cam_RotateRight", IE_Released, this, &ARTSCameraPawn::OnRotateRightReleased);
	PlayerInputComponent->BindAction("Cam_RotateDrag",  IE_Pressed,  this, &ARTSCameraPawn::OnDragRotatePressed);
	PlayerInputComponent->BindAction("Cam_RotateDrag",  IE_Released, this, &ARTSCameraPawn::OnDragRotateReleased);

	PlayerInputComponent->BindAction("Debug_SaveGame", IE_Pressed, this, &ARTSCameraPawn::OnSaveGame);
	PlayerInputComponent->BindAction("Debug_LoadGame", IE_Pressed, this, &ARTSCameraPawn::OnLoadGame);
}

void ARTSCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ApplyPan(DeltaSeconds);
	ApplyEdgeScroll(DeltaSeconds);
	ApplyRotate(DeltaSeconds);
	ApplyZoom(DeltaSeconds);

	// Reset per-frame axis accumulators (BindAxis re-fires every frame).
	MoveForwardInput = 0.f;
	MoveRightInput   = 0.f;
	PendingZoom      = 0.f;
}

// --- Input accumulation ---
void ARTSCameraPawn::OnMoveForward(float Value) { MoveForwardInput = Value; }
void ARTSCameraPawn::OnMoveRight(float Value)   { MoveRightInput = Value; }
void ARTSCameraPawn::OnZoom(float Value)        { PendingZoom = Value; }

// --- Camera math ---
void ARTSCameraPawn::ApplyPan(float DeltaSeconds)
{
	if (FMath::IsNearlyZero(MoveForwardInput) && FMath::IsNearlyZero(MoveRightInput))
	{
		return;
	}

	// Move in the ground plane relative to the camera's yaw (ignore pitch).
	const float Yaw = SpringArm->GetComponentRotation().Yaw;
	const FRotator YawOnly(0.f, Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);

	const FVector Delta = (Forward * MoveForwardInput + Right * MoveRightInput)
		* PanSpeed * DeltaSeconds;
	AddActorWorldOffset(FVector(Delta.X, Delta.Y, 0.f));
}

void ARTSCameraPawn::ApplyEdgeScroll(float DeltaSeconds)
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->ShouldShowMouseCursor())
	{
		return;
	}

	float MouseX = 0.f, MouseY = 0.f;
	if (!PC->GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	int32 ViewX = 0, ViewY = 0;
	PC->GetViewportSize(ViewX, ViewY);
	if (ViewX <= 0 || ViewY <= 0)
	{
		return;
	}

	float EdgeFwd = 0.f, EdgeRight = 0.f;
	if (MouseX <= EdgeScrollMargin)            EdgeRight = -1.f;
	else if (MouseX >= ViewX - EdgeScrollMargin) EdgeRight = 1.f;
	if (MouseY <= EdgeScrollMargin)            EdgeFwd = 1.f;
	else if (MouseY >= ViewY - EdgeScrollMargin) EdgeFwd = -1.f;

	if (FMath::IsNearlyZero(EdgeFwd) && FMath::IsNearlyZero(EdgeRight))
	{
		return;
	}

	const float Yaw = SpringArm->GetComponentRotation().Yaw;
	const FRotator YawOnly(0.f, Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
	const FVector Right   = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);

	const FVector Delta = (Forward * EdgeFwd + Right * EdgeRight)
		* PanSpeed * DeltaSeconds;
	AddActorWorldOffset(FVector(Delta.X, Delta.Y, 0.f));
}

void ARTSCameraPawn::ApplyRotate(float DeltaSeconds)
{
	float YawDelta = 0.f;

	// Q/E continuous rotation.
	if (bRotateLeft)  YawDelta -= RotateSpeed * DeltaSeconds;
	if (bRotateRight) YawDelta += RotateSpeed * DeltaSeconds;

	// Middle-mouse drag rotation: scale raw mouse-X delta directly.
	if (bDragRotate)
	{
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			float DX = 0.f, DY = 0.f;
			PC->GetInputMouseDelta(DX, DY);
			YawDelta += DX * DragRotateSpeed;
		}
	}

	if (!FMath::IsNearlyZero(YawDelta))
	{
		FRotator Rot = SpringArm->GetRelativeRotation();
		Rot.Yaw += YawDelta;
		SpringArm->SetRelativeRotation(Rot);
	}
}

void ARTSCameraPawn::ApplyZoom(float DeltaSeconds)
{
	if (!FMath::IsNearlyZero(PendingZoom))
	{
		TargetArmLength = FMath::Clamp(
			TargetArmLength - PendingZoom * ZoomStep,
			MinArmLength, MaxArmLength);
	}

	SpringArm->TargetArmLength = FMath::FInterpTo(
		SpringArm->TargetArmLength, TargetArmLength, DeltaSeconds, 10.f);
}

// --- Phase 0 save/load stub ---
void ARTSCameraPawn::OnSaveGame()
{
	UGameInstance* GI = GetGameInstance();
	USimSubsystem* SimSub = GI ? GI->GetSubsystem<USimSubsystem>() : nullptr;
	if (!SimSub)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RealmSave] No SimSubsystem; cannot save."));
		return;
	}

	URealmSaveGame* SaveObj = Cast<URealmSaveGame>(
		UGameplayStatics::CreateSaveGameObject(URealmSaveGame::StaticClass()));
	if (!SaveObj)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RealmSave] Failed to create save object."));
		return;
	}

	SaveObj->TickCount = static_cast<int32>(SimSub->GetSim().GetTickNumber());
	const bool bOk = UGameplayStatics::SaveGameToSlot(SaveObj, SaveSlotName, SaveUserIndex);
	UE_LOG(LogTemp, Log, TEXT("[RealmSave] Save %s slot='%s' TickCount=%d"),
		bOk ? TEXT("OK") : TEXT("FAILED"), *SaveSlotName, SaveObj->TickCount);
}

void ARTSCameraPawn::OnLoadGame()
{
	if (!UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[RealmSave] No save in slot '%s'."), *SaveSlotName);
		return;
	}

	URealmSaveGame* Loaded = Cast<URealmSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));
	if (!Loaded)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RealmSave] Load FAILED for slot '%s'."), *SaveSlotName);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[RealmSave] Load OK slot='%s' TickCount=%d (round-trip verified)"),
		*SaveSlotName, Loaded->TickCount);

	// Demonstrate the value actually round-trips into the sim.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USimSubsystem* SimSub = GI->GetSubsystem<USimSubsystem>())
		{
			SimSub->GetSim().SetTickNumber(Loaded->TickCount);
		}
	}
}
