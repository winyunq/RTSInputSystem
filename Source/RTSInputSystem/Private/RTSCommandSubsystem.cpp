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
#include "Fragments/Select.h"
#include "Fragments/SubType.h"
#include "Fragments/Network.h"
#include "Fragments/Transform.h"
#include "GameplayTagsManager.h"
#include "GameFramework/PlayerController.h"
#include "MassBattleEnums.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "RTSInputPanelSettings.h"
#include "RTSMoveNavigationProvider.h"
#include "RTSSelectionSubsystem.h"

#include "Subsystems/MassBattleSubsystem.h"
#include "Subsystems/MassBattleNetworkSubsystem.h"
#include "Network/MassBattleNetworkPlayerControllerBase.h"
#include "Network/MassBattleNetworkGameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GenericTeamAgentInterface.h"
#include "Fragments/Team.h"

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
	OwnedMoveTaskIds.Reset();
	CommandGenerations.Reset();
	PendingAdvances.Reset();
	QueuedLocationCommands.Reset();
	ActiveQueuedLocationEntities.Reset();
	ActiveCommandTags.Reset();
	Super::Deinitialize();
}

void URTSCommandSubsystem::IssueCommand(FGameplayTag Tag, AActor* /*Context*/)
{
	ExecuteCommand(Tag, GetSelectedMassEntities(), nullptr, FEntityHandle(), false);
}

void URTSCommandSubsystem::IssueCommandWithLocation(
	FGameplayTag Tag,
	const FVector& Location,
	bool bQueue,
	bool bForceStrategicNavigation)
{
	ExecuteCommand(
		Tag,
		GetSelectedMassEntities(),
		&Location,
		FEntityHandle(),
		bQueue,
		bForceStrategicNavigation
			? ERTSMoveNavigationScale::Strategic
			: ERTSMoveNavigationScale::Auto);
}

