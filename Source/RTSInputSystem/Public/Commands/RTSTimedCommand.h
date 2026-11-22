// Copyright 2026 Winyunq. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RTSTimedCommand.generated.h"

class URTSCommandButton;
class UTexture2D;

/**
 * Execution state shared by every command with a duration. The command system
 * never asks whether an instance represents training, research, construction,
 * or an ability.
 */
UENUM(BlueprintType)
enum class ERTSTimedCommandState : uint8
{
	Active,
	Queued,
	Paused
};

/**
 * The single authoritative state vector for any command with a duration.
 * Domain systems may keep payload beside it, but must not duplicate time,
 * queue, lane, pause, or cancellation state.
 * @brief 保存一项计时命令的状态和时间边界。
 * @details 进度接口用开始、结束和暂停时刻计算可显示状态。调用者可传入当前时间读取结果。
 * 
 * ```
 * 状态 / 起止时间 / 冻结时间 + 当前时刻 → 进度 / 已用时长 / 剩余时长
 * 状态与结束边界 → 完成判断
 * ```
 */
USTRUCT(BlueprintType)
struct RTSINPUTSYSTEM_API FRTSTimedCommandInstance
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	FGuid InstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	FGameplayTag CommandTag;

	/** Opaque payload key interpreted only when the command completes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	FName PayloadId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS Command")
	FText DisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS Command")
	TObjectPtr<UTexture2D> Icon = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	ERTSTimedCommandState State = ERTSTimedCommandState::Queued;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, NotReplicated, Category = "RTS Command")
	float ElapsedSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	float DurationSeconds = 0.0f;

	

	

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	int32 LaneIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	int32 QueueIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	bool bCanCancel = true;

	/** The exact definition used by both the command card and activity slot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, NotReplicated, Category = "RTS Command")
	TObjectPtr<URTSCommandButton> CommandButton = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, NotReplicated, Category = "RTS Command")
	TObjectPtr<UObject> Controller = nullptr;

	

	

	

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	double BeginTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	double EndTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	double FrozenTimeSeconds = 0.0;

	/**
	 * @brief 判断本条命令是否保存了冻结时刻。
	 * @details FrozenTimeSeconds 使用 double 最大值表示继续按当前时刻计时。
	 * @return 存在冻结时刻时为 true。
	 */
	bool IsTimeFrozen() const;

	/**
	 * @brief 读取此刻的归一化进度。
	 * @details 通过 Controller 的 IRTSCommandProgressProvider 取得时间，转给 GetProgress01AtTime。
	 * @return 当前归一化进度。
	 * @note 无参入口依赖 Controller 提供有效的命令进度时钟。
	 */
	double GetProgress01() const;

	/**
	 * @brief 读取此刻的已用时长。
	 * @details 通过 Controller 的进度时间接口取当前时刻，再用该时刻计算已用时长。
	 * @return 已用秒数。
	 * @note 无参入口依赖 Controller 提供有效的命令进度时钟。
	 */
	double GetElapsedSeconds() const;

	/**
	 * @brief 读取此刻的剩余时长。
	 * @details 通过 Controller 的进度时间接口取当前时刻，再用该时刻计算剩余时长。
	 * @return 剩余秒数。
	 * @note 无参入口依赖 Controller 提供有效的命令进度时钟。
	 */
	double GetRemainingSeconds() const;

	/**
	 * @brief 判断命令在当前时刻是否结束。
	 * @details 通过 Controller 的进度时间接口读取时刻，转给 IsCompleteAtTime。
	 * @return 计时命令达到完成条件时为 true。
	 * @note 无参入口依赖 Controller 提供有效的命令进度时钟。
	 */
	bool IsComplete() const;

	/**
	 * @brief 按给定时刻计算归一化进度。
	 * @details 零时长返回 1，起止区间无效时返回 0。其余情况用冻结时刻或传入时刻计算进度，并限制在 0 到 1 之间。
	 * @param CurrentTime CurrentTime 使用命令进度提供者的时间基准。
	 * @return 归一化进度。
	 */
	double GetProgress01AtTime(double CurrentTime) const;

	/**
	 * @brief 按给定时刻计算已用时长。
	 * @details 使用预定时长乘以该时刻的归一化进度。
	 * @param CurrentTime CurrentTime 使用命令进度提供者的时间基准。
	 * @return 已用秒数。
	 */
	double GetElapsedSecondsAtTime(double CurrentTime) const;

	/**
	 * @brief 按给定时刻计算剩余时长。
	 * @details 使用预定时长乘以尚未完成的进度比例。
	 * @param CurrentTime CurrentTime 使用命令进度提供者的时间基准。
	 * @return 剩余秒数。
	 */
	double GetRemainingSecondsAtTime(double CurrentTime) const;

	/**
	 * @brief 判断计时命令在给定时刻是否结束。
	 * @details 活动状态的零时长命令立即满足；正向时间区间内，冻结时刻或给定时刻到达结束边界时满足。
	 * @param CurrentTime CurrentTime 使用命令进度提供者的时间基准。
	 * @return 活动状态下满足完成条件时为 true。
	 */
	bool IsCompleteAtTime(double CurrentTime) const;
};

