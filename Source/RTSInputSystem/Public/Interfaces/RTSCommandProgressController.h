// Copyright 2026 Winyunq. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commands/RTSTimedCommand.h"
#include "UObject/Interface.h"
#include "RTSCommandProgressController.generated.h"

/**
 * Optional action endpoint for a common command-progress item.
 *
 * The selection UI knows only the stable item id. Gameplay systems remain
 * responsible for authority, refunds, queue promotion, and state refresh.
 */
UINTERFACE(BlueprintType)
class RTSINPUTSYSTEM_API URTSCommandProgressController : public UInterface
{
	GENERATED_BODY()
};

/**
 * @brief 定义计时命令的准备、开始、完成及取消入口。
 * @details 业务对象实现相应阶段处理，可以对进度项的执行与容量变化作出响应。
 *
 * ```
 * 进度项 → 准备 → 开始 → 完成
 * 取消请求 / 容量变化 → 业务对象处理
 * ```
 */
class RTSINPUTSYSTEM_API IRTSCommandProgressController
{
	GENERATED_BODY()

public:
	/** Validates and prepares a task before it enters the existing capacity queue. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Progress")
	bool PrepareCommandProgressItem(UPARAM(ref) FRTSTimedCommandInstance& Item, FName SourceId = NAME_None);
	virtual bool PrepareCommandProgressItem_Implementation(FRTSTimedCommandInstance& Item, FName SourceId) { return true; }

	/** Called once when the existing executor promotes a prepared item into a lane. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Progress")
	void StartCommandProgressItem(UPARAM(ref) FRTSTimedCommandInstance& Item);
	virtual void StartCommandProgressItem_Implementation(FRTSTimedCommandInstance& Item) {}

	/** Applies the domain result after the executor has removed the finished/cancelled task. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Progress")
	void ResolveCommandProgressItem(const FRTSTimedCommandInstance& Item, bool bCancelled);
	virtual void ResolveCommandProgressItem_Implementation(const FRTSTimedCommandInstance& Item, bool bCancelled) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Progress")
	void RequestCancelCommandProgressItem(FName ItemId);

	/**
	 * @brief 由容量已有的状态所有者接收本容量变动，避免全局广播唤醒无关对象。
	 */
	virtual void HandleCapacityChanged(FName CapacityId) {}
};
