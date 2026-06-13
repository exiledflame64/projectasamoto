// Copyright Asamoto.
// FSimWorld tick wiring. Economy with a player-directed workforce: villagers
// arrive via houses, idle until assigned to a resource building, then run that
// building's work loop until unassigned (sticky assignment, one building each).

#include "SimWorld.h"
#include "RoadPathfinder.h"

namespace
{
	// Tuning.
	// Version-bump replan amortization: at most this many walking agents replan
	// per tick when a new road network is swapped in (§7). Brand-new movement
	// legs always plan immediately and are not counted against this budget.
	constexpr int32 MaxReplansPerTick = 16;
	constexpr float ChopDuration       = 1.5f;   // seconds of "chopping" per log
	constexpr float SawDuration        = 3.0f;   // seconds per log -> plank
	constexpr float FieldWorkDuration  = 4.0f;   // seconds tending per harvest
	constexpr int32 FarmYield          = 2;      // food per harvest carried off the field
	constexpr int32 SawmillInputCap    = 5;      // logs buffered at the sawmill
	constexpr int32 SawmillOutputCap   = 5;      // planks buffered before production stalls
	constexpr float EatIntervalSeconds = 20.f;   // each villager eats 1 food this often
	constexpr float StarveSeconds      = 60.f;   // unfed past a due meal this long = death
	constexpr float StarvingSpeedFactor = 0.2f;  // unfed agents work/move at 1/5 speed
	constexpr float ArrivalTolerance   = 2.0f;   // cm; movement snaps to target on arrival
	constexpr float MinBuildingSpacing = 320.f;  // cm between building centres
	constexpr float BuildingRadius     = MinBuildingSpacing * 0.5f;
	constexpr float FieldRadius        = 260.f;  // farm field plot (half-size 220 + margin)
	constexpr float TreeRadius         = 40.f;   // trunk footprint for build-over checks

	// Tier system tuning. Downgrade refunds this fraction of each edge's cost
	// (full refund per current design; keep adjustable).
	constexpr float DowngradeRefund = 1.0f;

	constexpr int32 ResIdx(EResource R) { return static_cast<int32>(R); }

	// Ground space a building occupies, as discs: its own footprint, plus the
	// attached field plot for farms. Placement rejects any disc overlap, so
	// fields collide with buildings and with other fields.
	int32 GetOccupiedDiscs(EBuildingType Type, const FVector& Pos,
		FVector OutCenters[2], float OutRadii[2])
	{
		OutCenters[0] = Pos;
		OutRadii[0]   = BuildingRadius;
		if (Type == EBuildingType::Farm)
		{
			OutCenters[1] = Pos + FVector(FarmFieldOffset, 0.f, 0.f);
			OutRadii[1]   = FieldRadius;
			return 2;
		}
		return 1;
	}

	float StarvingMult(const FAgent& A) { return A.StarveTimer > 0.f ? StarvingSpeedFactor : 1.f; }
}

// Placeholder footprints derived from the current debug-ghost box extents
// (±120 cm). The longer side runs along local Y so the building presents its
// long wall to a road when snapped. TODO(user): tune to the real mesh bounds —
// the current building meshes are placeholders.
FVector2D BuildingFootprintHalfSize(EBuildingType Type)
{
	switch (Type)
	{
	case EBuildingType::House:      return FVector2D(110.f, 140.f);
	case EBuildingType::Warehouse:  return FVector2D(140.f, 180.f);
	case EBuildingType::Lumberyard: return FVector2D(120.f, 150.f);
	case EBuildingType::Sawmill:    return FVector2D(130.f, 160.f);
	case EBuildingType::Farm:       return FVector2D(120.f, 130.f);   // field plot is separate
	case EBuildingType::Temple:     return FVector2D(130.f, 150.f);
	case EBuildingType::Dojo:       return FVector2D(130.f, 150.f);
	default:                        return FVector2D(120.f, 120.f);
	}
}

// Tier ladder edges (a TREE: one incoming edge per tier — downgrade relies on
// it). Costs/requirements are PLACEHOLDER tuning pending Anton's numbers
// (population_todos.md §11); the structure is final.
const TArray<FHouseUpgradeRule>& GetHouseUpgradeRules()
{
	static const TArray<FHouseUpgradeRule> Rules = {
		{ ETier::Peasant, ETier::Artisan,     EBuildingType::None,
			{ { EResource::Plank, 4 } } },
		{ ETier::Artisan, ETier::Samurai,     EBuildingType::Dojo,
			{ { EResource::Plank, 8 }, { EResource::Food, 4 } } },
		{ ETier::Peasant, ETier::Monk,        EBuildingType::Temple,
			{ { EResource::Plank, 2 }, { EResource::Food, 2 } } },
		// Gate TBD (upgraded temple?); a plain Temple stands in for now.
		{ ETier::Monk,    ETier::WarriorMonk, EBuildingType::Temple,
			{ { EResource::Plank, 6 }, { EResource::Food, 4 } } },
	};
	return Rules;
}

void FSimWorld::Tick(float Dt)
{
	Phase_Clock(Dt);
	Phase_Needs(Dt);
	Phase_JobAssignment();
	Phase_Production(Dt);
	Phase_Movement(Dt);
	++TickNumber;
}

void FSimWorld::Phase_Clock(float /*Dt*/)      { /* advance time/season later */ }

