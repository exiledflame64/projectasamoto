// Copyright Asamoto.

#include "BlueprintCatalog.h"

const TArray<FBlueprintDef>& GetBlueprintCatalog()
{
	static const TArray<FBlueprintDef> Catalog = {
		{ EBlueprintKind::Lumberyard, EBuildingType::Lumberyard,
			NSLOCTEXT("Realm", "BP_Lumberyard", "Lumberyard"),
			NSLOCTEXT("Realm", "TT_Lumberyard",
				"Sends villagers to chop trees.\nLogs are carried to the warehouse."),
			true },
		{ EBlueprintKind::Sawmill,    EBuildingType::Sawmill,
			NSLOCTEXT("Realm", "BP_Sawmill",    "Sawmill"),
			NSLOCTEXT("Realm", "TT_Sawmill",
				"Saws logs into planks.\nHaulers feed it logs from the warehouse and carry planks back."),
			true },
		{ EBlueprintKind::Farm,       EBuildingType::Farm,
			NSLOCTEXT("Realm", "BP_Farm",       "Farm"),
			NSLOCTEXT("Realm", "TT_Farm",
				"Grows food over time.\nHaulers stock it in the warehouse; villagers eat 1 food every 20 s."),
			true },
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