// The state transition used by every timed command. These functions only
// transform command state; they contain no domain-specific branches.
struct RTSINPUTSYSTEM_API FRTSTimedCommandMath
{
	static FRTSTimedCommandInstance Create(
		FGameplayTag CommandTag,
		FName PayloadId,
		float DurationSeconds,
		bool bCanCancel = true,
		URTSCommandButton* CommandButton = nullptr,
		UObject* Controller = nullptr,
		FGuid InstanceId = FGuid());

	static void Activate(FRTSTimedCommandInstance& Instance, int32 LaneIndex, double CurrentTime, double CurrentRate);
	static void Queue(FRTSTimedCommandInstance& Instance, double CurrentTime);
	static bool SetPaused(FRTSTimedCommandInstance& Instance, bool bPaused, double CurrentTime, double CurrentRate);

	
	static bool AddProgress(FRTSTimedCommandInstance& Instance, float DeltaSeconds, double CurrentTime, double CurrentRate);

	template <typename RecordType, typename SelectCommandType>
	static void NormalizeQueue(
		TArray<RecordType>& Records,
		int32 LaneCount,
		SelectCommandType SelectCommand , double CurrentTime, double CurrentRate )
	{
		const int32 SafeLaneCount = FMath::Max(1, LaneCount);
		int32 OccupiedLanes = 0;

		for (RecordType& Record : Records)
		{
			FRTSTimedCommandInstance& Command = SelectCommand(Record);
			if (Command.State == ERTSTimedCommandState::Queued)
			{
				continue;
			}
			if (OccupiedLanes < SafeLaneCount)
			{
				Command.LaneIndex = OccupiedLanes++;
			}
			else
			{
				Queue(Command, CurrentTime);
			}
		}

		for (RecordType& Record : Records)
		{
			if (OccupiedLanes >= SafeLaneCount)
			{
				break;
			}
			FRTSTimedCommandInstance& Command = SelectCommand(Record);
			if (Command.State == ERTSTimedCommandState::Queued)
			{
				Activate(Command, OccupiedLanes++, CurrentTime, CurrentRate);
			}
		}

		int32 QueueIndex = 1;
		for (RecordType& Record : Records)
		{
			FRTSTimedCommandInstance& Command = SelectCommand(Record);
			Command.QueueIndex =
				Command.State == ERTSTimedCommandState::Queued
					? QueueIndex++
					: 0;
		}
	}

	template <typename RecordType, typename SelectCommandType>
	static void ReindexQueue(
		TArray<RecordType>& Records,
		SelectCommandType SelectCommand , double CurrentTime )
	{
		for (int32 Index = 0; Index < Records.Num(); ++Index)
		{
			FRTSTimedCommandInstance& Command =
				SelectCommand(Records[Index]);
			Queue(Command, CurrentTime);
			Command.QueueIndex = Index + 1;
		}
	}

public:
	static void SetSpeedMultiplier(FRTSTimedCommandInstance& Instance, double CurrentTime, double NewRate);
};
