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
	MovingToWork,     // walking to a tree to chop
	Working,          // chopping
	MovingToStore,    // carrying a chopped log to the warehouse
	MovingToPickup,   // Phase 2: walking to collect a hauled resource
	MovingToDeliver,  // Phase 2: carrying a hauled resource to its destination
	Dead              // Phase 2: starved; ignored by all systems, hidden by render
};

UENUM(BlueprintType)
enum class EBuildingType : uint8
{
	None,
	Lumberyard,
	Warehouse,   // settlement stockpile; villagers are fed from here
	Sawmill,     // Phase 2: consumes logs, produces planks
	Farm         // Phase 2: produces food
};

UENUM(BlueprintType)
enum class EResource : uint8
{
	None,
	Log,
	Plank,   // Phase 2
	Food,    // Phase 2
	MAX UMETA(Hidden)
};
static constexpr int32 NumResources = static_cast<int32>(EResource::MAX);

// Phase 2: what an agent intends to do when it reaches its destination.
enum class EJobKind : uint8
{
	None,
	Chop,         // lumberyard job: chop a tree, carry the log to the warehouse
	HaulOutput,   // collect a producer's output, deliver it to the warehouse
	FeedSawmill   // take a log from the warehouse, deliver it to a sawmill's input
};

// --- Sim data: plain structs, trivially copyable, NO UObjects ---
struct FAgent
{
	FVector     Position      = FVector::ZeroVector;
	FVector     Target        = FVector::ZeroVector;
	float       Speed         = 200.f;
	EAgentState State         = EAgentState::Idle;
	EJobKind    Job           = EJobKind::None;
	FBuildingId PickupFrom    = INVALID_ID;  // hauling: source building
	FBuildingId DeliverTo     = INVALID_ID;  // hauling: destination building
	FTreeId     TargetTree    = INVALID_ID;  // tree being walked to / chopped
	float       WorkTimer     = 0.f;         // chopping progress while Working
	int32       CarriedAmount = 0;
	EResource   CarriedType   = EResource::None;

	// Food need: abstract, no walking. Every EatInterval the agent consumes one
	// food straight from warehouse stock; going unfed runs the starvation clock.
	float EatTimer    = 0.f;   // seconds since the last meal
	float StarveTimer = 0.f;   // seconds overdue without food
};

struct FBuilding
{
	EBuildingType Type     = EBuildingType::None;
	FVector       Position = FVector::ZeroVector;

	// Per-resource inventory, indexed by EResource. The warehouse uses it as
	// the settlement stockpile; producers use their own slots as input (sawmill
	// logs) and output (sawmill planks, farm food) buffers.
	int32 Stored[NumResources] = {};

	float WorkTimer = 0.f;   // production progress (sawmill/farm)

	// Hauling claims: one hauler at a time per direction keeps agents from
	// dog-piling the same errand (released on pickup/delivery/death).
	bool bInputClaimed  = false;
	bool bOutputClaimed = false;
};

// Phase 1 "tree": minimal resource node — a target position and a yield.
struct FTree
{
	FVector Position  = FVector::ZeroVector;
	int32   Remaining = 5;       // logs left to chop
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

	UPROPERTY(BlueprintReadOnly, Category = "Sim")
	int32 CarriedAmount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Sim")
	EResource CarriedType = EResource::None;

	// True while the agent is overdue for a meal the warehouse couldn't provide.
	UPROPERTY(BlueprintReadOnly, Category = "Sim")
	bool bStarving = false;
};

// Read-only copies of buildings/trees so the render layer can mirror the world
// without touching sim data directly (hard rule #2).
struct FBuildingSnapshot
{
	FVector       Position = FVector::ZeroVector;
	EBuildingType Type     = EBuildingType::None;
};

struct FTreeSnapshot
{
	FVector Position  = FVector::ZeroVector;
	int32   Remaining = 0;
};

// Render-side, read-only copy of sim state for one frame. Plain struct (copied
// across the double buffer); the inner element is a USTRUCT for Blueprint/UI use.
struct FSimSnapshot
{
	TArray<FAgentSnapshot>    Agents;
	TArray<FBuildingSnapshot> Buildings;
	TArray<FTreeSnapshot>     Trees;

	// Settlement totals (summed over Warehouse buildings) + population, for the UI.
	int32 LogCount   = 0;
	int32 PlankCount = 0;
	int32 FoodCount  = 0;
	int32 Population = 0;     // alive agents
	bool  bGameOver  = false; // everyone starved

	int64 TickNumber = 0;
};
