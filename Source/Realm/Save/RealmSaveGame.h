// Copyright Asamoto.
// Phase 0 save stub: stores ONE dummy value so every later system plugs into a
// working serialization pattern (see baoding.md "Don't defer save/load").

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "RealmSaveGame.generated.h"

UCLASS()
class REALM_API URealmSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// Phase 0: round-trips the sim tick count. Phase 2+ serializes the FSimWorld
	// arrays directly (the payoff of keeping sim data POD).
	UPROPERTY()
	int32 TickCount = 0;
};
