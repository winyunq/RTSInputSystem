// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "MassAPIStructs.h"
#include "RTSCommandSubsystem.generated.h"

class URTSCommandGridAsset;
class AActor;
struct FEntityHandle;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRTSNavigationRequested, URTSCommandGridAsset*, AActor*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRTSCommandIssued, FGameplayTag, AActor*);

/**
 * A low-level signal hub for RTS commands.
 * Allows command buttons to broadcast signals to
 * higher-level UI and Selection systems without direct dependencies.
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCommandSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** 广播给 UI，请求导航到一个网格 */
	FOnRTSNavigationRequested OnNavigationRequested;

	/** 广播给 逻辑层，请求对单位执行指令 */
	FOnRTSCommandIssued OnCommandIssued;

	/** 标准按钮调用此函数触发逻辑执行 */
	void IssueCommand(FGameplayTag Tag, AActor* Context);

	/** 发送带位移目标的指令（用于移动、攻击等按地面点目标） */
	void IssueCommandWithLocation(FGameplayTag Tag, const FVector& Location, bool bQueue = false);

	/** 发送目标实体指令（用于攻击锁定） */
	void IssueCommandWithTarget(FGameplayTag Tag, AActor* TargetActor);

	/** Returns the common live order for the supplied Mass selection. Mixed orders return an empty tag. */
	FGameplayTag GetActiveCommandTag(const TArray<FEntityHandle>& Entities) const;

	/** 子菜单或全局按钮调用此函数触发 UI 切换 */
	void RequestNavigation(URTSCommandGridAsset* NewGrid, AActor* Context)
	{
		OnNavigationRequested.Broadcast(NewGrid, Context);
	}

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	struct FQueuedLocationCommand
	{
		FGameplayTag Tag;
		FVector Location = FVector::ZeroVector;
		bool bCanInterrupt = false;
	};

	TMap<FEntityHandle, FGameplayTag> ActiveCommandTags;
	TMap<FEntityHandle, TArray<FQueuedLocationCommand>> QueuedLocationCommands;
	TSet<FEntityHandle> ActiveQueuedLocationEntities;

	TArray<FEntityHandle> GetSelectedMassEntities() const;
	TArray<FEntityHandle> FilterEntitiesForCommand(
		const TArray<FEntityHandle>& Entities,
		FGameplayTag Tag,
		bool bHasLocation,
		bool bHasTargetActor) const;
	FGameplayTag ResolveEntityCommandTag(const FEntityHandle& Entity) const;
	void RecordCommandTag(const TArray<FEntityHandle>& Entities, FGameplayTag Tag);
	void ExecuteCommand(
		FGameplayTag Tag,
		const TArray<FEntityHandle>& SelectedEntities,
		const FVector* Location = nullptr,
		AActor* TargetActor = nullptr,
		bool bQueue = false
	);
	void QueueLocationCommand(
		FGameplayTag Tag,
		const TArray<FEntityHandle>& SelectedEntities,
		const FVector& Location,
		bool bCanInterrupt);
	void ClearQueuedLocationCommands(const TArray<FEntityHandle>& Entities);
	void AdvanceQueuedLocationCommands(const TArray<FEntityHandle>& Entities);

	UFUNCTION()
	void HandleMoveTaskResolved(const TArray<FEntityHandle>& Entities);

	UFUNCTION()
	void HandleMoveTaskTransferred(const TArray<FEntityHandle>& Entities);

	bool IsEntityMoving(const FEntityHandle& Entity) const;
	bool IssueMoveTo(const TArray<FEntityHandle>& SelectedEntities, const FVector& Location, bool bCanInterrupt);
	bool IssueAttackTarget(const TArray<FEntityHandle>& SelectedEntities, AActor* TargetActor);
};
