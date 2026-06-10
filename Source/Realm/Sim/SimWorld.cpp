// Copyright Asamoto.
// FSimWorld tick wiring. Economy with a player-directed workforce: villagers
// arrive via houses, idle until assigned to a resource building, then run that
// building's work loop until unassigned (sticky assignment, one building each).

#include "SimWorld.h"

namespace
{
	// Tuning.
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
	// Step agents toward Target along (eventually) a flow field. For now a
	// straight-line step is enough to exercise arrival detection.
	for (FAgent& A : Agents)
	{
		if (A.State == EAgentState::MovingToWork ||
			A.State == EAgentState::MovingToStore ||
			A.State == EAgentState::MovingToPickup ||
			A.State == EAgentState::MovingToDeliver)
		{
			const FVector To   = A.Target - A.Position;
			const float   Dist = To.Size();
			const float   Step = A.Speed * Dt * StarvingMult(A);   // 1/5 speed while starving
			if (Dist <= Step)
			{
				A.Position = A.Target;   // arrival handled next tick in job/production
			}
			else
			{
				A.Position += To / Dist * Step;
			}
		}
	}
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
	default:                        return 0;   // warehouse/house take no workers
	}
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

	for (FAgent& A : Agents)
	{
		if (A.State != EAgentState::Dead && A.AssignedBuilding == INVALID_ID)
		{
			A.AssignedBuilding = Building;
			B.AssignedWorkers += 1;
			return true;
		}
	}
	return false;   // no idle villager available
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
	const FVector& VisualScale)
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
	return Buildings.Add(B);
}

FAgentId FSimWorld::SpawnAgent(const FVector& Pos)
{
	FAgent A;
	A.Position = Pos;
	bEverHadAgents = true;
	return Agents.Add(A);
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
	for (const FAgent& A : Agents)
	{
		FAgentSnapshot Snap;
		Snap.Position      = A.Position;
		Snap.State         = A.State;
		Snap.CarriedAmount = A.CarriedAmount;
		Snap.CarriedType   = A.CarriedType;
		Snap.bAssigned     = A.AssignedBuilding != INVALID_ID;
		Snap.bStarving     = A.StarveTimer > 0.f;
		Out.Agents.Add(Snap);
		if (A.State != EAgentState::Dead)
		{
			++Out.Population;
			if (A.AssignedBuilding == INVALID_ID)
			{
				++Out.IdleVillagers;
			}
		}
	}

	Out.Buildings.Reset(Buildings.Num());
	Out.LogCount = Out.PlankCount = Out.FoodCount = 0;
	for (const FBuilding& B : Buildings)
	{
		Out.Buildings.Add({ B.Position, B.Type, B.AssignedWorkers, MaxWorkersFor(B.Type),
			B.VisualScale });
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

void FSimWorld::Serialize(FArchive& Ar)
{
	int32 Version = 5;   // v5: per-instance visual scale (FTree/FBuilding layout changed)
	Ar << Version;
	if (Ar.IsLoading() && Version != 5)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RealmSave] Sim save version %d unsupported (want 5); load skipped."), Version);
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
	SerializeArray(Agents);
	SerializeArray(Buildings);
	SerializeArray(Trees);
}
