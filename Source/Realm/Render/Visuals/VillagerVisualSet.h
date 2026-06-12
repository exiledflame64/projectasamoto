// Copyright Asamoto.
// Villager appearance (job/class variants can join later). Edit the
// /Game/Realm/Villagers/VillagerVisualSet asset; the C++ defaults (engine
// basic shapes) are the fallback when the asset doesn't exist.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Render/Visuals/RealmMeshDef.h"
#include "VillagerVisualSet.generated.h"

UCLASS(BlueprintType)
class REALM_API UVillagerVisualSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UVillagerVisualSet();

	UPROPERTY(EditAnywhere, Category = "Villagers")
	FRealmMeshDef Villager;

	// The project's asset if it exists, else the class defaults.
	static const UVillagerVisualSet* Resolve();

	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
