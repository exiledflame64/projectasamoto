// Copyright Asamoto.

#include "RTSCameraPawn.h"
#include "Components/SceneComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/InputSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "Core/SimSubsystem.h"
#include "Save/RealmSaveGame.h"
#include "Roads/RoadNetworkSubsystem.h"
#include "RealmPlayerController.h"

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
	PlayerInputComponent->BindAction("Cam_PanDrag",     IE_Pressed,  this, &ARTSCameraPawn::OnDragPanPressed);
	PlayerInputComponent->BindAction("Cam_PanDrag",     IE_Released, this, &ARTSCameraPawn::OnDragPanReleased);

	PlayerInputComponent->BindAction("Debug_SaveGame", IE_Pressed, this, &ARTSCameraPawn::OnSaveGame);
	PlayerInputComponent->BindAction("Debug_LoadGame", IE_Pressed, this, &ARTSCameraPawn::OnLoadGame);

	// Diagnostic: confirms the renamed ini mapping actually loaded (a stale
	// editor session or Saved-config override would report 0 here).
	TArray<FInputActionKeyMapping> PanMappings;
	UInputSettings::GetInputSettings()->GetActionMappingByName(TEXT("Cam_PanDrag"), PanMappings);
	UE_LOG(LogTemp, Log, TEXT("[RealmCam] Input bound; Cam_PanDrag has %d key mapping(s)."),
		PanMappings.Num());
}

void ARTSCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ApplyPan(DeltaSeconds);
	ApplyEdgeScroll(DeltaSeconds);
	ApplyDragPan();
	ApplyRotate(DeltaSeconds);
	ApplyZoom(DeltaSeconds);

	// Reset per-frame axis accumulators (BindAxis re-fires every frame).
	MoveForwardInput = 0.f;
	MoveRightInput   = 0.f;
	PendingZoom      = 0.f;
}

// --- Input accumulation ---
void ARTSCameraPawn::OnDragPanPressed()
{
	bDragPan = DeprojectCursorToGround(DragAnchor);

	// Camera lag would make the anchor swim under the cursor; hold it 1:1
	// (the editor viewport has no lag either).
	SpringArm->bEnableCameraLag = !bDragPan;

	UE_LOG(LogTemp, Log, TEXT("[RealmCam] Pan drag %s at %s."),
		bDragPan ? TEXT("started") : TEXT("rejected (no ground under cursor)"),
		*DragAnchor.ToString());
}

void ARTSCameraPawn::OnDragPanReleased()
{
	bDragPan = false;
	SpringArm->bEnableCameraLag = true;
	// Verbose: focus-loss key flushes re-fire IE_Released, which would spam at Log.
	UE_LOG(LogTemp, Verbose, TEXT("[RealmCam] Pan drag released."));
}

void ARTSCameraPawn::OnMoveForward(float Value) { MoveForwardInput = Value; }
void ARTSCameraPawn::OnMoveRight(float Value)   { MoveRightInput = Value; }
void ARTSCameraPawn::OnZoom(float Value)
{
	// Ctrl + wheel belongs to the road tool (segment curvature) — the axis
	// fires regardless of modifiers, so the camera must stand down here.
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC && (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl)))
	{
		return;
	}
	// While a building blueprint is armed the wheel rotates the ghost instead of
	// zooming (same stand-down pattern as the road tool's Ctrl+wheel).
	if (const ARealmPlayerController* RealmPC = Cast<ARealmPlayerController>(PC))
	{
		if (RealmPC->IsBuildingGhostArmed())
		{
			return;
		}
	}
	PendingZoom = Value;
}

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
	// Edge scroll yields entirely to an active middle-drag pan.
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (bDragPan || !PC || !PC->ShouldShowMouseCursor())
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

