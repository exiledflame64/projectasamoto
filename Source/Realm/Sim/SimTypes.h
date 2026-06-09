// Copyright Asamoto.
// Simulation data types. Plain C++ structs, trivially copyable, NO UObjects.
// IDs are array indices/handles (never raw pointers) so snapshots and save data
// stay trivially copyable. See baoding.md "Core architecture".

#pragma once

#include "CoreMinimal.h"
#include "SimTypes.generated.h"   // only for the USTRUCT/UENUM used in snapshots/UI

// --- Handles (indices into arrays; never raw pointers in sim data) ---
using FAgentId    = int32;
using FBuildingId = int32;
using FTreeId     = int32;
static constexpr int32 INVALID_ID = INDEX_NONE;

// --- Enums (UENUM so they can appear in snapshots/UI later) ---
UENUM(BlueprintType)
enum class EAgentState : uint8
{
	Idle,
	MovingToWork,
	Working,
	MovingToStore
};

UENUM(BlueprintType)
enum class EBuildingType : uint8
{
	None,
	WoodcutterHut,
	Storage   // Phase 1 set; extend later
};

UENUM(BlueprintType)
enum class EResource : uint8
{
	None,
	Log   // Phase 1; Planks/Food come in Phase 2
};

// --- Sim data: plain structs, trivially copyable, NO UObjects ---
struct FAgent
{
	FVector     Position      = FVector::ZeroVector;
	FVector     Target        = FVector::ZeroVector;
	float       Speed         = 200.f;
	EAgentState State         = EAgentState::Idle;
	FBuildingId AssignedJob   = INVALID_ID;  // building emitting the claimed job
	FTreeId     TargetTree     = INVALID_ID;  // tree being walked to / chopped
	int32       CarriedAmount = 0;
	EResource   CarriedType   = EResource::None;
};

struct FBuilding
{
	EBuildingType Type            = EBuildingType::None;
	FVector       Position        = FVector::ZeroVector;
	int32         Inventory       = 0;   // single slot for Phase 1
	int32         WorkerSlots     = 1;
	int32         AssignedWorkers = 0;
	float         WorkTimer       = 0.f; // production progress
	bool          bHasOpenJob     = false;
};

// Phase 1 "tree": minimal resource node — a target position and a yield.
struct FTree
{
	FVector Position  = FVector::ZeroVector;
	int32   Remaining = 1;       // logs left to chop
	bool    bClaimed  = false;   // an agent is walking to / chopping this
};

// --- Render snapshot: what the visual layer is allowed to read ---
USTRUCT(BlueprintType)
struct FAgentSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sim")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Sim")
	EAgentState State = EAgentState::Idle;
};

// Render-side, read-only copy of sim state for one frame. Plain struct (copied
// across the double buffer); the inner element is a USTRUCT for Blueprint/UI use.
struct FSimSnapshot
{
	TArray<FAgentSnapshot> Agents;
	int32 StorageLogCount = 0;
	int64 TickNumber      = 0;
};
