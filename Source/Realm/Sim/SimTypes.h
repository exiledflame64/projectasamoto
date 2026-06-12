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
	House,       // grants 1 villager when placed; carries the resident tier
	Temple,      // monk workplace (Monk / WarriorMonk only)
	Dojo         // samurai workplace (Samurai only)
};

// --- Population tiers (Sengoku ladder; see population_todos.md) ---
// Two ladders sharing a root: Peasant -> Artisan -> Samurai and
// Peasant -> Monk -> WarriorMonk. The graph is a TREE (one incoming edge per
// tier) — downgrade resolution and snapshot edge listing rely on that.
UENUM(BlueprintType)
enum class ETier : uint8
{
	Peasant     = 0,
	Artisan     = 1,
	Samurai     = 2,
	Monk        = 3,
	WarriorMonk = 4,

	COUNT       UMETA(Hidden)
};
static constexpr int32 NumTiers = static_cast<int32>(ETier::COUNT);

constexpr uint8 TierBit(ETier T) { return uint8(1) << uint8(T); }

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
	FeedSawmill,  // sawmill: take a log from the warehouse, deliver to its input
	Attend        // temple/dojo: walk there and "work" (no production yet)
};

// --- House tier promotion rules (data, not code) ---
struct FResourceCost
{
	EResource Resource = EResource::None;
	int32     Amount   = 0;
};

struct FHouseUpgradeRule
{
	ETier         From = ETier::Peasant;
	ETier         To   = ETier::Peasant;
	// Must EXIST anywhere in the settlement (global existence, no proximity);
	// EBuildingType::None = no building requirement.
	EBuildingType RequiredBuilding = EBuildingType::None;
	FResourceCost Costs[4];   // unused entries Amount = 0
};

// Validation is a sequence of independent checks, each with its own fail
// reason, so future requirement kinds append cleanly.
enum class EUpgradeFail : uint8
{
	None,
	NoRule,              // no edge from the house's current tier to the target
	MissingBuilding,     // RequiredBuilding not present in the settlement
	NotEnoughResources
};

// Tier ladder edges + costs. Tuning lives in SimWorld.cpp with the other sim
// constants; the UI reads this for cost/requirement tooltips.
REALM_API const TArray<FHouseUpgradeRule>& GetHouseUpgradeRules();

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

	// The House that spawned this agent. The agent's tier is DERIVED from that
	// house's ResidentTier (INVALID_ID falls back to Peasant).
	FBuildingId HomeBuilding = INVALID_ID;

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

	// Population tier of the residents. Meaningful only for House; ignored
	// otherwise (kept directly on FBuilding to preserve the flat POD layout).
	ETier ResidentTier = ETier::Peasant;

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

	// Derived from the home house's ResidentTier (Peasant when homeless).
	UPROPERTY(BlueprintReadOnly, Category = "Sim")
	ETier Tier = ETier::Peasant;
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

	// Tier gate of this workplace (TierBit mask; 0 = takes no workers).
	uint8 AllowedTiers = 0;

	// House-only tier state for the UI. A Peasant house has TWO outgoing
	// edges (Artisan and Monk); each carries its own fail reason.
	ETier        ResidentTier    = ETier::Peasant;
	int32        NumUpgradeEdges = 0;
	ETier        UpgradeTo[2]    = { ETier::Peasant, ETier::Peasant };
	EUpgradeFail UpgradeFail[2]  = { EUpgradeFail::NoRule, EUpgradeFail::NoRule };
	bool         bCanDowngrade   = false;
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
	int32 IdleByTier[NumTiers] = {};   // idle pool split per tier (gates the [+] buttons)
	bool  bGameOver     = false;

	int64 TickNumber = 0;
};