// Food need, abstract: every EatInterval each villager consumes one food
// straight from warehouse stock — no walking, no eat trips. When the warehouse
// can't provide a due meal, the agent's starvation clock runs; it dies after
// StarveSeconds unfed. Everyone dead => lose.
void FSimWorld::Phase_Needs(float Dt)
{
	int32 Alive = 0;
	for (FAgent& A : Agents)
	{
		if (A.State == EAgentState::Dead)
		{
			continue;
		}

		A.EatTimer += Dt;
		if (A.EatTimer >= EatIntervalSeconds)
		{
			if (ConsumeFromWarehouses(EResource::Food, 1))
			{
				A.EatTimer    = 0.f;
				A.StarveTimer = 0.f;
			}
			else
			{
				// Meal overdue and the pantry is empty: starve. EatTimer stays
				// due, so the next food to arrive feeds this agent immediately.
				A.StarveTimer += Dt;
				if (A.StarveTimer >= StarveSeconds)
				{
					KillAgent(A);
					continue;
				}
			}
		}
		++Alive;
	}

	if (bEverHadAgents && Alive == 0 && !bGameOver)
	{
		bGameOver = true;
	}
}

// Idle assigned agents start their building's work loop; arrivals advance the
// state machine. Unassigned villagers stay idle — work is a player decision.
void FSimWorld::Phase_JobAssignment()
{
	for (FAgent& A : Agents)
	{
		switch (A.State)
		{
		case EAgentState::Idle:
		{
			// Leftover carry (e.g. warehouse appeared after a stranded harvest):
			// deliver before starting anything new.
			if (A.CarriedAmount > 0)
			{
				const FBuildingId S = FindNearestWarehouse(A.Position);
				if (S != INVALID_ID)
				{
					A.DeliverTo = S;
					A.Target    = Buildings[S].Position;
					A.State     = EAgentState::MovingToDeliver;
				}
				break;
			}

			if (!Buildings.IsValidIndex(A.AssignedBuilding))
			{
				break;   // unemployed: waits for the player
			}

			const FBuilding& WB = Buildings[A.AssignedBuilding];
			switch (WB.Type)
			{
			case EBuildingType::Lumberyard: StartChop(A);                          break;
			case EBuildingType::Farm:       StartFieldWork(A, WB);                 break;
			case EBuildingType::Sawmill:    StartSawmillWork(A, A.AssignedBuilding); break;
			case EBuildingType::Temple:     StartAttend(A, WB);                    break;
			case EBuildingType::Dojo:       StartAttend(A, WB);                    break;
			default:                                                               break;
			}
			break;
		}

		case EAgentState::MovingToWork:
		{
			if (FVector::Dist(A.Position, A.Target) <= ArrivalTolerance)
			{
				A.State     = EAgentState::Working;
				A.WorkTimer = 0.f;
			}
			break;
		}

		case EAgentState::MovingToStore:
		{
			if (FVector::Dist(A.Position, A.Target) <= ArrivalTolerance)
			{
				const FBuildingId S = FindNearestWarehouse(A.Position);
				if (S != INVALID_ID)
				{
					Buildings[S].Stored[ResIdx(A.CarriedType)] += A.CarriedAmount;
					A.CarriedAmount = 0;
					A.CarriedType   = EResource::None;
				}
				FinishJob(A);
			}
			break;
		}

		case EAgentState::MovingToPickup:
		{
			if (FVector::Dist(A.Position, A.Target) > ArrivalTolerance)
			{
				break;
			}

			if (A.Job == EJobKind::HaulOutput && Buildings.IsValidIndex(A.PickupFrom))
			{
				// Collect a plank from the sawmill's output buffer.
				FBuilding& Mill = Buildings[A.PickupFrom];
				Mill.bOutputClaimed = false;   // claim released at pickup
				if (Mill.Stored[ResIdx(EResource::Plank)] > 0)
				{
					Mill.Stored[ResIdx(EResource::Plank)] -= 1;
					A.CarriedAmount = 1;
					A.CarriedType   = EResource::Plank;

					const FBuildingId S = FindNearestWarehouse(A.Position);
					if (S != INVALID_ID)
					{
						A.DeliverTo = S;
						A.Target    = Buildings[S].Position;
						A.State     = EAgentState::MovingToDeliver;
						break;
					}
				}
				FinishJob(A);   // nothing to take / nowhere to put it
			}
			else if (A.Job == EJobKind::FeedSawmill && Buildings.IsValidIndex(A.PickupFrom))
			{
				FBuilding& Source = Buildings[A.PickupFrom];
				if (Source.Stored[ResIdx(EResource::Log)] > 0 &&
					Buildings.IsValidIndex(A.DeliverTo))
				{
					Source.Stored[ResIdx(EResource::Log)] -= 1;
					A.CarriedAmount = 1;
					A.CarriedType   = EResource::Log;
					A.Target        = Buildings[A.DeliverTo].Position;
					A.State         = EAgentState::MovingToDeliver;
				}
				else
				{
					// Logs vanished while walking; release the sawmill's claim.
					if (Buildings.IsValidIndex(A.DeliverTo))
					{
						Buildings[A.DeliverTo].bInputClaimed = false;
					}
					FinishJob(A);
				}
			}
			else
			{
				FinishJob(A);
			}
			break;
		}

		case EAgentState::MovingToDeliver:
		{
			if (FVector::Dist(A.Position, A.Target) > ArrivalTolerance)
			{
				break;
			}

			if (Buildings.IsValidIndex(A.DeliverTo))
			{
				FBuilding& Dest = Buildings[A.DeliverTo];
				Dest.Stored[ResIdx(A.CarriedType)] += A.CarriedAmount;
				if (A.Job == EJobKind::FeedSawmill)
				{
					Dest.bInputClaimed = false;
				}
				A.CarriedAmount = 0;
				A.CarriedType   = EResource::None;
			}
			FinishJob(A);
			break;
		}

		default:
			break;
		}
	}
}

