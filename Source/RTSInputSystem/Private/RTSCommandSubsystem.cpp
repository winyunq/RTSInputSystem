// Copyright 2024 Winy unq All Rights Reserved.

#include "RTSCommandSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "MassAPIFuncLib.h"
#include "MassBattleStructs.h"
#include "Tasks/MassBattleBPTaskAgentsMoveTo.h"
#include "Tasks/MassBattleBPTaskAgentsChaseAttack.h"
#include "FuncLibs/MassBattleFuncLib.h"
#include "Components/MassBattleAgentComponent.h"
#include "Fragments/Attack.h"
#include "Fragments/Move.h"
#include "Fragments/SubType.h"
#include "Fragments/Network.h"
#include "GameplayTagsManager.h"
#include "MassBattleEnums.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "RTSInputPanelSettings.h"
#include "RTSMoveNavigationProvider.h"
#include "RTSSelectionSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogRTSCommand, Log, All);

namespace
{
	const FName MoveTagName(TEXT("RTS.Command.Move"));
	const FName AttackTagName(TEXT("RTS.Command.Attack"));
	const FName StopTagName(TEXT("RTS.Command.Stop"));
	const FName HoldTagName(TEXT("RTS.Command.Hold"));
	const FName PatrolTagName(TEXT("RTS.Command.Patrol"));

	FGameplayTag GetCommandTag(const FName TagName)
	{
		return FGameplayTag::RequestGameplayTag(TagName, false);
	}

	const TCHAR* GetNavigationModeName(ENavMode Mode)
	{
		switch (Mode)
		{
		case ENavMode::Individual: return TEXT("Individual/AStar");
		case ENavMode::Direct: return TEXT("Direct");
		case ENavMode::FlowField: return TEXT("FlowField");
		case ENavMode::Custom: return TEXT("Custom");
		default: return TEXT("None");
		}
	}
}

void URTSCommandSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UGameplayTagsManager::Get().AddNativeGameplayTag(MoveTagName, TEXT("Default RTS unit move command"));
	UGameplayTagsManager::Get().AddNativeGameplayTag(AttackTagName, TEXT("Default RTS unit attack command"));
	UGameplayTagsManager::Get().AddNativeGameplayTag(StopTagName, TEXT("Default RTS unit stop command"));
	UGameplayTagsManager::Get().AddNativeGameplayTag(HoldTagName, TEXT("Default RTS unit hold command"));
	UGameplayTagsManager::Get().AddNativeGameplayTag(PatrolTagName, TEXT("Default RTS unit patrol command"));
}

void URTSCommandSubsystem::Deinitialize()
{
	QueuedLocationCommands.Reset();
	ActiveQueuedLocationEntities.Reset();
	ActiveCommandTags.Reset();
	Super::Deinitialize();
}

void URTSCommandSubsystem::IssueCommand(FGameplayTag Tag, AActor* /*Context*/)
{
	ExecuteCommand(Tag, GetSelectedMassEntities(), nullptr, nullptr, false);
}

void URTSCommandSubsystem::IssueCommandWithLocation(FGameplayTag Tag, const FVector& Location, bool bQueue)
{
	ExecuteCommand(Tag, GetSelectedMassEntities(), &Location, nullptr, bQueue);
}

void URTSCommandSubsystem::IssueCommandWithTarget(FGameplayTag Tag, AActor* TargetActor)
{
	ExecuteCommand(Tag, GetSelectedMassEntities(), nullptr, TargetActor, false);
}

TArray<FEntityHandle> URTSCommandSubsystem::GetSelectedMassEntities() const
{
	if (ULocalPlayer* LP = const_cast<URTSCommandSubsystem*>(this)->GetLocalPlayer())
	{
		if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
		{
			// Context commands are capability-composed across the whole selection.
			// Tab focus still controls the visible command card, but must not make a
			// selected building swallow a simultaneous infantry move.
			return Selection->GetControllableSelectedEntities();
		}
	}

	FMassBattleQuery Query;
	Query.BattleAllFlagsList.Reset();
	Query.BattleAllFlagsList.Add(EBattleFlags::Selected);

	const FEntityQuery EntityQuery = Query.ToEntityQuery();
	return UMassAPIFuncLib::GetMatchingEntities(this, EntityQuery);
}

