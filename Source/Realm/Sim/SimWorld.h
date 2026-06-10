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

	// Phase 1 commands issued from the render/UI side (applied immediately on the
	// game thread for now; queue them if/when commands come off-thread).
	FBuildingId PlaceBuilding(EBuildingType Type, const FVector& Pos);
	FAgentId    SpawnAgent(const FVector& Pos);
	FTreeId     SpawnTree(const FVector& Pos);

	// Produce a read-only snapshot for the renderer.
	void BuildSnapshot(FSimSnapshot& Out) const;

	int64 GetTickNumber() const { return TickNumber; }
	void  SetTickNumber(int64 In) { TickNumber = In; }   // used by save/load stub

private:
	// Parallel arrays indexed by handle.
	TArray<FAgent>    Agents;
	TArray<FBuilding> Buildings;
	TArray<FTree>     Trees;
	int64 TickNumber = 0;

	// --- The 6 tick phases ---
	void Phase_Clock(float Dt);
	void Phase_Needs(float Dt);        // no-op until Phase 2
	void Phase_JobAssignment();
	void Phase_Production(float Dt);
	void Phase_Movement(float Dt);
	// Phase 6 (snapshot) is built on demand via BuildSnapshot().

	// --- Phase 1 helpers ---
	bool        HasBuilding(EBuildingType Type) const;
	FTreeId     FindNearestAvailableTree(const FVector& From) const;
	FBuildingId FindNearestStorage(const FVector& From) const;
};