// Working agents advance their job timer; the sawmill runs its own conversion.
void FSimWorld::Phase_Production(float Dt)
{
	for (FAgent& A : Agents)
	{
		if (A.State != EAgentState::Working)
		{
			continue;
		}

		A.WorkTimer += Dt * StarvingMult(A);   // starving villagers work at 1/5 pace

		if (A.Job == EJobKind::Chop)
		{
			if (A.WorkTimer < ChopDuration)
			{
				continue;
			}

			// One log produced from the claimed tree.
			if (Trees.IsValidIndex(A.TargetTree) && Trees[A.TargetTree].Remaining > 0)
			{
				Trees[A.TargetTree].Remaining -= 1;
				A.CarriedAmount = 1;
				A.CarriedType   = EResource::Log;
			}

			// Release the claim (re-acquired next idle cycle).
			if (Trees.IsValidIndex(A.TargetTree))
			{
				Trees[A.TargetTree].bClaimed = false;
			}
			A.TargetTree = INVALID_ID;
		}
		else if (A.Job == EJobKind::WorkField)
		{
			if (A.WorkTimer < FieldWorkDuration)
			{
				continue;
			}
			A.CarriedAmount = FarmYield;
			A.CarriedType   = EResource::Food;
		}
		else if (A.Job == EJobKind::Attend)
		{
			// Temple/dojo: stay "working" indefinitely — no production yet
			// (effects are future design). Unassignment is the only way out.
			A.WorkTimer = 0.f;
			continue;
		}
		else
		{
			FinishJob(A);
			continue;
		}

		A.WorkTimer = 0.f;

		// Carry the produce to the warehouse. With nowhere to deposit, the agent
		// idles holding it (delivered later via the Idle leftover-carry path) —
		// no silent mid-job stall (production-chain deadlock guard).
		const FBuildingId S = FindNearestWarehouse(A.Position);
		if (S != INVALID_ID && A.CarriedAmount > 0)
		{
			A.Target = Buildings[S].Position;
			A.State  = EAgentState::MovingToStore;
		}
		else
		{
			A.Job   = EJobKind::None;
			A.State = EAgentState::Idle;
		}
	}

	for (FBuilding& B : Buildings)
	{
		if (B.Type != EBuildingType::Sawmill)
		{
			continue;
		}
		// Stalls visibly when fed nothing or when the output buffer is full —
		// its assigned workers clearing planks / bringing logs un-stall it
		// (explicit blocked-queue handling, baoding.md hardest problem #2).
		if (B.Stored[ResIdx(EResource::Log)] > 0 &&
			B.Stored[ResIdx(EResource::Plank)] < SawmillOutputCap)
		{
			B.WorkTimer += Dt;
			if (B.WorkTimer >= SawDuration)
			{
				B.WorkTimer = 0.f;
				B.Stored[ResIdx(EResource::Log)]   -= 1;
				B.Stored[ResIdx(EResource::Plank)] += 1;
			}
		}
	}
}

void FSimWorld::Phase_Movement(float Dt)
{
	// Follow each moving agent's planned FAgentPath (pathfinding.md §6). Paths
	// are planned lazily here at the moment an agent starts walking a new leg, so
	// no other phase needs to know about pathfinding — they keep setting A.Target
	// exactly as before, and the final waypoint == A.Target keeps arrival
	// detection unchanged.
	AgentPaths.SetNum(Agents.Num());   // keep the parallel array sized to Agents

	int32 ReplansThisTick = 0;
	for (int32 i = 0; i < Agents.Num(); ++i)
	{
		FAgent& A = Agents[i];
		if (A.State != EAgentState::MovingToWork &&
			A.State != EAgentState::MovingToStore &&
			A.State != EAgentState::MovingToPickup &&
			A.State != EAgentState::MovingToDeliver)
		{
			continue;
		}

		FAgentPath& Path = AgentPaths[i];
		const FVector2D Goal2D(A.Target.X, A.Target.Y);

		// A genuinely new leg (target changed, or the previous path is empty /
		// already consumed) plans immediately. A path merely stale because the
		// road network changed counts against the per-tick replan budget; until
		// it gets its turn the agent keeps walking its old (still-valid) path.
		const bool bNewLeg = Path.Waypoints.Num() == 0 || Path.IsConsumed() ||
			!Path.FinalGoal.Equals(Goal2D, 1.f);
		const bool bStale = Path.PlannedNavVersion != NavVersion;

		if (bNewLeg)
		{
			const FVector2D Start2D(A.Position.X, A.Position.Y);
			Path = RoadPathfinder::PlanPath(Start2D, Goal2D, NavRoads, NavParams);
			Path.PlannedNavVersion = NavVersion;
		}
		else if (bStale && ReplansThisTick < MaxReplansPerTick)
		{
			const FVector2D Start2D(A.Position.X, A.Position.Y);
			Path = RoadPathfinder::PlanPath(Start2D, Goal2D, NavRoads, NavParams);
			Path.PlannedNavVersion = NavVersion;
			++ReplansThisTick;
		}

		const float BaseStep = A.Speed * Dt * StarvingMult(A);   // 1/5 speed while starving
		const FVector2D Pos2D(A.Position.X, A.Position.Y);
		const FVector2D New2D = Nav::StepAlongPath(Path, Pos2D, BaseStep, NavParams);
		if (Path.IsConsumed())
		{
			A.Position = A.Target;   // snap exactly so arrival fires next tick (§6)
		}
		else
		{
			A.Position.X = New2D.X;
			A.Position.Y = New2D.Y;   // Z stays on the sim's flat plane (render lifts it)
		}
	}
}