TArray<FEntityHandle> URTSCommandSubsystem::FilterEntitiesForCommand(
	const TArray<FEntityHandle>& Entities,
	FGameplayTag Tag,
	bool bHasLocation,
	bool bHasTargetActor) const
{
	TArray<FEntityHandle> Result;
	if (!Tag.IsValid() || Entities.IsEmpty())
	{
		return Result;
	}

	const UWorld* World = GetWorld();
	const UMassEntitySubsystem* MassSubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!MassSubsystem)
	{
		return Result;
	}

	const FGameplayTag MoveTag = GetCommandTag(MoveTagName);
	const FGameplayTag AttackTag = GetCommandTag(AttackTagName);
	const FGameplayTag StopTag = GetCommandTag(StopTagName);
	const FGameplayTag HoldTag = GetCommandTag(HoldTagName);
	const FGameplayTag PatrolTag = GetCommandTag(PatrolTagName);
	const bool bMoveCommand = Tag == MoveTag || Tag == PatrolTag;
	const bool bAttackCommand = Tag == AttackTag;
	const bool bStopCommand = Tag == StopTag || Tag == HoldTag;

	const FMassEntityManager& EntityManager = MassSubsystem->GetEntityManager();
	const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();
	Result.Reserve(Entities.Num());

	for (const FEntityHandle& Entity : Entities)
	{
		const FMassEntityHandle NativeEntity = Entity;
		if (!EntityManager.IsEntityActive(NativeEntity))
		{
			continue;
		}

		// An authored command card is also the authoritative capability declaration.
		// This keeps production structures out of Move while allowing mobile units in
		// the same selection to receive the very same right-click.
		if (const FSubType* SubType = EntityManager.GetFragmentDataPtr<FSubType>(NativeEntity))
		{
			const FNetworking* Networking = EntityManager.GetFragmentDataPtr<FNetworking>(NativeEntity);
			if (const FRTSMassUnitTypeProtocol* Protocol =
				RTSUnitTypeProtocol::FindByNetworkKeyOrSubType(
					Settings,
					Networking ? Networking->Key : NAME_None,
					SubType->Index))
			{
				if (!RTSUnitTypeProtocol::ExposesCommandButton(Settings, *Protocol, Tag))
				{
					continue;
				}
			}
		}

		const FMove* Move = EntityManager.GetFragmentDataPtr<FMove>(NativeEntity);
		const FAttack* Attack = EntityManager.GetFragmentDataPtr<FAttack>(NativeEntity);
		bool bCompatible = true;
		if (bMoveCommand)
		{
			bCompatible = bHasLocation && Move && Move->bEnable;
		}
		else if (bAttackCommand)
		{
			// A ground attack is the default attack-move operation and therefore
			// requires locomotion. A concrete target only requires attack capability.
			bCompatible = bHasTargetActor
				? Attack && Attack->bEnable
				: bHasLocation && Move && Move->bEnable && Attack && Attack->bEnable;
		}
		else if (bStopCommand)
		{
			bCompatible = (Move && Move->bEnable) || (Attack && Attack->bEnable);
		}

		if (bCompatible)
		{
			Result.Add(Entity);
		}
	}

	return Result;
}

FGameplayTag URTSCommandSubsystem::GetActiveCommandTag(const TArray<FEntityHandle>& Entities) const
{
	FGameplayTag CommonTag;
	bool bHasCommonTag = false;

	for (const FEntityHandle& Entity : Entities)
	{
		const FGameplayTag EntityTag = ResolveEntityCommandTag(Entity);
		if (!EntityTag.IsValid())
		{
			continue;
		}

		if (!bHasCommonTag)
		{
			CommonTag = EntityTag;
			bHasCommonTag = true;
		}
		else if (CommonTag != EntityTag)
		{
			return FGameplayTag::EmptyTag;
		}
	}

	return bHasCommonTag ? CommonTag : FGameplayTag::EmptyTag;
}

