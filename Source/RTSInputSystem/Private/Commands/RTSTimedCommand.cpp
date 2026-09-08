// Copyright 2026 Winyunq. All Rights Reserved.

#include "Commands/RTSTimedCommand.h"

FRTSTimedCommandInstance FRTSTimedCommandMath::Create(
	const FGameplayTag CommandTag,
	const FName PayloadId,
	const float DurationSeconds,
	const bool bCanCancel,
	URTSCommandButton* const CommandButton,
	UObject* const Controller,
	const FGuid InstanceId)
{
	FRTSTimedCommandInstance Result;
	Result.InstanceId = InstanceId.IsValid() ? InstanceId : FGuid::NewGuid();
	Result.CommandTag = CommandTag;
	Result.PayloadId = PayloadId;
	Result.DurationSeconds = FMath::Max(0.0f, DurationSeconds);
	Result.bCanCancel = bCanCancel;
	Result.CommandButton = CommandButton;
	Result.Controller = Controller;
	return Result;
}

void FRTSTimedCommandMath::Activate(
	FRTSTimedCommandInstance& Instance,
	const int32 LaneIndex)
{
	Instance.State = ERTSTimedCommandState::Active;
	Instance.LaneIndex = FMath::Max(0, LaneIndex);
	Instance.QueueIndex = 0;
}

void FRTSTimedCommandMath::Queue(FRTSTimedCommandInstance& Instance)
{
	Instance.State = ERTSTimedCommandState::Queued;
	Instance.LaneIndex = INDEX_NONE;
}

bool FRTSTimedCommandMath::SetPaused(
	FRTSTimedCommandInstance& Instance,
	const bool bPaused)
{
	if (Instance.State == ERTSTimedCommandState::Queued)
	{
		return false;
	}
	const ERTSTimedCommandState NewState = bPaused
		? ERTSTimedCommandState::Paused
		: ERTSTimedCommandState::Active;
	if (Instance.State == NewState)
	{
		return false;
	}
	Instance.State = NewState;
	return true;
}

bool FRTSTimedCommandMath::Advance(
	FRTSTimedCommandInstance& Instance,
	const float DeltaSeconds,
	const float SpeedMultiplier)
{
	if (Instance.State != ERTSTimedCommandState::Active
		|| DeltaSeconds <= 0.0f)
	{
		return false;
	}
	Instance.ElapsedSeconds = FMath::Min(
		Instance.DurationSeconds,
		Instance.ElapsedSeconds
			+ DeltaSeconds * FMath::Max(0.0f, SpeedMultiplier));
	return Instance.ElapsedSeconds + UE_KINDA_SMALL_NUMBER
		>= Instance.DurationSeconds;
}

bool FRTSTimedCommandMath::AddProgress(
	FRTSTimedCommandInstance& Instance,
	const float DeltaSeconds)
{
	Instance.ElapsedSeconds = FMath::Clamp(
		Instance.ElapsedSeconds + DeltaSeconds,
		0.0f,
		Instance.DurationSeconds);
	return Instance.ElapsedSeconds + UE_KINDA_SMALL_NUMBER
		>= Instance.DurationSeconds;
}
