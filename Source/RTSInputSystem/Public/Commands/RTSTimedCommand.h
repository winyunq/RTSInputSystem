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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	float ElapsedSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "RTS Command")
	float DurationSeconds = 0.0f;

	// Optional presentation clock authored by a deterministic MassBattle command.
	// The UI derives progress locally; these fields never advance simulation.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, NotReplicated, Category = "RTS Command")
	int32 SimulationStartTick = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, NotReplicated, Category = "RTS Command")
	int32 SimulationEndTick = INDEX_NONE;

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

	float GetProgress01() const
	{
		return DurationSeconds <= UE_KINDA_SMALL_NUMBER
			? 1.0f
			: FMath::Clamp(ElapsedSeconds / DurationSeconds, 0.0f, 1.0f);
	}

	float GetRemainingSeconds() const
	{
		return FMath::Max(0.0f, DurationSeconds - ElapsedSeconds);
	}
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
		UObject* Controller = nullptr);

	static void Activate(FRTSTimedCommandInstance& Instance, int32 LaneIndex);
	static void Queue(FRTSTimedCommandInstance& Instance);
	static bool SetPaused(FRTSTimedCommandInstance& Instance, bool bPaused);

	/** Returns true exactly when the transformed state has completed. */
	static bool Advance(
		FRTSTimedCommandInstance& Instance,
		float DeltaSeconds,
		float SpeedMultiplier = 1.0f);
	static bool AddProgress(
		FRTSTimedCommandInstance& Instance,
		float DeltaSeconds);

	template <typename RecordType, typename SelectCommandType>
	static void NormalizeQueue(
		TArray<RecordType>& Records,
		int32 LaneCount,
		SelectCommandType SelectCommand)
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
				Queue(Command);
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
				Activate(Command, OccupiedLanes++);
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
		SelectCommandType SelectCommand)
	{
		for (int32 Index = 0; Index < Records.Num(); ++Index)
		{
			FRTSTimedCommandInstance& Command =
				SelectCommand(Records[Index]);
			Queue(Command);
			Command.QueueIndex = Index + 1;
		}
	}
};
