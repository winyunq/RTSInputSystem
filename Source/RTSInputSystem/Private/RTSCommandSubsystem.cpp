// Copyright 2024 Winy unq All Rights Reserved.

#include "RTSCommandSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "MassAPISubsystem.h"
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

bool URTSCommandSubsystem::SubmitLockstepCommand(FGameplayTag Tag,
    const TArray<FEntityHandle>& Entities, const FVector* Location,
    FEntityHandle Target, bool bQueue, ERTSMoveNavigationScale Scale)
{
    ULocalPlayer* LocalPlayer = GetLocalPlayer();
    APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
    auto* NetworkController = Cast<AMassBattleNetworkPlayerControllerBase>(Controller);
    auto* Network = UMassBattleNetworkSubsystem::GetPtr(this);
    if (!NetworkController || !Network || !Tag.IsValid() || (Location && Location->ContainsNaN())) return false;
    const int32 TargetId = UMassAPIFuncLib::IsValid(this, Target)
        ? Network->LookupUniqueIDByEntity(Target) : INDEX_NONE;
    TArray<int32> Ids;
    for (const FEntityHandle& Entity : Entities)
    {
        const int32 Id = Network->LookupUniqueIDByEntity(Entity);
        if (Id >= 0) Ids.AddUnique(Id);
    }
    if (Ids.IsEmpty()) return false;
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
    return true;
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

TArray<URTSCommandSubsystem*> URTSCommandSubsystem::GetWorldCommandOwners(UObject* WorldContext)
{
	TArray<URTSCommandSubsystem*> Owners;
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (GameInstance)
	{
		for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
		{
			if (URTSCommandSubsystem* Commands = LocalPlayer->GetSubsystem<URTSCommandSubsystem>())
			{
				Owners.Add(Commands);
			}
		}
	}
	if (URTSCommandSubsystem* Commands = Cast<URTSCommandSubsystem>(WorldContext))
	{
		Owners.AddUnique(Commands);
	}
	return Owners;
}

TArray<FEntityHandle> URTSCommandSubsystem::FilterEntitiesForCommand(UObject* WorldContext,
	const TArray<FEntityHandle>& Entities,
	FGameplayTag Tag,
	bool bHasLocation,
	bool bHasTargetActor)
{
	TArray<FEntityHandle> Result;
	if (!Tag.IsValid() || Entities.IsEmpty())
	{
		return Result;
	}

	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
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
	ERTSMoveNavigationScale NavigationScale)
{
	IssueCommandForEntities(this, Tag, SelectedEntities, Location, TargetHandle, bQueue, NavigationScale);
}

bool URTSCommandSubsystem::IssueCommandForEntities(UObject* WorldContext, FGameplayTag Tag,
	const TArray<FEntityHandle>& Entities, const FVector* Location, FEntityHandle Target,
	bool bQueue, ERTSMoveNavigationScale NavigationScale,
	const FRTSCommandTasksPrepared& TasksPrepared, bool bTargetBehaviorsCanInterrupt,
	float AcceptanceRadiusOverride, bool* bNavigationRejected)
{
	if (bNavigationRejected) *bNavigationRejected = false;
	if (!WorldContext || !WorldContext->GetWorld() || !Tag.IsValid() || Entities.IsEmpty()
		|| (Location && Location->ContainsNaN()))
	{
		return false;
	}

	const TArray<URTSCommandSubsystem*> Owners = GetWorldCommandOwners(WorldContext);
	URTSCommandSubsystem* InputOwner = Cast<URTSCommandSubsystem>(WorldContext);
	if (!InputOwner && !Owners.IsEmpty()) InputOwner = Owners[0];
	const auto* Battle = UMassBattleSubsystem::GetPtr(WorldContext);
	const auto* Network = UMassBattleNetworkSubsystem::GetPtr(WorldContext);
	if (Battle && Battle->bNetworkedMode && (!Network
		|| (!Network->IsInCommandExecution() && !Network->IsInLevelInitialization())))
	{
		// A native observer belongs to the already accepted execution. Strategic
		// records supply it when their existing lockstep command is consumed.
		if (!InputOwner || TasksPrepared || bTargetBehaviorsCanInterrupt || AcceptanceRadiusOverride > 0.0f)
		{
			return false;
		}
		return InputOwner->SubmitLockstepCommand(Tag, Entities, Location, Target, bQueue, NavigationScale);
	}

	const FGameplayTag MoveTag = GetCommandTag(MoveTagName);
	const FGameplayTag AttackTag = GetCommandTag(AttackTagName);
	const FGameplayTag StopTag = GetCommandTag(StopTagName);
	const FGameplayTag HoldTag = GetCommandTag(HoldTagName);
	const FGameplayTag PatrolTag = GetCommandTag(PatrolTagName);
	if (Tag != MoveTag && Tag != PatrolTag && Tag != AttackTag && Tag != StopTag && Tag != HoldTag)
	{
		return false;
	}
	const bool bHasTarget = UMassAPIFuncLib::IsValid(WorldContext, Target);
	const TArray<FEntityHandle> CompatibleEntities =
		FilterEntitiesForCommand(WorldContext, Entities, Tag, Location != nullptr, bHasTarget);
	if (CompatibleEntities.IsEmpty()) return false;

	const auto ClearReplacedOrders = [&Owners, &CompatibleEntities, bQueue]()
	{
		if (!bQueue)
		{
			for (URTSCommandSubsystem* Owner : Owners)
			{
				Owner->ClearQueuedLocationCommands(CompatibleEntities);
			}
		}
	};
	const FRTSCommandTasksPrepared PrepareAcceptedTasks = [&ClearReplacedOrders, &TasksPrepared](
		const TArray<UMassBattleBPTaskAgentsMoveTo*>& MoveTasks, UMassBattleBPTaskAgentsChaseAttack* ChaseTask)
	{
		// Navigation and task creation have succeeded. Replace the previous order
		// before activation can transfer or resolve any of its native tasks.
		ClearReplacedOrders();
		if (TasksPrepared) TasksPrepared(MoveTasks, ChaseTask);
	};
	const auto RecordAccepted = [&Owners, &CompatibleEntities, Tag](bool bAccepted)
	{
		if (bAccepted)
		{
			for (URTSCommandSubsystem* Owner : Owners)
			{
				Owner->RecordCommandTag(CompatibleEntities, Tag);
			}
		}
		return bAccepted;
	};

	if (Tag == StopTag || Tag == HoldTag)
	{
		ClearReplacedOrders();
		UMassBattleFuncLib::StopAgentsAllMovement(WorldContext, CompatibleEntities);
		return RecordAccepted(true);
	}
	if (Tag == AttackTag && bHasTarget)
	{
		return RecordAccepted(IssueAttackTarget(WorldContext, CompatibleEntities, Target,
			PrepareAcceptedTasks, bTargetBehaviorsCanInterrupt));
	}
	if (!Location) return false;
	if (bQueue)
	{
		if (!InputOwner || TasksPrepared) return false;
		InputOwner->QueueLocationCommand(Tag, CompatibleEntities, *Location, Tag == AttackTag, NavigationScale);
		return true;
	}
	return RecordAccepted(IssueMoveTo(WorldContext, CompatibleEntities, *Location,
		Tag == AttackTag, NavigationScale, PrepareAcceptedTasks, AcceptanceRadiusOverride, bNavigationRejected));
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
			this,
			SingleEntity,
			Location,
			bCanInterrupt,
			NavigationScale))
		{
			ActiveQueuedLocationEntities.Add(Entity);
			for (URTSCommandSubsystem* Owner : GetWorldCommandOwners(this))
			{
				Owner->RecordCommandTag(SingleEntity, Tag);
			}
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
			this,
			SingleEntity,
			NextOrder.Location,
			NextOrder.bCanInterrupt,
			NextOrder.NavigationScale))
		{
			for (URTSCommandSubsystem* Owner : GetWorldCommandOwners(this))
			{
				Owner->RecordCommandTag(SingleEntity, NextOrder.Tag);
			}
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

bool URTSCommandSubsystem::IssueMoveTo(UObject* WorldContext,
	const TArray<FEntityHandle>& SelectedEntities,
	const FVector& Location,
	bool bCanInterrupt,
	ERTSMoveNavigationScale NavigationScale,
	const FRTSCommandTasksPrepared& TasksPrepared, float AcceptanceRadiusOverride,
	bool* bNavigationRejected)
{
	// Auto remains unresolved so the original provider compares tactical/city
	// route costs. Explicit Strategic remains the caller's navigation parameter.
	TArray<FRTSMoveNavigationBatch> Batches;
	if (!FRTSMoveNavigationProviderRegistry::BuildBatches(
		WorldContext, SelectedEntities, Location, Batches, NavigationScale) || Batches.IsEmpty())
	{
		if (bNavigationRejected) *bNavigationRejected = true;
		UE_LOG(LogRTSCommand, Warning,
			TEXT("Move command rejected: no navigation batch for %d units."), SelectedEntities.Num());
		return false;
	}
	if (AcceptanceRadiusOverride > 0.0f)
	{
		if (UMassAPISubsystem* Mass = UMassAPISubsystem::GetPtr(WorldContext))
		{
			for (const FEntityHandle& Entity : SelectedEntities)
			{
				if (FMove* Move = Mass->GetFragmentPtr<FMove>(Entity))
				{
					Move->XY.AcceptanceRadius = AcceptanceRadiusOverride;
				}
			}
		}
	}

	const TArray<URTSCommandSubsystem*> Owners = GetWorldCommandOwners(WorldContext);
	URTSCommandSubsystem* QueueOwner = Cast<URTSCommandSubsystem>(WorldContext);
	if (!QueueOwner && !Owners.IsEmpty()) QueueOwner = Owners[0];
	TArray<UMassBattleBPTaskAgentsMoveTo*> MoveTasks;
	TArray<const FRTSMoveNavigationBatch*> TaskBatches;
	FMBMoveFailCondition MoveFailCondition;
	// Normal terrain speeds can be below the native stuck threshold. Keep the
	// original player/strategic order until arrival or replacement.
	MoveFailCondition.StuckTimeLimit = 0.0f;
	for (const FRTSMoveNavigationBatch& Batch : Batches)
	{
		if (Batch.Entities.IsEmpty()) continue;
		FMBMoveGoal Goal;
		Goal.GoalType = EMBMoveGoalType::Location;
		Goal.Locations.Init(Batch.Goal, Batch.Entities.Num());
		UE_LOG(LogRTSCommand, Log,
			TEXT("Move command batch: Units=%d Navigation=%s Layer=%d"),
			Batch.Entities.Num(), GetNavigationModeName(Batch.Navigation.Mode),
			Batch.Navigation.FlowFieldLayer.LayerID);
		if (UMassBattleBPTaskAgentsMoveTo* MoveTask = UMassBattleBPTaskAgentsMoveTo::AgentsMoveTo(
			WorldContext, Batch.Entities, Goal, Batch.Navigation, MoveFailCondition, bCanInterrupt))
		{
			if (QueueOwner)
			{
				MoveTask->OnAgentSuccess.AddDynamic(QueueOwner, &URTSCommandSubsystem::HandleMoveTaskResolved);
				MoveTask->OnAgentFail.AddDynamic(QueueOwner, &URTSCommandSubsystem::HandleMoveTaskResolved);
				MoveTask->OnAgentTransfer.AddDynamic(QueueOwner, &URTSCommandSubsystem::HandleMoveTaskTransferred);
			}
			MoveTasks.Add(MoveTask);
			TaskBatches.Add(&Batch);
		}
	}
	if (MoveTasks.IsEmpty()) return false;
	// The whole accepted batch is known before binding a business observer,
	// including tasks that can finish synchronously during Activate.
	if (TasksPrepared) TasksPrepared(MoveTasks, nullptr);
	for (int32 TaskIndex = 0; TaskIndex < MoveTasks.Num(); ++TaskIndex)
	{
		UMassBattleBPTaskAgentsMoveTo* MoveTask = MoveTasks[TaskIndex];
		const FRTSMoveNavigationBatch& Batch = *TaskBatches[TaskIndex];
		if (QueueOwner)
		{
			for (const FEntityHandle& Entity : Batch.Entities)
			{
				QueueOwner->OwnedMoveTaskIds.Add(Entity, MoveTask->GetTaskID());
				QueueOwner->ActiveQueuedLocationEntities.Add(Entity);
			}
		}
		MoveTask->Activate();
		FRTSMoveNavigationProviderRegistry::NotifyBatchActivated(WorldContext, Batch);
	}
	return true;
}

bool URTSCommandSubsystem::IssueAttackTarget(UObject* WorldContext,
	const TArray<FEntityHandle>& SelectedEntities, FEntityHandle TargetHandle,
	const FRTSCommandTasksPrepared& TasksPrepared, bool bTargetBehaviorsCanInterrupt)
{
	if (!UMassAPIFuncLib::IsValid(WorldContext, TargetHandle)) return false;
	FMBMoveFailCondition ChaseFailCondition;
	ChaseFailCondition.StuckTimeLimit = 0.0f;
	if (UMassBattleBPTaskAgentsChaseAttack* ChaseTask = UMassBattleBPTaskAgentsChaseAttack::AgentsChaseAttack(
		WorldContext, SelectedEntities, TargetHandle, bTargetBehaviorsCanInterrupt, ChaseFailCondition))
	{
		if (TasksPrepared) TasksPrepared({}, ChaseTask);
		ChaseTask->Activate();
		FRTSMoveNavigationProviderRegistry::BindChaseTaskNavigation(
			WorldContext, SelectedEntities, TargetHandle, ChaseTask->GetTaskID());
		return true;
	}
	return false;
}
