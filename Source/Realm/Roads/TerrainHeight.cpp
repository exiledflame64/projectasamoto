// Copyright Asamoto.

#include "Roads/TerrainHeight.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

bool UTerrainHeightSubsystem::GetHeightAt(const FVector2D& WorldXY, float& OutZ) const
{
	if (Provider.IsValid())
	{
		return Provider->GetHeightAt(WorldXY, OutZ);
	}
	FVector Unused;
	return TraceSample(WorldXY, OutZ, Unused);
}

bool UTerrainHeightSubsystem::GetNormalAt(const FVector2D& WorldXY, FVector& OutNormal) const
{
	if (Provider.IsValid())
	{
		return Provider->GetNormalAt(WorldXY, OutNormal);
	}
	float Unused;
	return TraceSample(WorldXY, Unused, OutNormal);
}

bool UTerrainHeightSubsystem::TraceSample(const FVector2D& WorldXY, float& OutZ, FVector& OutNormal) const
{
	if (CacheFrame != GFrameCounter)
	{
		FrameCache.Reset();
		CacheFrame = GFrameCounter;
	}

	const FIntPoint Key(FMath::RoundToInt(WorldXY.X * 0.1f), FMath::RoundToInt(WorldXY.Y * 0.1f));
	if (const FCachedSample* Cached = FrameCache.Find(Key))
	{
		OutZ = Cached->Z;
		OutNormal = Cached->Normal;
		return Cached->bHit;
	}

	FCachedSample Sample;
	if (const UWorld* World = GetWorld())
	{
		// Generous vertical range: covers any terrain height the project will
		// see long before a Landscape replaces the flat plane.
		const FVector Start(WorldXY.X, WorldXY.Y, 100000.f);
		const FVector End(WorldXY.X, WorldXY.Y, -100000.f);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TerrainHeight), /*bTraceComplex=*/false);
		if (World->LineTraceSingleByChannel(Hit, Start, End, RealmTerrainTraceChannel, Params)
			&& Hit.bBlockingHit)
		{
			Sample.bHit = true;
			Sample.Z = Hit.Location.Z;
			Sample.Normal = Hit.Normal.GetSafeNormal();
		}
	}

	FrameCache.Add(Key, Sample);
	OutZ = Sample.Z;
	OutNormal = Sample.Normal;
	return Sample.bHit;
}