void FSimWorld::SetNavRoads(FSimNavRoads&& In, const FSimNavParams& Params)
{
	NavRoads   = MoveTemp(In);
	NavParams  = Params;
	NavRoads.Version = ++NavVersion;   // walking agents lazily replan (§7)
}

bool FSimWorld::DebugMoveAgent(FAgentId Agent, const FVector& Goal)
{
	if (!Agents.IsValidIndex(Agent) || Agents[Agent].State == EAgentState::Dead)
	{
		return false;
	}
	FAgent& A = Agents[Agent];
	A.Target = Goal;
	A.State  = EAgentState::MovingToWork;   // Phase_Movement plans on the next tick
	return true;
}

bool FSimWorld::GetAgentPath(FAgentId Agent, FAgentPath& Out) const
{
	if (!AgentPaths.IsValidIndex(Agent))
	{
		return false;
	}
	Out = AgentPaths[Agent];
	return true;
}

// --- Per-building work loops ---

void FSimWorld::StartChop(FAgent& A)
{
	const FTreeId T = FindNearestAvailableTree(A.Position);
	if (T == INVALID_ID)
	{
		return;   // no trees left: stand by at the lumberyard
	}
	Trees[T].bClaimed = true;
	A.Job        = EJobKind::Chop;
	A.TargetTree = T;
	A.Target     = Trees[T].Position;
	A.State      = EAgentState::MovingToWork;
}

void FSimWorld::StartFieldWork(FAgent& A, const FBuilding& Farm)
{
	// Pick a spot in the field plot attached to the farm (the sub-building).
	const FVector FieldCenter = Farm.Position + FVector(FarmFieldOffset, 0.f, 0.f);
	const FVector Spot = FieldCenter + FVector(
		FMath::FRandRange(-FarmFieldHalfSize, FarmFieldHalfSize),
		FMath::FRandRange(-FarmFieldHalfSize, FarmFieldHalfSize),
		0.f);

	A.Job    = EJobKind::WorkField;
	A.Target = Spot;
	A.State  = EAgentState::MovingToWork;
}

// Temple/dojo minimal loop: walk to the workplace and idle-"work" there.
void FSimWorld::StartAttend(FAgent& A, const FBuilding& B)
{
	A.Job    = EJobKind::Attend;
	A.Target = B.Position;
	A.State  = EAgentState::MovingToWork;
}

void FSimWorld::StartSawmillWork(FAgent& A, FBuildingId MillId)
{
	FBuilding& Mill = Buildings[MillId];

	// Clear finished planks first, then keep the input buffer fed.
	if (Mill.Stored[ResIdx(EResource::Plank)] > 0 && !Mill.bOutputClaimed &&
		FindNearestWarehouse(A.Position) != INVALID_ID)
	{
		Mill.bOutputClaimed = true;
		A.Job        = EJobKind::HaulOutput;
		A.PickupFrom = MillId;
		A.Target     = Mill.Position;
		A.State      = EAgentState::MovingToPickup;
		return;
	}

	if (Mill.Stored[ResIdx(EResource::Log)] < SawmillInputCap && !Mill.bInputClaimed)
	{
		const FBuildingId Source = FindNearestWarehouseWith(A.Position, EResource::Log);
		if (Source != INVALID_ID)
		{
			Mill.bInputClaimed = true;
			A.Job        = EJobKind::FeedSawmill;
			A.PickupFrom = Source;
			A.DeliverTo  = MillId;
			A.Target     = Buildings[Source].Position;
			A.State      = EAgentState::MovingToPickup;
		}
	}
	// Otherwise: mill is saturated; stand by.
}

void FSimWorld::FinishJob(FAgent& A)
{
	A.Job        = EJobKind::None;
	A.PickupFrom = INVALID_ID;
	A.DeliverTo  = INVALID_ID;
	A.State      = EAgentState::Idle;
}

// Release everything the agent holds mid-errand. Carried goods are lost.
void FSimWorld::AbortJob(FAgent& A)
{
	if (Trees.IsValidIndex(A.TargetTree))
	{
		Trees[A.TargetTree].bClaimed = false;
	}
	if (A.Job == EJobKind::HaulOutput && Buildings.IsValidIndex(A.PickupFrom))
	{
		Buildings[A.PickupFrom].bOutputClaimed = false;
	}
	if (A.Job == EJobKind::FeedSawmill && Buildings.IsValidIndex(A.DeliverTo))
	{
		Buildings[A.DeliverTo].bInputClaimed = false;
	}
	A.TargetTree    = INVALID_ID;
	A.CarriedAmount = 0;
	A.CarriedType   = EResource::None;
	FinishJob(A);
}

void FSimWorld::KillAgent(FAgent& A)
{
	AbortJob(A);
	if (Buildings.IsValidIndex(A.AssignedBuilding))
	{
		Buildings[A.AssignedBuilding].AssignedWorkers -= 1;
	}
	A.AssignedBuilding = INVALID_ID;
	A.State            = EAgentState::Dead;
}

// --- Workforce commands ---

int32 FSimWorld::MaxWorkersFor(EBuildingType Type)
{
	switch (Type)
	{
	case EBuildingType::Lumberyard: return 3;
	case EBuildingType::Sawmill:    return 2;
	case EBuildingType::Farm:       return 3;
	case EBuildingType::Temple:     return 2;
	case EBuildingType::Dojo:       return 2;
	default:                        return 0;   // warehouse/house take no workers
	}
}

