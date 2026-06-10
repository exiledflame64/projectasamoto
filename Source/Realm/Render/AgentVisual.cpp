// Copyright Asamoto.

#include "AgentVisual.h"
#include "RealmVisualSet.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

AAgentVisual::AAgentVisual()
{
	PrimaryActorTick.bCanEverTick = false;   // the visualizer drives updates

	// Root scene component keeps the actor origin on the sim ground position;
	// the mesh hangs off it with whatever the visual set prescribes.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(SceneRoot);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(40.f);
	Label->SetText(FText::FromString(TEXT("idle")));
	Label->SetTextRenderColor(FColor::White);
}

void AAgentVisual::BeginPlay()
{
	Super::BeginPlay();

	// Appearance from the shared visual set (asset or C++ defaults).
	const URealmVisualSet* Set = URealmVisualSet::Resolve();
	const FRealmMeshDef& Def = Set->Villager;
	Def.ApplyTo(Mesh, this);
	Mesh->SetRelativeLocation(FVector(0.f, 0.f, Def.GroundLift()));
	MeshMID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));

	// Label floats just above whatever mesh the set prescribes.
	if (const UStaticMesh* SM = Mesh->GetStaticMesh())
	{
		const float TopZ = Def.GroundLift() + SM->GetBoundingBox().Max.Z * Def.Scale.Z;
		Label->SetRelativeLocation(FVector(0.f, 0.f, TopZ + 60.f));
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
	// Generic labels: the same states serve every gathering job (trees today,
	// fields, stone, iron later).
	case EAgentState::MovingToWork:    StateText = TEXT("walking to workplace"); Color = FLinearColor(1.f, 0.85f, 0.1f); break;
	case EAgentState::Working:         StateText = TEXT("working");             Color = FLinearColor(0.9f, 0.3f, 0.1f); break;
	case EAgentState::MovingToStore:   StateText = TEXT("returning");       Color = FLinearColor(0.2f, 0.8f, 0.3f);  break;
	case EAgentState::MovingToPickup:  StateText = TEXT("fetching");        Color = FLinearColor(0.3f, 0.6f, 0.9f);  break;
	case EAgentState::MovingToDeliver: StateText = TEXT("hauling");         Color = FLinearColor(0.2f, 0.45f, 0.95f); break;
	case EAgentState::Idle:
	default:
		StateText = Snap.bAssigned ? TEXT("idle") : TEXT("unemployed");
		Color = Snap.bAssigned ? FLinearColor(0.6f, 0.6f, 0.6f) : FLinearColor(0.75f, 0.75f, 0.5f);
		break;
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
