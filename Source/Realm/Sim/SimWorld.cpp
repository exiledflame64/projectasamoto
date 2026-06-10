// Copyright Asamoto.
// FSimWorld tick wiring. Phase 0 ships the tick loop and the movement step;
// the remaining phase bodies are stubs filled out during Phase 1.

#include "SimWorld.h"

namespace
{
	// Phase 1 tuning.
	constexpr float ChopDuration     = 1.5f;  // seconds of "chopping" per log
	constexpr float ArrivalTolerance = 2.0f;  // cm; movement snaps to target on arrival
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
void FSimWorld::Phase_Needs(float /*Dt*/)      { /* Phase 2 */ }

// Idle agents claim the nearest tree; arrivals advance the state machine.
// Claim-on-assignment / release-on-departure keeps the claim cycle honest even
// with a single agent.
void FSimWorld::Phase_JobAssignment()
{
	const bool bBuildingExists = HasBuilding(EBuildingType::Lumberyard);

	for (FAgent& A : Agents)
	{
		switch (A.State)
		{
		case EAgentState::Idle:
		{
			// A lumberyard must exist to emit the "chop wood" job.
			if (!bBuildingExists)
			{
				break;
			}
			const FTreeId T = FindNearestAvailableTree(A.Position);
			if (T != INVALID_ID)
			{
				Trees[T].bClaimed = true;
				A.TargetTree = T;
				A.Target     = Trees[T].Position;
				A.State      = EAgentState::MovingToWork;
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
				const FBuildingId S = FindNearestStorage(A.Position);
				if (S != INVALID_ID)
				{
					Buildings[S].Inventory += A.CarriedAmount;
				}
				A.CarriedAmount = 0;
				A.CarriedType   = EResource::None;
				A.State         = EAgentState::Idle;
			}
			break;
		}

		default:
			break;
		}
	}
}

// Working agents advance their chop timer; when it completes they take one log
// and release the tree, then head for storage.
void FSimWorld::Phase_Production(float Dt)
{
	for (FAgent& A : Agents)
	{
		if (A.State != EAgentState::Working)
		{
			continue;
		}

		A.WorkTimer += Dt;
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

		// Carry it to storage. With nowhere to deposit, the agent idles rather
		// than silently stalling mid-job (a production-chain deadlock guard).
		const FBuildingId S = FindNearestStorage(A.Position);
		if (S != INVALID_ID && A.CarriedAmount > 0)
		{
			A.Target = Buildings[S].Position;
			A.State  = EAgentState::MovingToStore;
		}
		else
		{
			A.State = EAgentState::Idle;
		}
	}
}

void FSimWorld::Phase_Movement(float Dt)
{
	// Phase 1: step agents toward Target along (eventually) a flow field. For now
	// a straight-line step is enough to exercise arrival detection.
	for (FAgent& A : Agents)
	{
		if (A.State == EAgentState::MovingToWork ||
			A.State == EAgentState::MovingToStore)
		{
			const FVector To   = A.Target - A.Position;
			const float   Dist = To.Size();
			const float   Step = A.Speed * Dt;
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
	return Agents.Add(A);
}

FTreeId FSimWorld::SpawnTree(const FVector& Pos)
{
	FTree T;
	T.Position = Pos;
	return Trees.Add(T);
}

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
	// Linear scan: fine at Phase 1 counts. A spatial index / priority queue
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

FBuildingId FSimWorld::FindNearestStorage(const FVector& From) const
{
	FBuildingId Best = INVALID_ID;
	float       BestDistSq = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Buildings.Num(); ++i)
	{
		if (Buildings[i].Type != EBuildingType::Storage)
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

void FSimWorld::BuildSnapshot(FSimSnapshot& Out) const
{
	Out.Agents.Reset(Agents.Num());
	for (const FAgent& A : Agents)
	{
		FAgentSnapshot Snap;
		Snap.Position      = A.Position;
		Snap.State         = A.State;
		Snap.CarriedAmount = A.CarriedAmount;
		Out.Agents.Add(Snap);
	}

	Out.Buildings.Reset(Buildings.Num());
	Out.StorageLogCount = 0;
	for (const FBuilding& B : Buildings)
	{
		Out.Buildings.Add({ B.Position, B.Type, B.Inventory });
		if (B.Type == EBuildingType::Storage)
		{
			Out.StorageLogCount += B.Inventory;
		}
	}

	Out.Trees.Reset(Trees.Num());
	for (const FTree& T : Trees)
	{
		Out.Trees.Add({ T.Position, T.Remaining });
	}

	Out.TickNumber = TickNumber;
}
