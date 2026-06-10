// Copyright Asamoto.
// FSimWorld: owns the simulation state and runs the fixed-timestep tick.
// Engine-render-free so it stays unit-testable and serializable. May use
// FVector/TArray (lightweight core types) but must NOT hold AActor*/UObject*.

#pragma once

#include "CoreMinimal.h"
#include "SimTypes.h"

class REALM_API FSimWorld
{
public:
	// Fixed-timestep entry point, called at 10 Hz by the subsystem.
	void Tick(float FixedDt);

	// Commands issued from the render/UI side (applied immediately on the
	// game thread for now; queue them if/when commands come off-thread).
	// PlaceBuilding refuses invalid spots (returns INVALID_ID). Validation is
	// footprint-aware: a farm also occupies its attached field plot.
	// VisualScale is cosmetic passthrough for the render proxy (zero = visual
	// set default); editor-scaled seeds feed it.
	bool        CanPlaceBuilding(EBuildingType Type, const FVector& Pos) const;
	FBuildingId PlaceBuilding(EBuildingType Type, const FVector& Pos,
		const FVector& VisualScale = FVector::ZeroVector);
	FAgentId    SpawnAgent(const FVector& Pos);
	FTreeId     SpawnTree(const FVector& Pos,
		const FVector& VisualScale = FVector::ZeroVector);
	void        AddResource(FBuildingId Building, EResource Resource, int32 Amount);

	// Workforce: a villager works exactly one resource building, sticky until
	// unassigned. Assign pulls from the idle pool; unassign aborts the current
	// errand (claims released, carried goods lost) and returns them to idle.
	bool AssignWorkerTo(FBuildingId Building);
	bool UnassignWorkerFrom(FBuildingId Building);
	static int32 MaxWorkersFor(EBuildingType Type);

	// Produce a read-only snapshot for the renderer.
	void BuildSnapshot(FSimSnapshot& Out) const;

	// Save/load: the arrays are POD, so they serialize directly
	// (baoding.md: "that's the payoff of keeping them POD").
	void Serialize(FArchive& Ar);

	int64 GetTickNumber() const { return TickNumber; }
	void  SetTickNumber(int64 In) { TickNumber = In; }

private:
	// Parallel arrays indexed by handle.
	TArray<FAgent>    Agents;
	TArray<FBuilding> Buildings;
	TArray<FTree>     Trees;
	int64 TickNumber = 0;

	bool bEverHadAgents = false;   // lose state only arms once someone lived
	bool bGameOver      = false;

	// --- The 6 tick phases ---
	void Phase_Clock(float Dt);
	void Phase_Needs(float Dt);        // food consumption + starvation
	void Phase_JobAssignment();
	void Phase_Production(float Dt);
	void Phase_Movement(float Dt);
	// Phase 6 (snapshot) is built on demand via BuildSnapshot().

	// --- Per-building-type work loops (agent is assigned to that building) ---
	void StartChop(FAgent& A);                          // lumberyard
	void StartFieldWork(FAgent& A, const FBuilding& Farm);
	void StartSawmillWork(FAgent& A, FBuildingId MillId);
	void AbortJob(FAgent& A);          // release claims, drop carried, back to Idle
	void FinishJob(FAgent& A);         // reset job fields back to Idle
	void KillAgent(FAgent& A);         // starvation: release everything, mark Dead

	// --- Needs ---
	bool ConsumeFromWarehouses(EResource Resource, int32 Amount);

	// --- Queries ---
	bool        HasBuilding(EBuildingType Type) const;
	FTreeId     FindNearestAvailableTree(const FVector& From) const;
	FBuildingId FindNearestWarehouse(const FVector& From) const;
	FBuildingId FindNearestWarehouseWith(const FVector& From, EResource Resource) const;
};