// Editor-viewport-style pan: keep the ground point grabbed at drag start under
// the cursor. Built on cursor deprojection rather than mouse deltas, because in
// NoCapture mouse mode (cursor visible for UI/placement) raw deltas are never
// routed to player input.
void ARTSCameraPawn::ApplyDragPan()
{
	if (!bDragPan)
	{
		return;
	}

	FVector Cur;
	if (DeprojectCursorToGround(Cur))
	{
		// Move the rig so the anchor lands back under the cursor.
		AddActorWorldOffset(FVector(DragAnchor.X - Cur.X, DragAnchor.Y - Cur.Y, 0.f));
	}
}

bool ARTSCameraPawn::DeprojectCursorToGround(FVector& OutGroundPos) const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	FVector Origin, Dir;
	if (!PC || !PC->DeprojectMousePositionToWorld(Origin, Dir))
	{
		return false;
	}
	if (Dir.Z > -KINDA_SMALL_NUMBER)   // ray parallel to / away from the ground
	{
		return false;
	}

	OutGroundPos = Origin - Dir * (Origin.Z / Dir.Z);
	OutGroundPos.Z = 0.f;
	return true;
}

void ARTSCameraPawn::ApplyRotate(float DeltaSeconds)
{
	float YawDelta = 0.f;

	// Q/E continuous rotation.
	if (bRotateLeft)  YawDelta -= RotateSpeed * DeltaSeconds;
	if (bRotateRight) YawDelta += RotateSpeed * DeltaSeconds;

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
		const float OldTarget = TargetArmLength;
		TargetArmLength = FMath::Clamp(
			TargetArmLength - PendingZoom * ZoomStep,
			MinArmLength, MaxArmLength);

		// Zoom toward the cursor: slide the rig so the ground point under the
		// mouse holds (approximately) still on screen, RTS-style. Zooming out
		// inverts the fraction and backs away from that point symmetrically.
		FVector Anchor;
		if (TargetArmLength != OldTarget && DeprojectCursorToGround(Anchor))
		{
			const float Frac = 1.f - TargetArmLength / OldTarget;
			const FVector ToAnchor = Anchor - GetActorLocation();
			AddActorWorldOffset(FVector(ToAnchor.X * Frac, ToAnchor.Y * Frac, 0.f));
		}
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

	// Phase 2: serialize the whole sim world (agents, buildings, trees).
	FMemoryWriter Writer(SaveObj->SimBytes);
	SimSub->GetSim().Serialize(Writer);

	// Road network graph rides along as its own blob.
	if (URoadNetworkSubsystem* Roads = GetWorld()->GetSubsystem<URoadNetworkSubsystem>())
	{
		Roads->SerializeToBytes(SaveObj->RoadBytes);
	}

	const bool bOk = UGameplayStatics::SaveGameToSlot(SaveObj, SaveSlotName, SaveUserIndex);
	UE_LOG(LogTemp, Log, TEXT("[RealmSave] Save %s slot='%s' Tick=%d SimBytes=%d RoadBytes=%d"),
		bOk ? TEXT("OK") : TEXT("FAILED"), *SaveSlotName, SaveObj->TickCount,
		SaveObj->SimBytes.Num(), SaveObj->RoadBytes.Num());
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

	// Phase 2: restore the whole sim world. The visualizer reconciles its proxy
	// actors against the (possibly smaller) loaded arrays on its next tick.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USimSubsystem* SimSub = GI->GetSubsystem<USimSubsystem>())
		{
			FMemoryReader Reader(Loaded->SimBytes);
			SimSub->GetSim().Serialize(Reader);
		}
	}

	// Roads: an empty blob (pre-road save) clears the network; the renderer
	// rebuilds/prunes from the change broadcast.
	if (URoadNetworkSubsystem* Roads = GetWorld()->GetSubsystem<URoadNetworkSubsystem>())
	{
		Roads->LoadFromBytes(Loaded->RoadBytes);
	}

	UE_LOG(LogTemp, Log, TEXT("[RealmSave] Load OK slot='%s' Tick=%d SimBytes=%d RoadBytes=%d"),
		*SaveSlotName, Loaded->TickCount, Loaded->SimBytes.Num(), Loaded->RoadBytes.Num());
}
