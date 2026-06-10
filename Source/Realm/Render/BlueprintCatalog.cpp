// Copyright Asamoto.

#include "BlueprintCatalog.h"

const TArray<FBlueprintDef>& GetBlueprintCatalog()
{
	static const TArray<FBlueprintDef> Catalog = {
		{ EBlueprintKind::Building, NSLOCTEXT("Realm", "BP_Building", "Building"), true  },
		{ EBlueprintKind::Farm,     NSLOCTEXT("Realm", "BP_Farm",     "Farm"),     false },
	};
	return Catalog;
}
