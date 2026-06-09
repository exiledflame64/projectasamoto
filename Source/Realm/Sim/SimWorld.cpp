// Copyright Asamoto.
// FSimWorld tick wiring. Phase 0 ships the tick loop and the movement step;
// the remaining phase bodies are stubs filled out during Phase 1.

#include "SimWorld.h"

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
void FSimWorld::Phase_JobAssignment()          { /* Phase 1: claim/release */ }
void FSimWorld::Phase_Production(float /*Dt*/)  { /* Phase 1: woodcutter timer */ }

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

void FSimWorld::BuildSnapshot(FSimSnapshot& Out) const
{
	Out.Agents.Reset(Agents.Num());
	for (const FAgent& A : Agents)
	{
		FAgentSnapshot Snap;
		Snap.Position = A.Position;
		Snap.State    = A.State;
		Out.Agents.Add(Snap);
	}

	Out.StorageLogCount = 0;
	for (const FBuilding& B : Buildings)
	{
		if (B.Type == EBuildingType::Storage)
		{
			Out.StorageLogCount += B.Inventory;
		}
	}

	Out.TickNumber = TickNumber;
}
