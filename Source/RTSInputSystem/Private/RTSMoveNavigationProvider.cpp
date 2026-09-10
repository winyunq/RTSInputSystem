#include "RTSMoveNavigationProvider.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MassAPISubsystem.h"
#include "Fragments/Move.h"
#include "Fragments/Trace.h"
#include "Fragments/Transform.h"

namespace
{
	TMap<TWeakObjectPtr<UWorld>, IRTSMoveNavigationProvider*> Providers;

	void PruneInvalidWorlds()
	{
		for (auto It = Providers.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}
}

void FRTSMoveNavigationProviderRegistry::Register(
	UWorld* World,
	IRTSMoveNavigationProvider* Provider)
{
	check(IsInGameThread());
	if (!IsValid(World) || !Provider)
	{
		return;
	}
	PruneInvalidWorlds();
	Providers.Add(World, Provider);
}

void FRTSMoveNavigationProviderRegistry::Unregister(
	UWorld* World,
	IRTSMoveNavigationProvider* Provider)
{
	check(IsInGameThread());
	if (!World)
	{
		return;
	}
	if (IRTSMoveNavigationProvider** Existing = Providers.Find(World);
		Existing && *Existing == Provider)
	{
		Providers.Remove(World);
	}
}

bool FRTSMoveNavigationProviderRegistry::BuildBatches(
	UObject* WorldContext,
	const TArray<FEntityHandle>& Entities,
	const FVector& RequestedGoal,
	TArray<FRTSMoveNavigationBatch>& OutBatches,
	const ERTSMoveNavigationScale NavigationScale)
{
	check(IsInGameThread());
	OutBatches.Reset();
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(
			WorldContext,
			EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World || Entities.IsEmpty())
	{
		return false;
	}

	PruneInvalidWorlds();
	if (IRTSMoveNavigationProvider** Provider = Providers.Find(World))
	{
		return *Provider
			&& (*Provider)->BuildMoveNavigationBatches(
				Entities,
				RequestedGoal,
				OutBatches,
				NavigationScale);
	}

	// Generic RTSInput operation keeps MassBattle's stock Individual contract
	// when a project has not registered a navigation provider.
	FRTSMoveNavigationBatch& Batch = OutBatches.AddDefaulted_GetRef();
	Batch.Entities = Entities;
	Batch.Goal = RequestedGoal;
	Batch.Navigation.Mode = ENavMode::Individual;
	return true;
}

void FRTSMoveNavigationProviderRegistry::NotifyBatchActivated(
	UObject* WorldContext,
	const FRTSMoveNavigationBatch& Batch)
{
	check(IsInGameThread());
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(
			WorldContext,
			EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World)
	{
		return;
	}

	PruneInvalidWorlds();
	if (IRTSMoveNavigationProvider** Provider = Providers.Find(World);
		Provider && *Provider)
	{
		(*Provider)->OnMoveNavigationBatchActivated(Batch);
	}
}

void FRTSMoveNavigationProviderRegistry::BindChaseTaskNavigation(
	UObject* WorldContext,
	const TArray<FEntityHandle>& Entities,
	const FEntityHandle Target,
	const uint32 ChaseTaskID)
{
	check(IsInGameThread());
	UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(WorldContext);
	if (!MassAPI || ChaseTaskID == 0) return;
	const FLocating* TargetLocation = MassAPI->IsValid(Target)
		? MassAPI->GetFragmentPtr<FLocating>(Target) : nullptr;
	if (!TargetLocation) return;
	TArray<FEntityHandle> MovingEntities;
	for (const FEntityHandle& Entity : Entities)
	{
		if (!MassAPI->IsValid(Entity)) continue;
		const FMove* Move = MassAPI->GetFragmentPtr<FMove>(Entity);
		const FTracing* Tracing = MassAPI->GetFragmentPtr<FTracing>(Entity);
		if (Move && Move->bEnable && Tracing
			&& Tracing->ActiveChaseAttackTaskID == ChaseTaskID)
			MovingEntities.Add(Entity);
	}
	// Native Chase owns targeting and completion; moving weapons share the same
	// terrain steering as MoveTo. Stationary weapons keep their native attack.
	TArray<FRTSMoveNavigationBatch> Batches;
	if (!MovingEntities.IsEmpty() && BuildBatches(WorldContext, MovingEntities,
		TargetLocation->Location, Batches, ERTSMoveNavigationScale::Tactical))
	{
		for (const FRTSMoveNavigationBatch& Batch : Batches)
			NotifyBatchActivated(WorldContext, Batch);
	}
}

bool FRTSMoveNavigationProviderRegistry::ResolveInitialSpawnLocation(
	UObject* WorldContext,
	const UMassBattleAgentConfigDataAsset* AgentConfig,
	const FVector& RequestedLocation,
	const float FormationRadiusUU,
	FVector& OutLocation)
{
	check(IsInGameThread());
	OutLocation = RequestedLocation;
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(
			WorldContext,
			EGetWorldErrorMode::ReturnNull)
		: nullptr;
	if (!World || !AgentConfig)
	{
		return false;
	}

	PruneInvalidWorlds();
	if (IRTSMoveNavigationProvider** Provider = Providers.Find(World))
	{
		return *Provider
			&& (*Provider)->ResolveInitialSpawnLocation(
				AgentConfig,
				RequestedLocation,
				FMath::Max(0.0f, FormationRadiusUU),
				OutLocation);
	}

	// A project without a placement provider retains stock spawn semantics.
	return true;
}