FGameplayTag URTSCommandSubsystem::ResolveEntityCommandTag(const FEntityHandle& Entity) const
{
	const FGameplayTag MoveTag = GetCommandTag(MoveTagName);
	const FGameplayTag AttackTag = GetCommandTag(AttackTagName);
	const FGameplayTag StopTag = GetCommandTag(StopTagName);
	const FGameplayTag PatrolTag = GetCommandTag(PatrolTagName);

	const UWorld* World = GetWorld();
	const UMassEntitySubsystem* MassSubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!MassSubsystem)
	{
		return FGameplayTag::EmptyTag;
	}

	const FMassEntityManager& EntityManager = MassSubsystem->GetEntityManager();
	const FMassEntityHandle NativeEntity = Entity;
	if (!EntityManager.IsEntityActive(NativeEntity))
	{
		return FGameplayTag::EmptyTag;
	}

	const FEntityFlagFragment* Flags = EntityManager.GetFragmentDataPtr<FEntityFlagFragment>(NativeEntity);
	const FMoving* Moving = EntityManager.GetFragmentDataPtr<FMoving>(NativeEntity);
	const bool bMoving = Moving && Moving->bMovingToGoal;
	const bool bAttacking = Flags
		&& (Flags->HasFlag(static_cast<EEntityFlags>(EBattleFlags::Attacking))
			|| Flags->HasFlag(static_cast<EEntityFlags>(EBattleFlags::Chasing))
			|| Flags->HasFlag(static_cast<EEntityFlags>(EBattleFlags::BPTask_ChaseAttack)));
	const bool bPatrolling = Flags
		&& Flags->HasFlag(static_cast<EEntityFlags>(EBattleFlags::Patrolling));

	if (bAttacking)
	{
		return AttackTag;
	}
	if (bPatrolling)
	{
		return PatrolTag;
	}

	if (const FGameplayTag* RecordedTag = ActiveCommandTags.Find(Entity))
	{
		if (*RecordedTag == MoveTag || *RecordedTag == AttackTag || *RecordedTag == PatrolTag)
		{
			return bMoving ? *RecordedTag : StopTag;
		}
		return *RecordedTag;
	}

	return bMoving ? MoveTag : StopTag;
}

void URTSCommandSubsystem::RecordCommandTag(const TArray<FEntityHandle>& Entities, FGameplayTag Tag)
{
	for (const FEntityHandle& Entity : Entities)
	{
		if (UMassAPIFuncLib::IsValid(this, Entity))
		{
			ActiveCommandTags.FindOrAdd(Entity) = Tag;
		}
	}

	if (ActiveCommandTags.Num() > 4096)
	{
		for (auto It = ActiveCommandTags.CreateIterator(); It; ++It)
		{
			if (!UMassAPIFuncLib::IsValid(this, It.Key()))
			{
				It.RemoveCurrent();
			}
		}
	}
}

void URTSCommandSubsystem::ExecuteCommand(
	FGameplayTag Tag,
	const TArray<FEntityHandle>& SelectedEntities,
	const FVector* Location,
	AActor* TargetActor,
	bool bQueue
)
{
	if (!Tag.IsValid() || SelectedEntities.Num() == 0)
	{
		return;
	}

	const FGameplayTag MoveTag = GetCommandTag(MoveTagName);
	const FGameplayTag AttackTag = GetCommandTag(AttackTagName);
	const FGameplayTag StopTag = GetCommandTag(StopTagName);
	const FGameplayTag HoldTag = GetCommandTag(HoldTagName);
	const FGameplayTag PatrolTag = GetCommandTag(PatrolTagName);
	const TArray<FEntityHandle> CompatibleEntities =
		FilterEntitiesForCommand(SelectedEntities, Tag, Location != nullptr, TargetActor != nullptr);
	if (CompatibleEntities.IsEmpty())
	{
		return;
	}

	if (!bQueue)
	{
		ClearQueuedLocationCommands(CompatibleEntities);
	}

	if (Tag == MoveTag || Tag == PatrolTag)
	{
		if (!Location) return;
		if (bQueue)
		{
			QueueLocationCommand(Tag, CompatibleEntities, *Location, false);
			return;
		}
		if (IssueMoveTo(CompatibleEntities, *Location, /*bCanInterrupt*/ false))
		{
			RecordCommandTag(CompatibleEntities, Tag);
		}
		return;
	}

	if (Tag == AttackTag)
	{
		if (TargetActor)
		{
			if (IssueAttackTarget(CompatibleEntities, TargetActor))
			{
				RecordCommandTag(CompatibleEntities, Tag);
			}
			return;
		}

		if (Location && bQueue)
		{
			QueueLocationCommand(Tag, CompatibleEntities, *Location, true);
			return;
		}

		if (Location && IssueMoveTo(CompatibleEntities, *Location, /*bCanInterrupt*/ true))
		{
			RecordCommandTag(CompatibleEntities, Tag);
		}
		return;
	}

	if (Tag == StopTag || Tag == HoldTag)
	{
		UMassBattleFuncLib::StopAgentsAllMovement(this, CompatibleEntities);
		RecordCommandTag(CompatibleEntities, Tag);
		return;
	}
}