// Tier gate per building type (population_todos.md §2.4). Eligibility is
// enforced entirely at assignment time; job loops never check tiers.
uint8 FSimWorld::AllowedTiersFor(EBuildingType Type)
{
	switch (Type)
	{
	case EBuildingType::Lumberyard: return TierBit(ETier::Peasant);
	case EBuildingType::Sawmill:    return TierBit(ETier::Peasant);
	case EBuildingType::Farm:       return TierBit(ETier::Peasant);
	case EBuildingType::Temple:     return TierBit(ETier::Monk) | TierBit(ETier::WarriorMonk);
	case EBuildingType::Dojo:       return TierBit(ETier::Samurai);
	default:                        return 0;
	}
}

ETier FSimWorld::GetAgentTier(FAgentId Agent) const
{
	if (!Agents.IsValidIndex(Agent))
	{
		return ETier::Peasant;
	}
	const FAgent& A = Agents[Agent];
	if (!Buildings.IsValidIndex(A.HomeBuilding))
	{
		return ETier::Peasant;   // homeless (e.g. pre-tier save): safety fallback
	}
	return Buildings[A.HomeBuilding].ResidentTier;
}

bool FSimWorld::AssignWorkerTo(FBuildingId Building)
{
	if (!Buildings.IsValidIndex(Building))
	{
		return false;
	}
	FBuilding& B = Buildings[Building];
	if (B.AssignedWorkers >= MaxWorkersFor(B.Type))
	{
		return false;
	}

	// The idle-pool search only considers villagers of an eligible tier.
	const uint8 Allowed = AllowedTiersFor(B.Type);
	for (int32 i = 0; i < Agents.Num(); ++i)
	{
		FAgent& A = Agents[i];
		if (A.State != EAgentState::Dead && A.AssignedBuilding == INVALID_ID &&
			(TierBit(GetAgentTier(i)) & Allowed) != 0)
		{
			A.AssignedBuilding = Building;
			B.AssignedWorkers += 1;
			return true;
		}
	}
	return false;   // no idle villager of an eligible tier
}

bool FSimWorld::UnassignWorkerFrom(FBuildingId Building)
{
	if (!Buildings.IsValidIndex(Building))
	{
		return false;
	}
	for (FAgent& A : Agents)
	{
		if (A.State != EAgentState::Dead && A.AssignedBuilding == Building)
		{
			AbortJob(A);
			A.AssignedBuilding = INVALID_ID;
			Buildings[Building].AssignedWorkers -= 1;
			return true;
		}
	}
	return false;
}

// --- House tier commands ---

// Side-effect-free and re-runnable: the snapshot mirrors it for button states.
// Checks are independent, each with its own fail reason, so future
// requirement kinds append cleanly.
EUpgradeFail FSimWorld::CanUpgradeHouse(FBuildingId House, const FHouseUpgradeRule& Rule) const
{
	if (!Buildings.IsValidIndex(House) || Buildings[House].Type != EBuildingType::House ||
		Buildings[House].ResidentTier != Rule.From)
	{
		return EUpgradeFail::NoRule;
	}
	if (Rule.RequiredBuilding != EBuildingType::None && !HasBuilding(Rule.RequiredBuilding))
	{
		return EUpgradeFail::MissingBuilding;
	}
	for (const FResourceCost& Cost : Rule.Costs)
	{
		if (Cost.Amount > 0 && CountStock(Cost.Resource) < Cost.Amount)
		{
			return EUpgradeFail::NotEnoughResources;
		}
	}
	return EUpgradeFail::None;
}

bool FSimWorld::UpgradeHouse(FBuildingId House, ETier Target)
{
	if (!Buildings.IsValidIndex(House) || Buildings[House].Type != EBuildingType::House)
	{
		return false;
	}

	// Rule lookup by (From == current, To == target) rejects branch-crossing
	// and edge-skipping by construction — upgrades move one edge at a time.
	const FHouseUpgradeRule* Rule = nullptr;
	for (const FHouseUpgradeRule& R : GetHouseUpgradeRules())
	{
		if (R.From == Buildings[House].ResidentTier && R.To == Target)
		{
			Rule = &R;
			break;
		}
	}
	if (!Rule || CanUpgradeHouse(House, *Rule) != EUpgradeFail::None)
	{
		return false;
	}

	for (const FResourceCost& Cost : Rule->Costs)
	{
		if (Cost.Amount > 0)
		{
			ConsumeFromWarehouses(Cost.Resource, Cost.Amount);
		}
	}
	Buildings[House].ResidentTier = Rule->To;
	InvalidateMismatchedJobs(House);
	return true;
}

bool FSimWorld::DowngradeHouse(FBuildingId House)
{
	if (!Buildings.IsValidIndex(House) || Buildings[House].Type != EBuildingType::House ||
		Buildings[House].ResidentTier == ETier::Peasant)
	{
		return false;
	}

	// The tier graph is a tree, so the rule whose To matches the current tier
	// is unique — no per-house upgrade history needed.
	const FHouseUpgradeRule* Rule = nullptr;
	for (const FHouseUpgradeRule& R : GetHouseUpgradeRules())
	{
		if (R.To == Buildings[House].ResidentTier)
		{
			Rule = &R;
			break;
		}
	}
	if (!Rule)
	{
		return false;
	}

	// Refund into warehouse stock (lost if no warehouse stands — edge case).
	const FBuildingId Store = FindNearestWarehouse(Buildings[House].Position);
	if (Store != INVALID_ID)
	{
		for (const FResourceCost& Cost : Rule->Costs)
		{
			const int32 Back = FMath::FloorToInt32(Cost.Amount * DowngradeRefund);
			if (Back > 0)
			{
				Buildings[Store].Stored[ResIdx(Cost.Resource)] += Back;
			}
		}
	}

	Buildings[House].ResidentTier = Rule->From;
	InvalidateMismatchedJobs(House);
	return true;
}

