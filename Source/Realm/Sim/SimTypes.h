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
	MovingToWork,     // walking to a workspace to gather goods
	Working,          // chopping / tending the field, etc
	MovingToStore,    // carrying a good to the warehouse
	MovingToPickup,   // walking to collect a hauled resource
	MovingToDeliver,  // carrying a hauled resource to its destination
	Dead              // starved; ignored by all systems, hidden by render
};

UENUM(BlueprintType)
enum class EBuildingType : uint8
{
	None,
	Lumberyard,
	Warehouse,   // settlement stockpile; villagers are fed from here. Max 1. No workers.
	Sawmill,     // consumes logs, produces planks
	Farm,        // workers tend the attached field, harvest food
	House        // grants 1 villager when placed
};

UENUM(BlueprintType)
enum class EResource : uint8
{
	None,
	Log,
	Plank,
	Food,
	MAX UMETA(Hidden)
};
static constexpr int32 NumResources = static_cast<int32>(EResource::MAX);

// What an agent intends to do when it reaches its destination. Jobs are only
// ever emitted by the building the agent is assigned to.
enum class EJobKind : uint8
{
	None,
	Chop,         // lumberyard: chop a tree, carry the log to the warehouse
	WorkField,    // farm: tend a spot in the attached field, harvest food
	HaulOutput,   // sawmill: collect finished planks, deliver to the warehouse
	FeedSawmill   // sawmill: take a log from the warehouse, deliver to its input
};

// Farm field (the "sub-building" plot attached to every farm). Shared by the
// sim (work-spot positions) and the render layer (field visual).
static constexpr float FarmFieldOffset   = 380.f;   // field centre, +X from the farm
static constexpr float FarmFieldHalfSize = 220.f;   // square half-extent

// --- Sim data: plain structs, trivially copyable, NO UObjects ---
struct FAgent
{
	FVector     Position      = FVector::ZeroVector;
	FVector     Target        = FVector::ZeroVector;
	float       Speed         = 200.f;
	EAgentState State         = EAgentState::Idle;
	EJobKind    Job           = EJobKind::None;

	// Workforce: the resource building this villager is assigned to (player
	// decision, sticky until unassigned). Unassigned villagers stay idle.
	FBuildingId AssignedBuilding = INVALID_ID;

	FBuildingId PickupFrom    = INVALID_ID;  // hauling: source building
	FBuildingId DeliverTo     = INVALID_ID;  // hauling: destination building
	FTreeId     TargetTree    = INVALID_ID;  // tree being walked to / chopped
	float       WorkTimer     = 0.f;         // progress while Working
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

	// Cosmetic only — carried from editor-scaled seeds to the render proxy.
	// Zero means "use the visual set's default scale". Sim logic ignores it.
	FVector VisualScale = FVector::ZeroVector;

	// Per-resource inventory, indexed by EResource. The warehouse uses it as
	// the settlement stockpile; the sawmill as input (logs) / output (planks).
	int32 Stored[NumResources] = {};

	float WorkTimer       = 0.f;   // production progress (planks,steel,etc)
	int32 AssignedWorkers = 0;     // mirror of agents assigned here

	// Hauling claims: one hauler at a time per direction keeps agents from
	// dog-piling the same errand (released on pickup/delivery/death).
	bool bInputClaimed  = false;
	bool bOutputClaimed = false;
};

// "Tree": minimal resource node — a target position and a yield.
struct FTree
{
	FVector Position  = FVector::ZeroVector;
	int32   Remaining = 5;       // logs left to chop
	bool    bClaimed  = false;   // an agent is walking to / chopping this

	// Cosmetic only (see FBuilding::VisualScale).
	FVector VisualScale = FVector::ZeroVector;
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

	// Assigned to a resource building (unassigned idle = "unemployed").
	UPROPERTY(BlueprintReadOnly, Category = "Sim")
	bool bAssigned = false;

	// True while the agent is overdue for a meal the warehouse couldn't provide.
	UPROPERTY(BlueprintReadOnly, Category = "Sim")
	bool bStarving = false;
};

// Read-only copies of buildings/trees so the render layer can mirror the world
// without touching sim data directly (hard rule #2).
struct FBuildingSnapshot
{
	FVector       Position        = FVector::ZeroVector;
	EBuildingType Type            = EBuildingType::None;
	int32         AssignedWorkers = 0;
	int32         MaxWorkers      = 0;
	FVector       VisualScale     = FVector::ZeroVector;   // zero = visual set default
};

struct FTreeSnapshot
{
	FVector Position    = FVector::ZeroVector;
	int32   Remaining   = 0;
	FVector VisualScale = FVector::ZeroVector;   // zero = visual set default
};

// Render-side, read-only copy of sim state for one frame. Plain struct (copied
// across the double buffer); the inner element is a USTRUCT for Blueprint/UI use.
struct FSimSnapshot
{
	TArray<FAgentSnapshot>    Agents;
	TArray<FBuildingSnapshot> Buildings;
	TArray<FTreeSnapshot>     Trees;

	// Settlement totals (summed over Warehouse buildings) + population, for the UI.
	int32 LogCount      = 0;
	int32 PlankCount    = 0;
	int32 FoodCount     = 0;
	int32 Population    = 0;   // alive agents
	int32 IdleVillagers = 0;   // alive, not assigned to any building
	bool  bGameOver     = false;

	int64 TickNumber = 0;
};