bool URTSCommandSubsystem::IsEntityMoving(const FEntityHandle& Entity) const
{
	const UWorld* World = GetWorld();
	const UMassEntitySubsystem* MassSubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!MassSubsystem)
	{
		return false;
	}

	const FMassEntityManager& EntityManager = MassSubsystem->GetEntityManager();
	const FMassEntityHandle NativeEntity = Entity;
	if (!EntityManager.IsEntityActive(NativeEntity))
	{
		return false;
	}

	const FMoving* Moving = EntityManager.GetFragmentDataPtr<FMoving>(NativeEntity);
	return Moving && Moving->bMovingToGoal;
}

void URTSCommandSubsystem::QueueLocationCommand(
	FGameplayTag Tag,
	const TArray<FEntityHandle>& SelectedEntities,
	const FVector& Location,
	bool bCanInterrupt)
{
	constexpr int32 MaxQueuedCommandsPerEntity = 64;
	for (const FEntityHandle& Entity : SelectedEntities)
	{
		if (!UMassAPIFuncLib::IsValid(this, Entity))
		{
			continue;
		}

		FQueuedLocationCommand Order;
		Order.Tag = Tag;
		Order.Location = Location;
		Order.bCanInterrupt = bCanInterrupt;

		if (ActiveQueuedLocationEntities.Contains(Entity) || IsEntityMoving(Entity))
		{
			TArray<FQueuedLocationCommand>& Queue = QueuedLocationCommands.FindOrAdd(Entity);
			if (Queue.Num() < MaxQueuedCommandsPerEntity)
			{
				Queue.Add(MoveTemp(Order));
			}
			ActiveQueuedLocationEntities.Add(Entity);
			continue;
		}

		const TArray<FEntityHandle> SingleEntity{Entity};
		if (IssueMoveTo(SingleEntity, Location, bCanInterrupt))
		{
			ActiveQueuedLocationEntities.Add(Entity);
			RecordCommandTag(SingleEntity, Tag);
		}
	}

}

void URTSCommandSubsystem::ClearQueuedLocationCommands(const TArray<FEntityHandle>& Entities)
{
	for (const FEntityHandle& Entity : Entities)
	{
		QueuedLocationCommands.Remove(Entity);
		ActiveQueuedLocationEntities.Remove(Entity);
	}

}

void URTSCommandSubsystem::AdvanceQueuedLocationCommands(
	const TArray<FEntityHandle>& Entities)
{
	for (const FEntityHandle& Entity : Entities)
	{
		ActiveQueuedLocationEntities.Remove(Entity);
		if (!UMassAPIFuncLib::IsValid(this, Entity))
		{
			QueuedLocationCommands.Remove(Entity);
			ActiveCommandTags.Remove(Entity);
			continue;
		}

		TArray<FQueuedLocationCommand>* Queue = QueuedLocationCommands.Find(Entity);
		if (!Queue || Queue->IsEmpty())
		{
			QueuedLocationCommands.Remove(Entity);
			continue;
		}

		const FQueuedLocationCommand NextOrder = (*Queue)[0];
		Queue->RemoveAt(0);
		if (Queue->IsEmpty())
		{
			QueuedLocationCommands.Remove(Entity);
		}

		const TArray<FEntityHandle> SingleEntity{Entity};
		if (IssueMoveTo(SingleEntity, NextOrder.Location, NextOrder.bCanInterrupt))
		{
			RecordCommandTag(SingleEntity, NextOrder.Tag);
		}
		else
		{
			ActiveQueuedLocationEntities.Remove(Entity);
		}
	}
}