void URTSCommandSubsystem::IssueCommandWithTarget(FGameplayTag Tag, AActor* TargetActor)
{
	FEntityHandle Target;
	if (TargetActor)
	{
		if (const auto* Component = TargetActor->FindComponentByClass<UMassBattleAgentComponent>())
		{
			Target = Component->GetEntityHandle();
		}
	}
	ExecuteCommand(Tag, GetSelectedMassEntities(), nullptr, Target, false);
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

FName URTSCommandSubsystem::GetInputCommandTag() { return TEXT("RTS.Input.UnitOrder"); }
FName URTSCommandSubsystem::GetAdvanceCommandTag() { return TEXT("RTS.Internal.AdvanceOrder"); }
bool URTSCommandSubsystem::IsRTSLockstepCommand(FName Tag)
{
    return Tag == GetInputCommandTag() || Tag == GetAdvanceCommandTag();
}

void URTSCommandSubsystem::SubmitLockstepCommand(FGameplayTag Tag,
    const TArray<FEntityHandle>& Entities, const FVector* Location,
    FEntityHandle Target, bool bQueue, ERTSMoveNavigationScale Scale)
{
    APlayerController* Controller = GetLocalPlayer()->GetPlayerController(GetWorld());
    auto* NetworkController = Cast<AMassBattleNetworkPlayerControllerBase>(Controller);
    auto* Network = UMassBattleNetworkSubsystem::GetPtr(this);
    if (!NetworkController || !Network || !Tag.IsValid() || (Location && Location->ContainsNaN())) return;
    const int32 TargetId = UMassAPIFuncLib::IsValid(this, Target)
        ? Network->LookupUniqueIDByEntity(Target) : INDEX_NONE;
    TArray<int32> Ids;
    for (const FEntityHandle& Entity : Entities)
    {
        const int32 Id = Network->LookupUniqueIDByEntity(Entity);
        if (Id >= 0) Ids.AddUnique(Id);
    }
    if (Ids.IsEmpty()) return;
    Ids.Sort();
    // Version, queue, navigation scale, target UID, followed by selected UIDs.
    TArray<int32> Payload{1, bQueue ? 1 : 0, static_cast<int32>(Scale), TargetId};
    Payload.Append(Ids);
    TArray<float> Coordinates;
    if (Location) Coordinates = {static_cast<float>(Location->X), static_cast<float>(Location->Y), static_cast<float>(Location->Z)};
    // The uplink is broadcast at the next flush boundary. Reserve that boundary
    // so even a zero-ping player's order arrives before its execution tick.
    NetworkController->SendLockstepCommandToServer(GetInputCommandTag(), {Tag.GetTagName()},
        Payload, Coordinates, FMath::Max(1, Network->NetworkFlushIntervalTicks));
    UE_LOG(LogRTSCommand, Log, TEXT("RTS order submitted: Tag=%s Units=%d"), *Tag.ToString(), Ids.Num());
}

void URTSCommandSubsystem::ExecuteLockstepCommand(const FMassBattleNetCommand& Command)
{
    auto* Network = UMassBattleNetworkSubsystem::GetPtr(this);
    if (!Network || !Network->IsInCommandExecution() || Command.DataBlockId >= 0) return;
    if (Command.CommandTag == GetAdvanceCommandTag())
    {
        // Server-generated commands have the framework's default player index.
        // This internal tag is deliberately absent from the client allow list.
        if (Command.PlayerIndex != 0 || Command.IntPayload.Num() % 2 != 0) return;
        for (int32 Index = 0; Index < Command.IntPayload.Num(); Index += 2)
        {
            const FEntityHandle Entity = Network->LookupEntityByUniqueID(Command.IntPayload[Index]);
            if (CommandGenerations.FindRef(Entity) == Command.IntPayload[Index + 1])
            {
                PendingAdvances.Remove(Entity);
                AdvanceQueuedLocationCommands({Entity});
            }
        }
        return;
    }
    if (Command.CommandTag != GetInputCommandTag() || Command.NamePayload.Num() != 1
        || Command.IntPayload.Num() < 5 || Command.IntPayload[0] != 1
        || (Command.IntPayload[1] != 0 && Command.IntPayload[1] != 1)
        || Command.IntPayload[2] < static_cast<int32>(ERTSMoveNavigationScale::Auto)
        || Command.IntPayload[2] > static_cast<int32>(ERTSMoveNavigationScale::Strategic)
        || (Command.FloatPayload.Num() != 0 && Command.FloatPayload.Num() != 3)) return;
    const FGameplayTag Tag = GetCommandTag(Command.NamePayload[0]);
    if (Tag != GetCommandTag(MoveTagName) && Tag != GetCommandTag(AttackTagName)
        && Tag != GetCommandTag(StopTagName) && Tag != GetCommandTag(HoldTagName)
        && Tag != GetCommandTag(PatrolTagName)) return;
    int32 TeamId = INDEX_NONE;
    if (const AGameStateBase* State = GetWorld()->GetGameState())
    {
        for (const APlayerState* Player : State->PlayerArray)
        {
            if (!Player || Player->GetPlayerId() != Command.PlayerIndex) continue;
            if (const auto* Team = Cast<IGenericTeamAgentInterface>(Player))
            {
                if (Team->GetGenericTeamId() != FGenericTeamId::NoTeam)
                    TeamId = Team->GetGenericTeamId().GetId();
            }
            break;
        }
    }
    auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
    if (TeamId == INDEX_NONE || !Mass) return;
    const FMassEntityManager& Manager = Mass->GetEntityManager();
    TArray<FEntityHandle> Entities;
    for (int32 Index = 4; Index < Command.IntPayload.Num(); ++Index)
    {
        const FEntityHandle Entity = Network->LookupEntityByUniqueID(Command.IntPayload[Index]);
        if (!Manager.IsEntityActive(Entity)) continue;
        const FTeam* Team = Manager.GetFragmentDataPtr<FTeam>(Entity);
        const FSelect* Select = Manager.GetFragmentDataPtr<FSelect>(Entity);
        if (Team && Team->index == TeamId && (!Select || Select->bEnable)) Entities.AddUnique(Entity);
    }
    FVector Location = FVector::ZeroVector;
    if (Command.FloatPayload.Num() == 3)
    {
        Location = FVector(Command.FloatPayload[0], Command.FloatPayload[1], Command.FloatPayload[2]);
        if (Location.ContainsNaN()) return;
    }
    const FEntityHandle Target = Network->LookupEntityByUniqueID(Command.IntPayload[3]);
    UE_LOG(LogRTSCommand, Log, TEXT("RTS order executing: Tick=%d Tag=%s Units=%d"),
        Command.ExecuteTick, *Tag.ToString(), Entities.Num());
    ExecuteCommand(Tag, Entities, Command.FloatPayload.Num() == 3 ? &Location : nullptr,
        Target, Command.IntPayload[1] != 0, static_cast<ERTSMoveNavigationScale>(Command.IntPayload[2]));
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
		bool bAuthoredAttackCapability = false;
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
				bAuthoredAttackCapability = RTSUnitTypeProtocol::ExposesCommandButton(Settings, *Protocol, AttackTag);
			}
		}

		const FMove* Move = EntityManager.GetFragmentDataPtr<FMove>(NativeEntity);
		const FAttack* Attack = EntityManager.GetFragmentDataPtr<FAttack>(NativeEntity);
		// The authored card also covers existing external weapon executors, such as
		// mobile turrets whose native Attack is disabled to prevent double damage.
		const bool bCanAttack = bAuthoredAttackCapability || (Attack && Attack->bEnable);
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
				? bCanAttack
				: bHasLocation && Move && Move->bEnable && bCanAttack;
		}
		else if (bStopCommand)
		{
			bCompatible = (Move && Move->bEnable) || bCanAttack;
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
	FEntityHandle TargetHandle,
	bool bQueue,
	ERTSMoveNavigationScale NavigationScale
)
{
	if (!Tag.IsValid() || SelectedEntities.Num() == 0)
	{
		return;
	}

	const auto* Battle = UMassBattleSubsystem::GetPtr(this);
	const auto* Network = UMassBattleNetworkSubsystem::GetPtr(this);
	if (Battle && Battle->bNetworkedMode && (!Network || !Network->IsInCommandExecution()))
	{
		SubmitLockstepCommand(Tag, SelectedEntities, Location, TargetHandle, bQueue, NavigationScale);
		return;
	}

	const FGameplayTag MoveTag = GetCommandTag(MoveTagName);
	const FGameplayTag AttackTag = GetCommandTag(AttackTagName);
	const FGameplayTag StopTag = GetCommandTag(StopTagName);
	const FGameplayTag HoldTag = GetCommandTag(HoldTagName);
	const FGameplayTag PatrolTag = GetCommandTag(PatrolTagName);
	const TArray<FEntityHandle> CompatibleEntities =
		FilterEntitiesForCommand(SelectedEntities, Tag, Location != nullptr, UMassAPIFuncLib::IsValid(this, TargetHandle));
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
			QueueLocationCommand(
				Tag,
				CompatibleEntities,
				*Location,
				false,
				NavigationScale);
			return;
		}
		if (IssueMoveTo(
			CompatibleEntities,
			*Location,
			/*bCanInterrupt*/ false,
			NavigationScale))
		{
			RecordCommandTag(CompatibleEntities, Tag);
		}
		return;
	}

	if (Tag == AttackTag)
	{
		if (UMassAPIFuncLib::IsValid(this, TargetHandle))
		{
			if (IssueAttackTarget(CompatibleEntities, TargetHandle))
			{
				RecordCommandTag(CompatibleEntities, Tag);
			}
			return;
		}

		if (Location && bQueue)
		{
			QueueLocationCommand(
				Tag,
				CompatibleEntities,
				*Location,
				true,
				NavigationScale);
			return;
		}

		if (Location && IssueMoveTo(
			CompatibleEntities,
			*Location,
			/*bCanInterrupt*/ true,
			NavigationScale))
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
	bool bCanInterrupt,
	ERTSMoveNavigationScale NavigationScale)
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
		Order.NavigationScale = NavigationScale;

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
		if (IssueMoveTo(
			SingleEntity,
			Location,
			bCanInterrupt,
			NavigationScale))
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
		++CommandGenerations.FindOrAdd(Entity);
		PendingAdvances.Remove(Entity);
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
		if (IssueMoveTo(
			SingleEntity,
			NextOrder.Location,
			NextOrder.bCanInterrupt,
			NextOrder.NavigationScale))
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
	const UMassBattleSubsystem* Battle = UMassBattleSubsystem::GetPtr(this);
	if (!Battle || !Battle->bNetworkedMode)
	{
		AdvanceQueuedLocationCommands(Entities);
		return;
	}
	// Completion callbacks run outside command execution. Authority schedules the
	// next order; each peer pops its queue only when that command executes.
	AMassBattleNetworkGameModeBase* Mode = GetWorld()->GetAuthGameMode<AMassBattleNetworkGameModeBase>();
	UMassBattleNetworkSubsystem* Network = UMassBattleNetworkSubsystem::GetPtr(this);
	if (!Mode || !Network) return;
	TArray<int32> Payload;
	for (const FEntityHandle& Entity : Entities)
	{
		if (PendingAdvances.Contains(Entity)) continue;
		const int32 Id = Network->LookupUniqueIDByEntity(Entity);
		if (Id < 0) continue;
		PendingAdvances.Add(Entity);
		Payload.Add(Id);
		Payload.Add(CommandGenerations.FindRef(Entity));
	}
	if (!Payload.IsEmpty())
	{
		Mode->SendLockstepCommandToClients(GetAdvanceCommandTag(), {}, Payload, {});
	}
}

