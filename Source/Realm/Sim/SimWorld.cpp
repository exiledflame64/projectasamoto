// Copyright Asamoto.
// FSimWorld tick wiring. Phase 2: economy spine — sawmill chain (logs->planks)
// with hauling logistics, farm food production, hunger/starvation, lose state.

#include "SimWorld.h"

namespace
{
	// Phase 2 tuning.
	constexpr float ChopDuration     = 1.5f;   // seconds of "chopping" per log
	constexpr float SawDuration      = 3.0f;   // seconds per log -> plank
	constexpr float FarmDuration     = 10.0f;  // seconds per food grown
	constexpr int32 SawmillInputCap  = 5;      // logs buffered at the sawmill
	constexpr int32 ProducerOutputCap = 5;     // output buffer before production stalls
	constexpr float EatIntervalSeconds = 20.f; // each villager eats 1 food this often
	constexpr float StarveSeconds      = 60.f; // unfed past a due meal this long = death
	constexpr float StarvingSpeedFactor = 0.2f; // unfed agents work/move at 1/5 speed

	float StarvingMult(const FAgent& A) { return A.StarveTimer > 0.f ? StarvingSpeedFactor : 1.f; }
	constexpr float ArrivalTolerance = 2.0f;   // cm; movement snaps to target on arrival

	constexpr int32 ResIdx(EResource R) { return static_cast<int32>(R); }
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

// Idle agents pick jobs in priority order; arrivals advance the state machine.
// Priorities: empty producer outputs > feed the sawmill > chop. A real
// urgency-sorted queue replaces this ordered scan when counts grow
// (baoding.md "Three hardest problems" #1).
void FSimWorld::Phase_JobAssignment()
{
	const bool bLumberyardExists = HasBuilding(EBuildingType::Lumberyard);

	for (FAgent& A : Agents)
	{
		switch (A.State)
		{
		case EAgentState::Idle:
		{
			if (TryClaimHaulOutput(A))                   break;
			if (TryClaimFeedSawmill(A))                  break;
			if (TryClaimChop(A, bLumberyardExists))      break;
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
				}
				A.CarriedAmount = 0;
				A.CarriedType   = EResource::None;
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
				FBuilding& B = Buildings[A.PickupFrom];
				const EResource Out = (B.Type == EBuildingType::Sawmill)
					? EResource::Plank : EResource::Food;

				B.bOutputClaimed = false;   // claim released at pickup
				if (B.Stored[ResIdx(Out)] > 0)
				{
					B.Stored[ResIdx(Out)] -= 1;
					A.CarriedAmount = 1;
					A.CarriedType   = Out;

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
			}
			A.CarriedAmount = 0;
			A.CarriedType   = EResource::None;
			FinishJob(A);
			break;
		}

		default:
			break;
		}
	}
}

// Working agents advance their chop timer; buildings run their own production.
void FSimWorld::Phase_Production(float Dt)
{
	for (FAgent& A : Agents)
	{
		if (A.State != EAgentState::Working)
		{
			continue;
		}

		A.WorkTimer += Dt * StarvingMult(A);   // starving villagers chop at 1/5 pace
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
		A.WorkTimer  = 0.f;

		// Carry it to the warehouse. With nowhere to deposit, the agent idles
		// rather than silently stalling mid-job (a production-chain deadlock guard).
		const FBuildingId S = FindNearestWarehouse(A.Position);
		if (S != INVALID_ID && A.CarriedAmount > 0)
		{
			A.Target = Buildings[S].Position;
			A.State  = EAgentState::MovingToStore;
		}
		else
		{
			FinishJob(A);
		}
	}

	for (FBuilding& B : Buildings)
	{
		switch (B.Type)
		{
		case EBuildingType::Sawmill:
		{
			// Stalls visibly when fed nothing or when the output buffer is full —
			// haulers clearing planks / bringing logs un-stall it (explicit
			// blocked-queue handling, baoding.md hardest problem #2).
			if (B.Stored[ResIdx(EResource::Log)] > 0 &&
				B.Stored[ResIdx(EResource::Plank)] < ProducerOutputCap)
			{
				B.WorkTimer += Dt;
				if (B.WorkTimer >= SawDuration)
				{
					B.WorkTimer = 0.f;
					B.Stored[ResIdx(EResource::Log)]   -= 1;
					B.Stored[ResIdx(EResource::Plank)] += 1;
				}
			}
			break;
		}

		case EBuildingType::Farm:
		{
			if (B.Stored[ResIdx(EResource::Food)] < ProducerOutputCap)
			{
				B.WorkTimer += Dt;
				if (B.WorkTimer >= FarmDuration)
				{
					B.WorkTimer = 0.f;
					B.Stored[ResIdx(EResource::Food)] += 1;
				}
			}
			break;
		}

		default:
			break;
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

// --- Job claiming ---

bool FSimWorld::TryClaimHaulOutput(FAgent& A)
{
	FBuildingId Best = INVALID_ID;
	float BestDistSq = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Buildings.Num(); ++i)
	{
		const FBuilding& B = Buildings[i];
		const bool bHasOutput =
			(B.Type == EBuildingType::Sawmill && B.Stored[ResIdx(EResource::Plank)] > 0) ||
			(B.Type == EBuildingType::Farm    && B.Stored[ResIdx(EResource::Food)]  > 0);
		if (!bHasOutput || B.bOutputClaimed)
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(A.Position, B.Position);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = i;
		}
	}
	if (Best == INVALID_ID || FindNearestWarehouse(A.Position) == INVALID_ID)
	{
		return false;
	}

	Buildings[Best].bOutputClaimed = true;
	A.Job        = EJobKind::HaulOutput;
	A.PickupFrom = Best;
	A.Target     = Buildings[Best].Position;
	A.State      = EAgentState::MovingToPickup;
	return true;
}

bool FSimWorld::TryClaimFeedSawmill(FAgent& A)
{
	FBuildingId Mill = INVALID_ID;
	float BestDistSq = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Buildings.Num(); ++i)
	{
		const FBuilding& B = Buildings[i];
		if (B.Type != EBuildingType::Sawmill || B.bInputClaimed ||
			B.Stored[ResIdx(EResource::Log)] >= SawmillInputCap)
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(A.Position, B.Position);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Mill = i;
		}
	}
	if (Mill == INVALID_ID)
	{
		return false;
	}
	const FBuildingId Source = FindNearestWarehouseWith(A.Position, EResource::Log);
	if (Source == INVALID_ID)
	{
		return false;
	}

	Buildings[Mill].bInputClaimed = true;
	A.Job        = EJobKind::FeedSawmill;
	A.PickupFrom = Source;
	A.DeliverTo  = Mill;
	A.Target     = Buildings[Source].Position;
	A.State      = EAgentState::MovingToPickup;
	return true;
}