void URTSCommandSubsystem::HandleMoveTaskResolved(
	const TArray<FEntityHandle>& Entities)
{
	AdvanceQueuedLocationCommands(Entities);
}

void URTSCommandSubsystem::HandleMoveTaskTransferred(
	const TArray<FEntityHandle>& Entities)
{
	// A replacement task owns these entities now. Queued orders attached to the
	// superseded task must not leak into that unrelated movement chain.
	ClearQueuedLocationCommands(Entities);
}

bool URTSCommandSubsystem::IssueMoveTo(const TArray<FEntityHandle>& SelectedEntities, const FVector& Location, bool bCanInterrupt)
{
	TArray<FRTSMoveNavigationBatch> Batches;
	if (!FRTSMoveNavigationProviderRegistry::BuildBatches(
		this,
		SelectedEntities,
		Location,
		Batches))
	{
		UE_LOG(
			LogRTSCommand,
			Warning,
			TEXT("Move command rejected: no navigation batch for %d units."),
			SelectedEntities.Num());
		return false;
	}

	bool bActivatedAny = false;
	for (const FRTSMoveNavigationBatch& Batch : Batches)
	{
		if (Batch.Entities.IsEmpty())
		{
			continue;
		}
		FMBMoveGoal Goal;
		Goal.GoalType = EMBMoveGoalType::Location;
		Goal.Locations.Add(Batch.Goal);
		UE_LOG(
			LogRTSCommand,
			Log,
			TEXT("Move command batch: Units=%d Navigation=%s Layer=%d"),
			Batch.Entities.Num(),
			GetNavigationModeName(Batch.Navigation.Mode),
			Batch.Navigation.FlowFieldLayer.LayerID);
		if (UMassBattleBPTaskAgentsMoveTo* MoveTask =
			UMassBattleBPTaskAgentsMoveTo::AgentsMoveTo(
				this,
				Batch.Entities,
				Goal,
				Batch.Navigation,
				FMBMoveFailCondition(),
				bCanInterrupt))
		{
			MoveTask->OnAgentSuccess.AddDynamic(
				this,
				&URTSCommandSubsystem::HandleMoveTaskResolved);
			MoveTask->OnAgentFail.AddDynamic(
				this,
				&URTSCommandSubsystem::HandleMoveTaskResolved);
			MoveTask->OnAgentTransfer.AddDynamic(
				this,
				&URTSCommandSubsystem::HandleMoveTaskTransferred);
			MoveTask->Activate();
			for (const FEntityHandle& Entity : Batch.Entities)
			{
				ActiveQueuedLocationEntities.Add(Entity);
			}
			bActivatedAny = true;
		}
	}
	return bActivatedAny;
}

bool URTSCommandSubsystem::IssueAttackTarget(const TArray<FEntityHandle>& SelectedEntities, AActor* TargetActor)
{
	FEntityHandle TargetHandle;
	if (!TargetActor) return false;

	if (const UMassBattleAgentComponent* TargetComp = TargetActor->FindComponentByClass<UMassBattleAgentComponent>())
	{
		TargetHandle = TargetComp->GetEntityHandle();
	}

	if (!UMassAPIFuncLib::IsValid(this, TargetHandle))
	{
		return false;
	}

	if (UMassBattleBPTaskAgentsChaseAttack* ChaseTask = UMassBattleBPTaskAgentsChaseAttack::AgentsChaseAttack(
		this,
		SelectedEntities,
		TargetHandle,
		false,
		6.0f,
		100.0f
	))
	{
		ChaseTask->Activate();
		return true;
	}

	return false;
}