// After a house changes tier, its residents working a now-ineligible
// building vacate it (same path as the [-] button: claims released, back to
// the idle pool). Linear agent scan is fine at current scale (hundreds).
void FSimWorld::InvalidateMismatchedJobs(FBuildingId House)
{
	const uint8 Bit = TierBit(Buildings[House].ResidentTier);
	for (FAgent& A : Agents)
	{
		if (A.State == EAgentState::Dead || A.HomeBuilding != House)
		{
			continue;
		}
		const FBuildingId B = A.AssignedBuilding;
		if (B != INVALID_ID && (Bit & AllowedTiersFor(Buildings[B].Type)) == 0)
		{
			AbortJob(A);
			A.AssignedBuilding = INVALID_ID;
			Buildings[B].AssignedWorkers -= 1;
		}
	}
}

int32 FSimWorld::CountStock(EResource Resource) const
{
	int32 Total = 0;
	for (const FBuilding& B : Buildings)
	{
		if (B.Type == EBuildingType::Warehouse)
		{
			Total += B.Stored[ResIdx(Resource)];
		}
	}
	return Total;
}

// --- Needs ---

// Pull from warehouse stock regardless of distance (the need is abstract; only
// hauling moves goods physically). Drains warehouses in index order.
bool FSimWorld::ConsumeFromWarehouses(EResource Resource, int32 Amount)
{
	for (FBuilding& B : Buildings)
	{
		if (B.Type != EBuildingType::Warehouse)
		{
			continue;
		}
		const int32 Take = FMath::Min(B.Stored[ResIdx(Resource)], Amount);
		B.Stored[ResIdx(Resource)] -= Take;
		Amount -= Take;
		if (Amount <= 0)
		{
			return true;
		}
	}
	return false;   // not enough in stock; nothing partial was kept back
}

// --- Commands ---

bool FSimWorld::CanPlaceBuilding(EBuildingType Type, const FVector& Pos) const
{
	FVector NewCenters[2];
	float   NewRadii[2];
	const int32 NewCount = GetOccupiedDiscs(Type, Pos, NewCenters, NewRadii);

	for (const FBuilding& B : Buildings)
	{
		FVector ExistingCenters[2];
		float   ExistingRadii[2];
		const int32 ExistingCount = GetOccupiedDiscs(B.Type, B.Position,
			ExistingCenters, ExistingRadii);

		for (int32 i = 0; i < NewCount; ++i)
		{
			for (int32 j = 0; j < ExistingCount; ++j)
			{
				const float MinDist = NewRadii[i] + ExistingRadii[j];
				if (FVector::DistSquared2D(NewCenters[i], ExistingCenters[j]) < MinDist * MinDist)
				{
					return false;
				}
			}
		}
	}
	return true;
}

FBuildingId FSimWorld::PlaceBuilding(EBuildingType Type, const FVector& Pos,
	const FVector& VisualScale, float YawDegrees)
{
	if (!CanPlaceBuilding(Type, Pos))
	{
		return INVALID_ID;   // sim validates commands; the UI ghost mirrors this
	}
	// Hard cap: a settlement has exactly one warehouse.
	if (Type == EBuildingType::Warehouse && HasBuilding(EBuildingType::Warehouse))
	{
		return INVALID_ID;
	}

	// Construction clears the ground: trees under the footprint (or under a
	// farm's field plot) are destroyed. Depleted-in-place rather than removed —
	// tree IDs are array indices held by agents, and a chopper already en route
	// simply finds nothing and releases its claim.
	FVector DiscCenters[2];
	float   DiscRadii[2];
	const int32 DiscCount = GetOccupiedDiscs(Type, Pos, DiscCenters, DiscRadii);
	for (FTree& T : Trees)
	{
		if (T.Remaining <= 0)
		{
			continue;
		}
		for (int32 i = 0; i < DiscCount; ++i)
		{
			const float MinDist = DiscRadii[i] + TreeRadius;
			if (FVector::DistSquared2D(T.Position, DiscCenters[i]) < MinDist * MinDist)
			{
				T.Remaining = 0;
				break;
			}
		}
	}

	FBuilding B;
	B.Type        = Type;
	B.Position    = Pos;
	B.VisualScale = VisualScale;
	B.YawDegrees  = YawDegrees;
	return Buildings.Add(B);
}

FAgentId FSimWorld::SpawnAgent(const FVector& Pos, FBuildingId Home)
{
	FAgent A;
	A.Position     = Pos;
	A.HomeBuilding = Home;   // tier is derived from this house's ResidentTier
	bEverHadAgents = true;
	const FAgentId Id = Agents.Add(A);
	AgentPaths.SetNum(Agents.Num());   // parallel path slot (replanned on demand)
	return Id;
}

FTreeId FSimWorld::SpawnTree(const FVector& Pos, const FVector& VisualScale)
{
	FTree T;
	T.Position    = Pos;
	T.VisualScale = VisualScale;
	return Trees.Add(T);
}

void FSimWorld::AddResource(FBuildingId Building, EResource Resource, int32 Amount)
{
	if (Buildings.IsValidIndex(Building) && Resource != EResource::None)
	{
		Buildings[Building].Stored[ResIdx(Resource)] += Amount;
	}
}

// --- Queries ---

bool FSimWorld::HasBuilding(EBuildingType Type) const
{
	for (const FBuilding& B : Buildings)
	{
		if (B.Type == Type)
		{
			return true;
		}
	}
	return false;
}

FTreeId FSimWorld::FindNearestAvailableTree(const FVector& From) const
{
	// Linear scan: fine at current counts. A spatial index / priority queue
	// replaces this when agent counts grow (see baoding.md "Three hardest problems").
	FTreeId Best = INVALID_ID;
	float   BestDistSq = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Trees.Num(); ++i)
	{
		const FTree& T = Trees[i];
		if (T.bClaimed || T.Remaining <= 0)
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(From, T.Position);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = i;
		}
	}
	return Best;
}

