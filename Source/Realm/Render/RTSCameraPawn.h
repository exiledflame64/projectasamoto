// Copyright Asamoto.
// Phase 0 RTS camera: WASD/edge-scroll pan, middle-drag pan (editor-viewport
// style: the grabbed ground point stays under the cursor), scroll-wheel zoom,
// Q/E rotate. Also hosts the F5/F9 save-load debug stub. Render/UI side only —
// it reads the sim snapshot and never mutates sim state directly.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RTSCameraPawn.generated.h"

class USceneComponent;
class USpringArmComponent;
class UCameraComponent;

UCLASS()
class REALM_API ARTSCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ARTSCameraPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	virtual void BeginPlay() override;

	// --- Components ---
	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	// --- Tunables ---
	UPROPERTY(EditAnywhere, Category = "Camera|Pan")
	float PanSpeed = 2000.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Pan")
	float EdgeScrollMargin = 16.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Zoom")
	float ZoomStep = 200.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Zoom")
	float MinArmLength = 400.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Zoom")
	float MaxArmLength = 5000.f;

	UPROPERTY(EditAnywhere, Category = "Camera|Rotate")
	float RotateSpeed = 90.f;   // deg/sec for Q/E

private:
	// Input handlers
	void OnMoveForward(float Value);
	void OnMoveRight(float Value);
	void OnZoom(float Value);
	void OnRotateLeftPressed()  { bRotateLeft = true; }
	void OnRotateLeftReleased() { bRotateLeft = false; }
	void OnRotateRightPressed()  { bRotateRight = true; }
	void OnRotateRightReleased() { bRotateRight = false; }
	void OnDragPanPressed();
	void OnDragPanReleased();
	void OnSaveGame();
	void OnLoadGame();

	void ApplyPan(float DeltaSeconds);
	void ApplyEdgeScroll(float DeltaSeconds);
	void ApplyDragPan();
	void ApplyRotate(float DeltaSeconds);
	void ApplyZoom(float DeltaSeconds);

	// Cursor ray intersected with the Z=0 ground plane.
	bool DeprojectCursorToGround(FVector& OutGroundPos) const;

	// Pending input accumulated each frame
	float MoveForwardInput = 0.f;
	float MoveRightInput   = 0.f;
	float PendingZoom      = 0.f;

	bool bRotateLeft  = false;
	bool bRotateRight = false;

	// Middle-drag pan: ground point grabbed at drag start; ApplyDragPan keeps it
	// under the cursor (editor-viewport feel).
	bool    bDragPan   = false;
	FVector DragAnchor = FVector::ZeroVector;

	// Smoothed zoom target.
	float TargetArmLength = 2500.f;

	// Debug save slot.
	static const FString SaveSlotName;
	static const int32   SaveUserIndex = 0;
};
