// Copyright Asamoto.

#include "BlueprintCatalog.h"

const TArray<FBlueprintDef>& GetBlueprintCatalog()
{
	static const TArray<FBlueprintDef> Catalog = {
		{ EBlueprintKind::Lumberyard, EBuildingType::Lumberyard,
			NSLOCTEXT("Realm", "BP_Lumberyard", "Lumberyard"), true },
		{ EBlueprintKind::Sawmill,    EBuildingType::Sawmill,
			NSLOCTEXT("Realm", "BP_Sawmill",    "Sawmill"),    true },
		{ EBlueprintKind::Farm,       EBuildingType::Farm,
			NSLOCTEXT("Realm", "BP_Farm",       "Farm"),       true },
	};
	return Catalog;
}

const FBlueprintDef* FindBlueprintDef(EBlueprintKind Kind)
{
	for (const FBlueprintDef& Def : GetBlueprintCatalog())
	{
		if (Def.Kind == Kind)
		{
			return &Def;
		}
	}
	return nullptr;
}