FBuildingId FSimWorld::FindNearestWarehouse(const FVector& From) const
{
	FBuildingId Best = INVALID_ID;
	float       BestDistSq = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Buildings.Num(); ++i)
	{
		if (Buildings[i].Type != EBuildingType::Warehouse)
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(From, Buildings[i].Position);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = i;
		}
	}
	return Best;
}

FBuildingId FSimWorld::FindNearestWarehouseWith(const FVector& From, EResource Resource) const
{
	FBuildingId Best = INVALID_ID;
	float       BestDistSq = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Buildings.Num(); ++i)
	{
		if (Buildings[i].Type != EBuildingType::Warehouse ||
			Buildings[i].Stored[ResIdx(Resource)] <= 0)
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(From, Buildings[i].Position);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = i;
		}
	}
	return Best;
}

// --- Snapshot ---

void FSimWorld::BuildSnapshot(FSimSnapshot& Out) const
{
	Out.Agents.Reset(Agents.Num());
	Out.Population    = 0;
	Out.IdleVillagers = 0;
	FMemory::Memzero(Out.IdleByTier, sizeof(Out.IdleByTier));
	for (int32 i = 0; i < Agents.Num(); ++i)
	{
		const FAgent& A = Agents[i];
		FAgentSnapshot Snap;
		Snap.Position      = A.Position;
		Snap.State         = A.State;
		Snap.CarriedAmount = A.CarriedAmount;
		Snap.CarriedType   = A.CarriedType;
		Snap.bAssigned     = A.AssignedBuilding != INVALID_ID;
		Snap.bStarving     = A.StarveTimer > 0.f;
		Snap.Tier          = GetAgentTier(i);
		Out.Agents.Add(Snap);
		if (A.State != EAgentState::Dead)
		{
			++Out.Population;
			if (A.AssignedBuilding == INVALID_ID)
			{
				++Out.IdleVillagers;
				++Out.IdleByTier[static_cast<int32>(Snap.Tier)];
			}
		}
	}

	Out.Buildings.Reset(Buildings.Num());
	Out.LogCount = Out.PlankCount = Out.FoodCount = 0;
	for (int32 i = 0; i < Buildings.Num(); ++i)
	{
		const FBuilding& B = Buildings[i];
		FBuildingSnapshot Snap;
		Snap.Position        = B.Position;
		Snap.Type            = B.Type;
		Snap.AssignedWorkers = B.AssignedWorkers;
		Snap.MaxWorkers      = MaxWorkersFor(B.Type);
		Snap.VisualScale     = B.VisualScale;
		Snap.YawDegrees      = B.YawDegrees;
		Snap.AllowedTiers    = AllowedTiersFor(B.Type);
		Snap.ResidentTier    = B.ResidentTier;
		if (B.Type == EBuildingType::House)
		{
			// List the outgoing tier edges with their current fail state
			// (a Peasant house has two: Artisan and Monk).
			for (const FHouseUpgradeRule& Rule : GetHouseUpgradeRules())
			{
				if (Rule.From == B.ResidentTier &&
					Snap.NumUpgradeEdges < (int32)UE_ARRAY_COUNT(Snap.UpgradeTo))
				{
					Snap.UpgradeTo[Snap.NumUpgradeEdges]   = Rule.To;
					Snap.UpgradeFail[Snap.NumUpgradeEdges] = CanUpgradeHouse(i, Rule);
					++Snap.NumUpgradeEdges;
				}
			}
			Snap.bCanDowngrade = B.ResidentTier != ETier::Peasant;
		}
		Out.Buildings.Add(Snap);
		if (B.Type == EBuildingType::Warehouse)
		{
			Out.LogCount   += B.Stored[ResIdx(EResource::Log)];
			Out.PlankCount += B.Stored[ResIdx(EResource::Plank)];
			Out.FoodCount  += B.Stored[ResIdx(EResource::Food)];
		}
	}

	Out.Trees.Reset(Trees.Num());
	for (const FTree& T : Trees)
	{
		Out.Trees.Add({ T.Position, T.Remaining, T.VisualScale });
	}

	Out.bGameOver  = bGameOver;
	Out.TickNumber = TickNumber;
}

// --- Save/load ---

namespace
{
	// v5 struct layouts, frozen for legacy load (v6 added FAgent::HomeBuilding
	// and FBuilding::ResidentTier). Field order must match the v5 structs
	// exactly — these are read back as raw blocks.
	struct FAgentV5
	{
		FVector     Position = FVector::ZeroVector;
		FVector     Target   = FVector::ZeroVector;
		float       Speed    = 200.f;
		EAgentState State    = EAgentState::Idle;
		EJobKind    Job      = EJobKind::None;
		FBuildingId AssignedBuilding = INVALID_ID;
		FBuildingId PickupFrom = INVALID_ID;
		FBuildingId DeliverTo  = INVALID_ID;
		FTreeId     TargetTree = INVALID_ID;
		float       WorkTimer  = 0.f;
		int32       CarriedAmount = 0;
		EResource   CarriedType   = EResource::None;
		float       EatTimer    = 0.f;
		float       StarveTimer = 0.f;
	};

	struct FBuildingV5
	{
		EBuildingType Type     = EBuildingType::None;
		FVector       Position = FVector::ZeroVector;
		FVector       VisualScale = FVector::ZeroVector;
		int32         Stored[NumResources] = {};
		float         WorkTimer       = 0.f;
		int32         AssignedWorkers = 0;
		bool          bInputClaimed   = false;
		bool          bOutputClaimed  = false;
	};

