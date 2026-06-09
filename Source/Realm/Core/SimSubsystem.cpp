// Copyright Asamoto.

#include "SimSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

bool USimSubsystem::IsTickable() const
{
	// Only tick inside an actual game/PIE world, never the editor preview world.
	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return false;
	}
	const UWorld* World = GI->GetWorld();
	return World && (World->IsGameWorld());
}

void USimSubsystem::Tick(float DeltaTime)
{
	Accumulator += DeltaTime;
	// Clamp to avoid a spiral-of-death after a long hitch.
	Accumulator = FMath::Min(Accumulator, 0.5f);

	while (Accumulator >= FixedDt)
	{
		Sim.Tick(FixedDt);
		Accumulator -= FixedDt;
	}

	PublishSnapshot();

	// Phase 0 gate visibility: prove the sim advances at a stable 10 Hz regardless
	// of render fps. Logs once per simulated second (every 10 ticks).
	const int64 Now = Sim.GetTickNumber();
	if (Now / 10 != LastLoggedSecond)
	{
		LastLoggedSecond = Now / 10;
		UE_LOG(LogTemp, Log, TEXT("[RealmSim] tick=%lld (%.0f sim sec) renderDt=%.4f"),
			Now, static_cast<double>(Now) * FixedDt, DeltaTime);
	}
}

void USimSubsystem::PublishSnapshot()
{
	const int32 Write = 1 - ReadIndex.load();
	Sim.BuildSnapshot(Snapshots[Write]);
	ReadIndex.store(Write);   // atomic swap: readers now see the fresh buffer
}
