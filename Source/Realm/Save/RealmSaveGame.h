// Copyright Asamoto.
// Phase 2 save: the full FSimWorld serialized into a byte blob (the arrays are
// POD, so FSimWorld::Serialize writes them directly). TickCount kept as a
// human-readable sanity value for logs.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "RealmSaveGame.generated.h"

UCLASS()
class REALM_API URealmSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 TickCount = 0;

	UPROPERTY()
	TArray<uint8> SimBytes;
};
