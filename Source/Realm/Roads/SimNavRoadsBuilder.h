// Copyright Asamoto.
// USimNavRoadsBuilder (pathfinding.md §9): the one-way bridge from the road
// graph to the simulation. It subscribes to URoadNetworkSubsystem::OnNetworkChanged,
// bakes the whole FRoadGraph into a plain FSimNavRoads (the same resampled
// polylines the renderer consumes, so visuals == nav), and hands it to FSimWorld
// via SetNavRoads. The sim never references the road subsystem; this builder is
// the only thing that touches both sides.
//
// A full rebake on every change is intentional — the graph is tiny (player-drawn
// roads), so dirty-edge incrementality is out of scope (§9).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimNavRoadsBuilder.generated.h"

UCLASS()
class REALM_API USimNavRoadsBuilder : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// FTickableGameObject: only used to draw agent paths when the
	// realm.Nav.DrawAgentPaths CVar is on (dev tooling, §10). No sim mutation.
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(USimNavRoadsBuilder, STATGROUP_Tickables);
	}

private:
	// Rebakes the entire road graph and pushes it into the sim. The dirty list is
	// ignored (full rebuild); an empty graph bakes to an empty FSimNavRoads.
	void HandleNetworkChanged(const TArray<FGuid>& DirtyEdges);
	void Rebake();
};