void URTSCommandSubsystem::HandleMoveTaskTransferred(
	const TArray<FEntityHandle>& Entities)
{
	const auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!Mass) return;
	const FMassEntityManager& Manager = Mass->GetEntityManager();
	for (const FEntityHandle& Entity : Entities)
	{
		const FMoving* Moving = Manager.IsEntityActive(Entity)
			? Manager.GetFragmentDataPtr<FMoving>(Entity) : nullptr;
		const uint32* OwnedId = OwnedMoveTaskIds.Find(Entity);
		// Old tasks report transfer after a replacement has already activated.
		// Preserve the replacement's queue when it is another order we own.
		if (Moving && OwnedId && Moving->ActiveMoveToTaskID == *OwnedId) continue;
		ClearQueuedLocationCommands({Entity});
	}
}

ERTSMoveNavigationScale URTSCommandSubsystem::ResolveNavigationScale(
	const TArray<FEntityHandle>& Entities,
	const ERTSMoveNavigationScale RequestedScale) const
{
	// Camera visibility is not a navigation cost. Auto remains unresolved here
	// so the terrain provider can compare tactical and city-route costs from a
	// representative command-group position. Explicit Strategic (for example a
	// minimap order) remains authoritative.
	return RequestedScale;
}

bool URTSCommandSubsystem::IssueMoveTo(
	const TArray<FEntityHandle>& SelectedEntities,
	const FVector& Location,
	bool bCanInterrupt,
	ERTSMoveNavigationScale NavigationScale)
{
	NavigationScale = ResolveNavigationScale(
		SelectedEntities,
		NavigationScale);
	TArray<FRTSMoveNavigationBatch> Batches;
	if (!FRTSMoveNavigationProviderRegistry::BuildBatches(
		this,
		SelectedEntities,
		Location,
		Batches,
		NavigationScale))
	{
		UE_LOG(
			LogRTSCommand,
			Warning,
			TEXT("Move command rejected: no navigation batch for %d units."),
			SelectedEntities.Num());
		return false;
	}

	bool bActivatedAny = false;
	FMBMoveFailCondition MoveFailCondition;
	// Match strategic orders: normal terrain speeds can be below the native
	// 100 UU/s stuck threshold. Keep the order until arrival or replacement.
	MoveFailCondition.StuckTimeLimit = 0.0f;
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
				MoveFailCondition,
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
				OwnedMoveTaskIds.Add(Entity, MoveTask->GetTaskID());
			}
			FRTSMoveNavigationProviderRegistry::NotifyBatchActivated(
				this,
				Batch);
			for (const FEntityHandle& Entity : Batch.Entities)
			{
				ActiveQueuedLocationEntities.Add(Entity);
			}
			bActivatedAny = true;
		}
	}
	return bActivatedAny;
}

bool URTSCommandSubsystem::IssueAttackTarget(const TArray<FEntityHandle>& SelectedEntities, FEntityHandle TargetHandle)
{
	if (!UMassAPIFuncLib::IsValid(this, TargetHandle))
	{
		return false;
	}

	FMBMoveFailCondition ChaseFailCondition;
	// Match MoveTo: normal terrain speeds can be below the native stuck threshold.
	ChaseFailCondition.StuckTimeLimit = 0.0f;
	if (UMassBattleBPTaskAgentsChaseAttack* ChaseTask = UMassBattleBPTaskAgentsChaseAttack::AgentsChaseAttack(
		this,
		SelectedEntities,
		TargetHandle,
		false,
		ChaseFailCondition
	))
	{
		ChaseTask->Activate();
		FRTSMoveNavigationProviderRegistry::BindChaseTaskNavigation(
			this, SelectedEntities, TargetHandle, ChaseTask->GetTaskID());
		return true;
	}

	return false;
}
