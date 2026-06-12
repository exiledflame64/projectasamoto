// Copyright Asamoto.
// Vegetation appearance: trees today; bushes, rocks and other resource nodes
// join here as the sim grows. Edit the /Game/Realm/Vegetation/
// VegetationVisualSet asset; the C++ defaults (engine basic shapes) are the
// fallback when the asset doesn't exist. Editor seed previews and runtime
// proxies both read this, so editor == runtime.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Render/Visuals/RealmMeshDef.h"
#include "VegetationVisualSet.generated.h"

UCLASS(BlueprintType)
class REALM_API UVegetationVisualSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UVegetationVisualSet();

	UPROPERTY(EditAnywhere, Category = "Vegetation")
	FRealmMeshDef Tree;

	// The project's asset if it exists, else the class defaults.
	static const UVegetationVisualSet* Resolve();

	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
