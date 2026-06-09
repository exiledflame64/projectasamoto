// Copyright Asamoto.
// USimSubsystem owns the FSimWorld, drives a fixed 10 Hz accumulator from its
// own tick, and publishes a double-buffered snapshot for the render/UI side.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Sim/SimWorld.h"
#include <atomic>
#include "SimSubsystem.generated.h"

UCLASS()
class REALM_API USimSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(USimSubsystem, STATGROUP_Tickables);
	}
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// Render/UI reads this (call on the game thread).
	const FSimSnapshot& GetSnapshot() const { return Snapshots[ReadIndex.load()]; }

	FSimWorld& GetSim() { return Sim; }

private:
	FSimWorld Sim;

	static constexpr float FixedDt = 0.1f;   // 10 Hz
	float Accumulator = 0.f;
	int64 LastLoggedSecond = -1;             // throttles the Phase 0 tick log

	// Double-buffered snapshot: sim writes the back buffer, then swaps the atomic
	// read index. No locks, no stalls (see baoding.md hard rule #4).
	FSimSnapshot Snapshots[2];
	std::atomic<int32> ReadIndex{ 0 };

	void PublishSnapshot();
};