	// v6 building layout, frozen for legacy load (v7 appended FBuilding::YawDegrees).
	// FAgent and FTree were unchanged between v6 and v7, so only buildings need a
	// frozen struct. Field order must match the v6 FBuilding exactly.
	struct FBuildingV6
	{
		EBuildingType Type     = EBuildingType::None;
		FVector       Position = FVector::ZeroVector;
		ETier         ResidentTier = ETier::Peasant;
		FVector       VisualScale = FVector::ZeroVector;
		int32         Stored[NumResources] = {};
		float         WorkTimer       = 0.f;
		int32         AssignedWorkers = 0;
		bool          bInputClaimed   = false;
		bool          bOutputClaimed  = false;
	};
}

void FSimWorld::Serialize(FArchive& Ar)
{
	// v7: building yaw (FBuilding::YawDegrees).
	// v6: tiers (FAgent::HomeBuilding, FBuilding::ResidentTier).
	// v5: per-instance visual scale (FTree/FBuilding layout changed).
	int32 Version = 7;
	Ar << Version;
	if (Ar.IsLoading() && Version != 7 && Version != 6 && Version != 5)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RealmSave] Sim save version %d unsupported (want 5-7); load skipped."), Version);
		return;
	}

	Ar << TickNumber;

	uint8 EverHad = bEverHadAgents ? 1 : 0;
	uint8 Over    = bGameOver ? 1 : 0;
	Ar << EverHad;
	Ar << Over;
	if (Ar.IsLoading())
	{
		bEverHadAgents = EverHad != 0;
		bGameOver      = Over != 0;
	}

	// The sim structs are trivially copyable by design, so each array is one
	// count + one raw block. Bump Version when any struct layout changes.
	const auto SerializeArray = [&Ar](auto& Arr)
	{
		int32 Num = Arr.Num();
		Ar << Num;
		if (Ar.IsLoading())
		{
			Arr.SetNum(Num);
		}
		if (Num > 0)
		{
			Ar.Serialize(Arr.GetData(), Num * sizeof(*Arr.GetData()));
		}
	};

	if (Ar.IsLoading() && Version == 5)
	{
		// Pre-tier save: read the frozen v5 layouts, then fill the new fields
		// with their defaults (everyone Peasant, no home link — GetAgentTier
		// falls back to Peasant). Old saves stay playable.
		TArray<FAgentV5>    OldAgents;
		TArray<FBuildingV5> OldBuildings;
		SerializeArray(OldAgents);
		SerializeArray(OldBuildings);
		SerializeArray(Trees);

		Agents.Reset(OldAgents.Num());
		for (const FAgentV5& O : OldAgents)
		{
			FAgent A;
			A.Position = O.Position;   A.Target = O.Target;   A.Speed = O.Speed;
			A.State = O.State;         A.Job = O.Job;
			A.AssignedBuilding = O.AssignedBuilding;
			A.HomeBuilding     = INVALID_ID;
			A.PickupFrom = O.PickupFrom;   A.DeliverTo = O.DeliverTo;
			A.TargetTree = O.TargetTree;   A.WorkTimer = O.WorkTimer;
			A.CarriedAmount = O.CarriedAmount;   A.CarriedType = O.CarriedType;
			A.EatTimer = O.EatTimer;   A.StarveTimer = O.StarveTimer;
			Agents.Add(A);
		}

		Buildings.Reset(OldBuildings.Num());
		for (const FBuildingV5& O : OldBuildings)
		{
			FBuilding B;
			B.Type = O.Type;   B.Position = O.Position;
			B.ResidentTier = ETier::Peasant;
			B.VisualScale = O.VisualScale;
			FMemory::Memcpy(B.Stored, O.Stored, sizeof(B.Stored));
			B.WorkTimer = O.WorkTimer;   B.AssignedWorkers = O.AssignedWorkers;
			B.bInputClaimed = O.bInputClaimed;   B.bOutputClaimed = O.bOutputClaimed;
			Buildings.Add(B);
		}
		AgentPaths.Reset();
		AgentPaths.SetNum(Agents.Num());   // paths aren't saved; replanned on load (§8)
		return;
	}

	if (Ar.IsLoading() && Version == 6)
	{
		// Pre-yaw save: agents/trees match the current layout, only buildings
		// grew the YawDegrees field. Read the frozen v6 building block, default
		// yaw to 0 (everything axis-aligned, exactly as it was authored).
		SerializeArray(Agents);
		TArray<FBuildingV6> OldBuildings;
		SerializeArray(OldBuildings);
		SerializeArray(Trees);

		Buildings.Reset(OldBuildings.Num());
		for (const FBuildingV6& O : OldBuildings)
		{
			FBuilding B;
			B.Type = O.Type;   B.Position = O.Position;
			B.ResidentTier = O.ResidentTier;
			B.VisualScale = O.VisualScale;
			FMemory::Memcpy(B.Stored, O.Stored, sizeof(B.Stored));
			B.WorkTimer = O.WorkTimer;   B.AssignedWorkers = O.AssignedWorkers;
			B.bInputClaimed = O.bInputClaimed;   B.bOutputClaimed = O.bOutputClaimed;
			B.YawDegrees = 0.f;
			Buildings.Add(B);
		}
		AgentPaths.Reset();
		AgentPaths.SetNum(Agents.Num());   // paths aren't saved; replanned on load (§8)
		return;
	}

	SerializeArray(Agents);
	SerializeArray(Buildings);
	SerializeArray(Trees);

	if (Ar.IsLoading())
	{
		AgentPaths.Reset();
		AgentPaths.SetNum(Agents.Num());   // paths aren't saved; replanned on load (§8)
	}
}
