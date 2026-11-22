// Copyright 2026 Winyunq. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Commands/RTSTimedCommand.h"
#include "RTSCommandProgressProvider.generated.h"

/**
 * Transport-neutral bridge between gameplay systems and the RTS selection UI.
 * Training, construction, research and national focuses all publish the same
 * progress-item shape; RTSInputSystem owns their shared presentation.
 */
UINTERFACE(BlueprintType)
class RTSINPUTSYSTEM_API URTSCommandProgressProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * @brief 从业务对象读取计时命令和统一时间。
 * @details HUD 通过来源 ID 读取对应的进度项，计时实例也可向同一实现取得当前时刻。
 *
 * ```
 * 来源 ID → GetCommandProgressItems → 计时实例
 * GetCommandProgressTimeSeconds → 当前时刻 → 进度计算
 * ```
 */
class RTSINPUTSYSTEM_API IRTSCommandProgressProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Progress")
	void GetCommandProgressItems(
		UPARAM(ref) TArray<FRTSTimedCommandInstance>& OutItems, FName SourceId = NAME_None) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
	 double GetCommandProgressTimeSeconds() const;
};
