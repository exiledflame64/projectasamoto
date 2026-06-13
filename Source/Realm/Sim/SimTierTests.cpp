// Copyright Asamoto.
// Unit tests for the population tier system (population_todos.md): tier-gated
// assignment, the upgrade/downgrade command flows with costs and refunds, the
// job invalidation sweep, and save round-tripping. FSimWorld is plain C++ —
// no world needed.
// Run: Automation RunTests Realm.Tiers

#include "Sim/SimWorld.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// A settlement with one of everything, spaced past the 320 cm minimum.
	struct FTierFixture
	{
		FSimWorld Sim;
		FBuildingId Warehouse, House, Lumberyard;
		FAgentId Villager;

		FTierFixture(int32 Planks, int32 Food)
		{
			Warehouse  = Sim.PlaceBuilding(EBuildingType::Warehouse,  FVector(0.0,    0.0, 0.0));
			House      = Sim.PlaceBuilding(EBuildingType::House,      FVector(1000.0, 0.0, 0.0));
			Lumberyard = Sim.PlaceBuilding(EBuildingType::Lumberyard, FVector(2000.0, 0.0, 0.0));
			Sim.AddResource(Warehouse, EResource::Plank, Planks);
			Sim.AddResource(Warehouse, EResource::Food,  Food);
			Villager = Sim.SpawnAgent(FVector(1120.0, 0.0, 0.0), House);
		}

		int32 Stock(EResource R) const
		{
			FSimSnapshot Snap;
			Sim.BuildSnapshot(Snap);
			return R == EResource::Plank ? Snap.PlankCount : Snap.FoodCount;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTierAssignmentGateTest,
	"Realm.Tiers.AssignmentGate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FTierAssignmentGateTest::RunTest(const FString& Parameters)
{
	FTierFixture F(20, 20);
	const FBuildingId Dojo = F.Sim.PlaceBuilding(EBuildingType::Dojo, FVector(3000.0, 0.0, 0.0));

	TestEqual(TEXT("spawns as Peasant"), F.Sim.GetAgentTier(F.Villager), ETier::Peasant);

	// A peasant may not work the dojo, but may work the lumberyard.
	TestFalse(TEXT("peasant rejected by dojo"), F.Sim.AssignWorkerTo(Dojo));
	TestTrue(TEXT("peasant accepted by lumberyard"), F.Sim.AssignWorkerTo(F.Lumberyard));
	TestTrue(TEXT("unassign works"), F.Sim.UnassignWorkerFrom(F.Lumberyard));

	// Idle pool splits per tier in the snapshot.
	FSimSnapshot Snap;
	F.Sim.BuildSnapshot(Snap);
	TestEqual(TEXT("idle peasant counted"),
		Snap.IdleByTier[static_cast<int32>(ETier::Peasant)], 1);
	TestEqual(TEXT("no idle samurai"),
		Snap.IdleByTier[static_cast<int32>(ETier::Samurai)], 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTierUpgradeChainTest,
	"Realm.Tiers.UpgradeChain",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FTierUpgradeChainTest::RunTest(const FString& Parameters)
{
	FTierFixture F(20, 20);
	const FBuildingId Dojo = F.Sim.PlaceBuilding(EBuildingType::Dojo, FVector(3000.0, 0.0, 0.0));
	const int32 Planks0 = F.Stock(EResource::Plank);
	const int32 Food0   = F.Stock(EResource::Food);

	// Edge-skipping and branch-crossing rejected by rule lookup.
	TestFalse(TEXT("Peasant->Samurai skips an edge"),
		F.Sim.UpgradeHouse(F.House, ETier::Samurai));
	TestFalse(TEXT("Peasant->WarriorMonk skips an edge"),
		F.Sim.UpgradeHouse(F.House, ETier::WarriorMonk));

	// Peasant -> Artisan (no required building), costs paid from stock.
	TestTrue(TEXT("Peasant->Artisan"), F.Sim.UpgradeHouse(F.House, ETier::Artisan));
	TestEqual(TEXT("agent tier follows the house"),
		F.Sim.GetAgentTier(F.Villager), ETier::Artisan);
	const int32 ArtisanPlankCost = Planks0 - F.Stock(EResource::Plank);
	TestTrue(TEXT("Artisan edge costs planks"), ArtisanPlankCost > 0);

	// Branch-crossing from Artisan rejected.
	TestFalse(TEXT("Artisan->Monk crosses branches"),
		F.Sim.UpgradeHouse(F.House, ETier::Monk));

	// Artisan -> Samurai requires the dojo: present, so it succeeds.
	TestTrue(TEXT("Artisan->Samurai with dojo"), F.Sim.UpgradeHouse(F.House, ETier::Samurai));
	TestTrue(TEXT("samurai accepted by dojo"), F.Sim.AssignWorkerTo(Dojo));

	// Downgrade one edge: refund restored, dojo vacated.
	TestTrue(TEXT("Samurai->Artisan downgrade"), F.Sim.DowngradeHouse(F.House));
	TestEqual(TEXT("agent back to Artisan"), F.Sim.GetAgentTier(F.Villager), ETier::Artisan);
	{
		FSimSnapshot Snap;
		F.Sim.BuildSnapshot(Snap);
		TestEqual(TEXT("ex-samurai vacated the dojo"), Snap.Buildings[Dojo].AssignedWorkers, 0);
		TestEqual(TEXT("ex-samurai is idle again"), Snap.IdleVillagers, 1);
	}

	// Second downgrade lands back at Peasant with the full stock restored
	// (DowngradeRefund = 1.0).
	TestTrue(TEXT("Artisan->Peasant downgrade"), F.Sim.DowngradeHouse(F.House));
	TestFalse(TEXT("Peasant cannot downgrade"), F.Sim.DowngradeHouse(F.House));
	TestEqual(TEXT("planks fully refunded"), F.Stock(EResource::Plank), Planks0);
	TestEqual(TEXT("food fully refunded"), F.Stock(EResource::Food), Food0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTierUpgradeRequirementsTest,
	"Realm.Tiers.UpgradeRequirements",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FTierUpgradeRequirementsTest::RunTest(const FString& Parameters)
{
	// Monk branch, starting WITHOUT a temple.
	FTierFixture F(20, 20);
	TestFalse(TEXT("Peasant->Monk blocked without temple"),
		F.Sim.UpgradeHouse(F.House, ETier::Monk));

	const FBuildingId Temple = F.Sim.PlaceBuilding(EBuildingType::Temple, FVector(3000.0, 0.0, 0.0));
	TestTrue(TEXT("Peasant->Monk with temple"), F.Sim.UpgradeHouse(F.House, ETier::Monk));
	TestTrue(TEXT("monk accepted by temple"), F.Sim.AssignWorkerTo(Temple));
	TestFalse(TEXT("monk rejected by lumberyard"), F.Sim.AssignWorkerTo(F.Lumberyard));

	// Pauper settlement: costs gate the edge.
	FTierFixture Poor(0, 0);
	TestFalse(TEXT("Peasant->Artisan blocked without planks"),
		Poor.Sim.UpgradeHouse(Poor.House, ETier::Artisan));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTierSaveRoundTripTest,
	"Realm.Tiers.SaveRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FTierSaveRoundTripTest::RunTest(const FString& Parameters)
{
	FTierFixture F(20, 20);
	F.Sim.PlaceBuilding(EBuildingType::Temple, FVector(3000.0, 0.0, 0.0));
	TestTrue(TEXT("Peasant->Monk"), F.Sim.UpgradeHouse(F.House, ETier::Monk));

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	F.Sim.Serialize(Writer);

	FSimWorld Loaded;
	FMemoryReader Reader(Bytes);
	Loaded.Serialize(Reader);

	TestEqual(TEXT("resident tier survives the round trip"),
		Loaded.GetAgentTier(F.Villager), ETier::Monk);
	FSimSnapshot Snap;
	Loaded.BuildSnapshot(Snap);
	TestEqual(TEXT("house snapshot tier"), Snap.Buildings[F.House].ResidentTier, ETier::Monk);
	return true;
}

// --- Building placement yaw (road_snapping_todos.md §3, §9.5) ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingYawSaveTest,
	"Realm.Sim.BuildingYawSave",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FBuildingYawSaveTest::RunTest(const FString& Parameters)
{
	FSimWorld Sim;
	const FBuildingId Yawed = Sim.PlaceBuilding(EBuildingType::House,
		FVector(0, 0, 0), FVector::ZeroVector, /*Yaw=*/47.5f);
	Sim.PlaceBuilding(EBuildingType::Lumberyard, FVector(1000, 0, 0));   // default yaw 0

	// Snapshot carries the yaw.
	FSimSnapshot Snap;
	Sim.BuildSnapshot(Snap);
	TestTrue(TEXT("yaw in snapshot"),
		FMath::IsNearlyEqual(Snap.Buildings[Yawed].YawDegrees, 47.5f, 0.01f));
	TestTrue(TEXT("default yaw zero"),
		FMath::IsNearlyEqual(Snap.Buildings[1].YawDegrees, 0.f, 0.01f));

	// Round-trips through serialize/load.
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Sim.Serialize(Writer);

	FSimWorld Loaded;
	FMemoryReader Reader(Bytes);
	Loaded.Serialize(Reader);

	FSimSnapshot After;
	Loaded.BuildSnapshot(After);
	TestTrue(TEXT("yaw survives save/load"),
		FMath::IsNearlyEqual(After.Buildings[Yawed].YawDegrees, 47.5f, 0.01f));

	// Footprint table: longer side runs along local Y (parallel to the road).
	const FVector2D Foot = BuildingFootprintHalfSize(EBuildingType::House);
	TestTrue(TEXT("footprint longer along Y"), Foot.Y >= Foot.X);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
