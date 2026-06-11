// Copyright Asamoto.
// Terrain height abstraction (road_todos.md Phase 0.6). Consumers (road graph,
// renderer, build tool) NEVER touch the terrain actor directly — they go
// through UTerrainHeightSubsystem. When the flat ground plane becomes a
// Landscape later, only the registered provider implementation may change.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TerrainHeight.generated.h"

// The dedicated ground trace channel ("Terrain" in DefaultEngine.ini): only
// terrain blocks it, so height/placement traces never hit buildings or agents.
static constexpr ECollisionChannel RealmTerrainTraceChannel = ECC_GameTraceChannel1;

// Pure interface so a heightmap-backed implementation (Implementation A) can
// replace the default trace fallback without touching any consumer.
class REALM_API ITerrainHeightProvider
{
public:
	virtual ~ITerrainHeightProvider() = default;
	virtual bool GetHeightAt(const FVector2D& WorldXY, float& OutZ) const = 0;
	virtual bool GetNormalAt(const FVector2D& WorldXY, FVector& OutNormal) const = 0;
};

UCLASS()
class REALM_API UTerrainHeightSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// Implementation A hook: a future heightmap-backed provider registers here
	// and replaces the trace fallback.
	void RegisterProvider(TSharedPtr<ITerrainHeightProvider> InProvider) { Provider = InProvider; }

	bool GetHeightAt(const FVector2D& WorldXY, float& OutZ) const;
	bool GetNormalAt(const FVector2D& WorldXY, FVector& OutNormal) const;

private:
	TSharedPtr<ITerrainHeightProvider> Provider;

	// Implementation B fallback: synchronous line trace on the Terrain channel.
	// Results are cached on a 10 cm grid for the current frame only, so a drag
	// frame that resamples the same polyline repeatedly pays one trace per spot.
	bool TraceSample(const FVector2D& WorldXY, float& OutZ, FVector& OutNormal) const;

	struct FCachedSample
	{
		float   Z = 0.f;
		FVector Normal = FVector::UpVector;
		bool    bHit = false;
	};
	mutable TMap<FIntPoint, FCachedSample> FrameCache;
	mutable uint64 CacheFrame = 0;
};
