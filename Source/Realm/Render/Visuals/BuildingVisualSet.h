// Copyright Asamoto.
// Building appearance per EBuildingType, plus the farm's field plot (a
// sub-building, so it lives under Buildings). Edit the
// /Game/Realm/Buildings/BuildingVisualSet asset; the C++ defaults (engine
// basic shapes) are the fallback when the asset doesn't exist.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Render/Visuals/RealmMeshDef.h"
#include "Sim/SimTypes.h"
#include "BuildingVisualSet.generated.h"

UCLASS(BlueprintType)
class REALM_API UBuildingVisualSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UBuildingVisualSet();

	UPROPERTY(EditAnywhere, Category = "Buildings")
	TMap<EBuildingType, FRealmMeshDef> Buildings;

	// The farm's attached field — a sub-building of the farm.
	UPROPERTY(EditAnywhere, Category = "Buildings|Field Plot")
	FRealmMeshDef FieldPlot;

	const FRealmMeshDef& BuildingDef(EBuildingType Type) const;

	// The project's asset if it exists, else the class defaults.
	static const UBuildingVisualSet* Resolve();

	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