bool FSimWorld::TryClaimChop(FAgent& A, bool bLumberyardExists)
{
	// A lumberyard must exist to emit the "chop wood" job.
	if (!bLumberyardExists)
	{
		return false;
	}
	const FTreeId T = FindNearestAvailableTree(A.Position);
	if (T == INVALID_ID)
	{
		return false;
	}
	Trees[T].bClaimed = true;
	A.Job        = EJobKind::Chop;
	A.TargetTree = T;
	A.Target     = Trees[T].Position;
	A.State      = EAgentState::MovingToWork;
	return true;
}

void FSimWorld::FinishJob(FAgent& A)
{
	A.Job        = EJobKind::None;
	A.PickupFrom = INVALID_ID;
	A.DeliverTo  = INVALID_ID;
	A.State      = EAgentState::Idle;
}

void FSimWorld::KillAgent(FAgent& A)
{
	// Release everything this agent held so the economy doesn't deadlock on a
	// corpse's claims. Whatever it carried is lost.
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
	A.PickupFrom    = INVALID_ID;
	A.DeliverTo     = INVALID_ID;
	A.Job           = EJobKind::None;
	A.CarriedAmount = 0;
	A.CarriedType   = EResource::None;
	A.State         = EAgentState::Dead;
}

// --- Commands ---

FBuildingId FSimWorld::PlaceBuilding(EBuildingType Type, const FVector& Pos)
{
	FBuilding B;
	B.Type     = Type;
	B.Position = Pos;
	return Buildings.Add(B);
}

FAgentId FSimWorld::SpawnAgent(const FVector& Pos)
{
	FAgent A;
	A.Position = Pos;
	bEverHadAgents = true;
	return Agents.Add(A);
}

FTreeId FSimWorld::SpawnTree(const FVector& Pos)
{
	FTree T;
	T.Position = Pos;
	return Trees.Add(T);
}

void FSimWorld::AddResource(FBuildingId Building, EResource Resource, int32 Amount)
{
	if (Buildings.IsValidIndex(Building) && Resource != EResource::None)
	{
		Buildings[Building].Stored[ResIdx(Resource)] += Amount;
	}
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
	// Linear scan: fine at Phase 2 counts. A spatial index / priority queue
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
	Out.Population = 0;
	for (const FAgent& A : Agents)
	{
		FAgentSnapshot Snap;
		Snap.Position      = A.Position;
		Snap.State         = A.State;
		Snap.CarriedAmount = A.CarriedAmount;
		Snap.CarriedType   = A.CarriedType;
		Snap.bStarving     = A.StarveTimer > 0.f;
		Out.Agents.Add(Snap);
		if (A.State != EAgentState::Dead)
		{
			++Out.Population;
		}
	}

	Out.Buildings.Reset(Buildings.Num());
	Out.LogCount = Out.PlankCount = Out.FoodCount = 0;
	for (const FBuilding& B : Buildings)
	{
		Out.Buildings.Add({ B.Position, B.Type });
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
		Out.Trees.Add({ T.Position, T.Remaining });
	}

	Out.bGameOver  = bGameOver;
	Out.TickNumber = TickNumber;
}

// --- Save/load ---

void FSimWorld::Serialize(FArchive& Ar)
{
	int32 Version = 3;   // v3: abstract food need (FAgent layout changed)
	Ar << Version;
	if (Ar.IsLoading() && Version != 3)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RealmSave] Sim save version %d unsupported (want 3); load skipped."), Version);
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
