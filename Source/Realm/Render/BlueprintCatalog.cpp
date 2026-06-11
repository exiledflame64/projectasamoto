// Copyright Asamoto.

#include "BlueprintCatalog.h"

const TArray<FBlueprintDef>& GetBlueprintCatalog()
{
	static const TArray<FBlueprintDef> Catalog = {
		{ EBlueprintKind::House,      EBuildingType::House,
			NSLOCTEXT("Realm", "BP_House",      "House"),
			NSLOCTEXT("Realm", "TT_House",
				"Brings 1 villager to the settlement.\nNew villagers are idle until you assign them to a building."),
			true },
		{ EBlueprintKind::Warehouse,  EBuildingType::Warehouse,
			NSLOCTEXT("Realm", "BP_Warehouse",  "Warehouse"),
			NSLOCTEXT("Realm", "TT_Warehouse",
				"The settlement stockpile — all goods end up here and villagers eat from it.\nNeeds no workers. Limit: 1."),
			true },
		{ EBlueprintKind::Lumberyard, EBuildingType::Lumberyard,
			NSLOCTEXT("Realm", "BP_Lumberyard", "Lumberyard"),
			NSLOCTEXT("Realm", "TT_Lumberyard",
				"Assigned villagers chop trees and carry logs to the warehouse."),
			true },
		{ EBlueprintKind::Sawmill,    EBuildingType::Sawmill,
			NSLOCTEXT("Realm", "BP_Sawmill",    "Sawmill"),
			NSLOCTEXT("Realm", "TT_Sawmill",
				"Saws logs into planks.\nAssigned villagers feed it logs from the warehouse and carry planks back."),
			true },
		{ EBlueprintKind::Farm,       EBuildingType::Farm,
			NSLOCTEXT("Realm", "BP_Farm",       "Farm"),
			NSLOCTEXT("Realm", "TT_Farm",
				"Assigned villagers tend the attached field and carry harvested food to the warehouse.\nVillagers eat 1 food every 20 s."),
			true },
		// Not a building: arming this hands clicks to the road build tool.
		{ EBlueprintKind::Road,       EBuildingType::None,
			NSLOCTEXT("Realm", "BP_Road",       "Road"),
			NSLOCTEXT("Realm", "TT_Road",
				"Draw a road: click to place points (snaps to existing roads),\nCtrl+wheel curves the segment, Shift snaps angles,\nright-click undoes, Enter commits."),
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
