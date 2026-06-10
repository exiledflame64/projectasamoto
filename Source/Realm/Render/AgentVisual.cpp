// Copyright Asamoto.

#include "AgentVisual.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AAgentVisual::AAgentVisual()
{
	PrimaryActorTick.bCanEverTick = false;   // the visualizer drives updates

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Villager ~50x50x100 cm. Basic cube is 100 cm; lift it half a unit so it
	// sits on the ground while the actor origin stays on the sim ground position.
	Mesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.0f));
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, 50.f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CubeFinder.Object);
	}

	// BasicShapeMaterial exposes a "Color" vector param (the default cube material
	// does not), so base the tintable dynamic material on it.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MatFinder.Succeeded())
	{
		Mesh->SetMaterial(0, MatFinder.Object);
	}

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Mesh);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(40.f);
	Label->SetText(FText::FromString(TEXT("idle")));
	Label->SetTextRenderColor(FColor::White);
}

void AAgentVisual::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInterface* Base = Mesh->GetMaterial(0))
	{
		MeshMID = UMaterialInstanceDynamic::Create(Base, this);
		Mesh->SetMaterial(0, MeshMID);
	}
}

void AAgentVisual::UpdateVisual(const FAgentSnapshot& Snap, const FVector& CameraLocation)
{
	// Starved agents simply vanish (Phase 2; a corpse/grave visual can come later).
	if (Snap.State == EAgentState::Dead)
	{
		SetActorHiddenInGame(true);
		return;
	}
	SetActorHiddenInGame(false);
	SetActorLocation(Snap.Position);

	FString StateText;
	FLinearColor Color;
	switch (Snap.State)
	{
	case EAgentState::MovingToWork:    StateText = TEXT("walking to tree"); Color = FLinearColor(1.f, 0.85f, 0.1f);  break;
	case EAgentState::Working:         StateText = TEXT("chopping");        Color = FLinearColor(0.9f, 0.3f, 0.1f);  break;
	case EAgentState::MovingToStore:   StateText = TEXT("returning");       Color = FLinearColor(0.2f, 0.8f, 0.3f);  break;
	case EAgentState::MovingToPickup:  StateText = TEXT("fetching");        Color = FLinearColor(0.3f, 0.6f, 0.9f);  break;
	case EAgentState::MovingToDeliver: StateText = TEXT("hauling");         Color = FLinearColor(0.2f, 0.45f, 0.95f); break;
	case EAgentState::Idle:
	default:                           StateText = TEXT("idle");            Color = FLinearColor(0.6f, 0.6f, 0.6f);  break;
	}

	if (Snap.CarriedAmount > 0)
	{
		const TCHAR* Res = TEXT("log");
		switch (Snap.CarriedType)
		{
		case EResource::Plank: Res = TEXT("plank"); break;
		case EResource::Food:  Res = TEXT("food");  break;
		default: break;
		}
		StateText += FString::Printf(TEXT(" [%s x%d]"), Res, Snap.CarriedAmount);
	}

	if (Snap.bStarving)
	{
		StateText += TEXT(" (STARVING)");
	}

	Label->SetText(FText::FromString(StateText));
	if (MeshMID)
	{
		MeshMID->SetVectorParameterValue(TEXT("Color"), Color);
	}

	// Billboard the label toward the camera so the text stays readable. Text
	// renders along the component's +X axis, so +X must point AT the camera —
	// pointing it away shows the glyphs from behind (mirrored).
	const FVector LabelLoc = Label->GetComponentLocation();
	const FRotator Face = (CameraLocation - LabelLoc).Rotation();
	Label->SetWorldRotation(Face);
}
