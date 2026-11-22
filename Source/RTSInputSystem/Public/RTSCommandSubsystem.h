// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "MassAPIStructs.h"
#include "RTSMoveNavigationProvider.h"
#include "RTSCommandSubsystem.generated.h"

class URTSCommandGridAsset;
class AActor;
class UMassBattleBPTaskAgentsMoveTo;
class UMassBattleBPTaskAgentsChaseAttack;
struct FEntityHandle;
struct FMassBattleNetCommand;

/** Binds business observers to the original native tasks before any task activates. */
using FRTSCommandTasksPrepared = TFunction<void(
	const TArray<UMassBattleBPTaskAgentsMoveTo*>&, UMassBattleBPTaskAgentsChaseAttack*)>;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRTSNavigationRequested, URTSCommandGridAsset*, AActor*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRTSCommandIssued, FGameplayTag, AActor*);

/**
 * A low-level signal hub for RTS commands.
 * Allows command buttons to broadcast signals to
 * higher-level UI and Selection systems without direct dependencies.
 * @ProductDocument
 * @brief 为本地玩家提交原生 Mass 命令并维护移动队列。
 * @details 调用方提供受令单位列表。联机请求在执行帧消费。
 * 
 * ```
 * 受令句柄数组 + 目标 → IssueCommandForEntities
 *                       ├→ 单机任务 / 位置队列
 *                       └→ UID 载荷 → 锁步执行帧 → 原生任务
 * ```
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCommandSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static FName GetInputCommandTag();
	static FName GetAdvanceCommandTag();
	static bool IsRTSLockstepCommand(FName Tag);
	/**
	 * @brief 在锁步执行阶段消费 RTS 原生命令。
	 * @details 普通命令校验载荷和发送方队伍，恢复受令实体和目标后交给原生入口。队列推进命令按网络 UID 和队列版本推进对应单位。
	 * @param Command 当前执行帧收到的联机命令。
	 */
	void ExecuteLockstepCommand(const FMassBattleNetCommand& Command);
	/** 广播给 UI，请求导航到一个网格 */
	FOnRTSNavigationRequested OnNavigationRequested;

	/**
	 * 广播给 逻辑层，请求对单位执行指令
	 * @ProductDocument
	 */
	FOnRTSCommandIssued OnCommandIssued;

	/**
	 * 标准按钮调用此函数触发逻辑执行
	 * @ProductDocument
	 * @brief 为本地当前选区发出无目标原生命令。
	 * @details 读取选中的 Mass 实体句柄并交给原生执行入口。
	 * @param Tag 无需目标输入的命令标签。
	 * @param Context 保留的调用上下文参数。受令单位从本地玩家选区读取。
	 */
	void IssueCommand(FGameplayTag Tag, AActor* Context);

	/**
	 * 发送带位移目标的指令（用于移动、攻击等按地面点目标）
	 * @ProductDocument
	 * @brief 向本地当前选区发出原生位置命令。
	 * @details 读取选中的 Mass 实体，把世界坐标、排队状态和导航尺度传给原生执行入口。
	 * @param Tag 要下发的原生命令标签。
	 * @param Location 目标世界坐标。
	 * @param bQueue 是否交给位置命令队列处理。
	 * @param bForceStrategicNavigation 为 true 时使用战略导航尺度，否则使用自动尺度。
	 */
	void IssueCommandWithLocation(
		FGameplayTag Tag,
		const FVector& Location,
		bool bQueue = false,
		bool bForceStrategicNavigation = false);

	/** 发送目标实体指令（用于攻击锁定） 
	 * @brief 向本地当前选区发出原生实体目标命令。
	 * @details 通过 IssueCommandWithEntityTarget 读取 Mass 选区并提交实体目标。
	 * @param Tag 要下发的原生命令标签。
	 * @param TargetEntity 命令指向的 Mass 实体句柄。
	 */
	void IssueCommandWithTarget(FGameplayTag Tag, const FEntityHandle& TargetEntity);

	/**
	 * @brief 向调用方指定的一组 Mass 实体下达原生命令。
	 * @details 联机且位于锁步执行之外时发送网络命令。单机或锁步执行时，使用给定实体数组执行移动、攻击、停止、坚守或巡逻。即时移动和追击在任务准备成功后替换之前的队列。
	 * @param WorldContext 用于取得世界、Mass 和联机状态的上下文对象。
	 * @param Tag 要执行的命令标签。
	 * @param Entities 调用方已确定的受令实体句柄数组。
	 * @param Location 目标世界坐标。无位置目标时传空指针。
	 * @param Target 命令指向的 Mass 实体。无实体目标时传空句柄。
	 * @param bQueue 位置命令是否交给单位的队列处理。
	 * @code{.cpp}
	 * URTSCommandSubsystem::IssueCommandForEntities(WorldContext, MoveCommandTag, Squad, &Destination);
	 * @endcode
	 * @ProductDocument
	 * @param NavigationScale 移动导航尺度。可以指定战略尺度或使用自动选择。
	 * @param TasksPrepared 原生移动或追击任务准备好后、激活前执行的可选回调。
	 * @param bTargetBehaviorsCanInterrupt 传给实体追击任务的行为中断配置。
	 * @param AcceptanceRadiusOverride 大于零时覆盖移动任务的平面抵达半径。
	 * @param bNavigationRejected 可选输出。进入函数时置为 false，移动导航分批失败时置为 true。
	 * @return 已发送联机请求、交付位置队列或成功执行原生命令时为 true。请求被入口拒绝或任务无法准备时为 false。
	 */
	static bool IssueCommandForEntities(UObject* WorldContext, FGameplayTag Tag,
		const TArray<FEntityHandle>& Entities, const FVector* Location = nullptr,
		FEntityHandle Target = FEntityHandle(), bool bQueue = false,
		ERTSMoveNavigationScale NavigationScale = ERTSMoveNavigationScale::Auto,
		const FRTSCommandTasksPrepared& TasksPrepared = {},
		bool bTargetBehaviorsCanInterrupt = false, float AcceptanceRadiusOverride = 0.0f,
		bool* bNavigationRejected = nullptr);

	/** Returns the common live order for the supplied Mass selection. Mixed orders return an empty tag. */
	FGameplayTag GetActiveCommandTag(const TArray<FEntityHandle>& Entities) const;

	/** 子菜单或全局按钮调用此函数触发 UI 切换 */
	void RequestNavigation(URTSCommandGridAsset* NewGrid, AActor* Context)
	{
		OnNavigationRequested.Broadcast(NewGrid, Context);
	}


	/**
	 * @brief 向指定 Mass 实体目标发出命令。
	 * @ProductDocument
	 */
	void IssueCommandWithEntityTarget(FGameplayTag Tag, const FEntityHandle& TargetEntity);
protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	struct FQueuedLocationCommand
	{
		FGameplayTag Tag;
		FVector Location = FVector::ZeroVector;
		bool bCanInterrupt = false;
		ERTSMoveNavigationScale NavigationScale =
			ERTSMoveNavigationScale::Auto;
	};

	TMap<FEntityHandle, uint32> OwnedMoveTaskIds;
	TMap<FEntityHandle, int32> CommandGenerations;
	TSet<FEntityHandle> PendingAdvances;
	TMap<FEntityHandle, FGameplayTag> ActiveCommandTags;
	TMap<FEntityHandle, TArray<FQueuedLocationCommand>> QueuedLocationCommands;
	TSet<FEntityHandle> ActiveQueuedLocationEntities;

	/**
	 * @brief 取得原生便捷入口的受令实体。
	 * @details 优先使用本地选区子系统的可控选中实体。没有选区子系统时对 Mass 查询具有 Selected 标记的实体。
	 * @return 当前原生便捷入口使用的实体句柄数组。
	 */
	TArray<FEntityHandle> GetSelectedMassEntities() const;
	static TArray<URTSCommandSubsystem*> GetWorldCommandOwners(UObject* WorldContext);
	
	FGameplayTag ResolveEntityCommandTag(const FEntityHandle& Entity) const;
	void RecordCommandTag(const TArray<FEntityHandle>& Entities, FGameplayTag Tag);
	/**
	 * @brief 将原生单位命令编码为锁步请求。
	 * @details 把受令单位换成网络 UID，去重排序后写入整数载荷。整数载荷依次含协议版本、排队标志、导航尺度、目标 UID 和受令单位 UID。位置按三个浮点数发送。
	 * @param Tag 要发出的命令标签。
	 * @param Entities 准备提交的受令实体句柄。
	 * @param Location 目标世界坐标，可为空。
	 * @param Target 目标实体句柄，缺省时写空目标 UID。
	 * @param bQueue 本次下发记录中的排队标志。
	 * @param Scale 写入联机请求的导航尺度。
	 * @return 请求送到网络控制器时为 true。网络上下文或受令实体 UID 不满足提交条件时为 false。
	 */
	bool SubmitLockstepCommand(FGameplayTag Tag, const TArray<FEntityHandle>& Entities,
		const FVector* Location, FEntityHandle Target, bool bQueue, ERTSMoveNavigationScale Scale);
	void ExecuteCommand(
		FGameplayTag Tag,
		const TArray<FEntityHandle>& SelectedEntities,
		const FVector* Location = nullptr,
		FEntityHandle TargetHandle = FEntityHandle(),
		bool bQueue = false,
		ERTSMoveNavigationScale NavigationScale =
			ERTSMoveNavigationScale::Auto
	);
	void QueueLocationCommand(
		FGameplayTag Tag,
		const TArray<FEntityHandle>& SelectedEntities,
		const FVector& Location,
		bool bCanInterrupt,
		ERTSMoveNavigationScale NavigationScale);
	void ClearQueuedLocationCommands(const TArray<FEntityHandle>& Entities);
	void AdvanceQueuedLocationCommands(const TArray<FEntityHandle>& Entities);

	UFUNCTION()
	void HandleMoveTaskResolved(const TArray<FEntityHandle>& Entities);

	UFUNCTION()
	void HandleMoveTaskTransferred(const TArray<FEntityHandle>& Entities);

	bool IsEntityMoving(const FEntityHandle& Entity) const;
	/**
	 * @brief 为指定 Mass 实体准备并启动移动任务。
	 * @details 先计算导航批次，再按批建立任务并在激活前通知 TasksPrepared。导航批次为空或准备失败时拒绝发令。
	 * @param WorldContext 用于解析世界和 Mass 状态的上下文。
	 * @param SelectedEntities 要移动的实体句柄。
	 * @param Location 目标世界坐标。
	 * @param bCanInterrupt 传给移动任务的行为中断配置。
	 * @param NavigationScale 用于构建导航批次的导航尺度。
	 * @param TasksPrepared 任务准备后、激活前执行的可选回调。
	 * @param AcceptanceRadiusOverride 大于零时覆盖平面抵达半径。
	 * @param bNavigationRejected 可选输出。导航分批失败时写入 true。
	 * @return 成功创建并启动至少一批移动任务时为 true。
	 */
	static bool IssueMoveTo(UObject* WorldContext,
		const TArray<FEntityHandle>& SelectedEntities,
		const FVector& Location,
		bool bCanInterrupt,
		ERTSMoveNavigationScale NavigationScale,
		const FRTSCommandTasksPrepared& TasksPrepared = {}, float AcceptanceRadiusOverride = 0.0f,
		bool* bNavigationRejected = nullptr);
	static bool IssueAttackTarget(UObject* WorldContext,
		const TArray<FEntityHandle>& SelectedEntities, FEntityHandle TargetHandle,
		const FRTSCommandTasksPrepared& TasksPrepared, bool bTargetBehaviorsCanInterrupt);
};
