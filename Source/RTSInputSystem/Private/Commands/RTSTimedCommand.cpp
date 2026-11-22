// Copyright 2026 Winyunq. All Rights Reserved.

/**
 * @file RTSTimedCommand.cpp
 * @brief 读取计时命令的进度、经过时间、剩余时间和完成状态。
 * @details 时间计算使用当前时间或冻结时间。极短时长返回完整进度，无效起止区间返回零。完成判断同时要求命令处于 Active。
 * @code{.unparsed}
 * 当前时间 / 冻结时间
 *           ├─ GetProgress01AtTime → 比例 → 经过时间 / 剩余时间
 *           └─ IsCompleteAtTime → Active 且达到时长或截止条件
 * @endcode
 */
#include "Commands/RTSTimedCommand.h"
#include "Interfaces/RTSCommandProgressProvider.h"

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
	 int32 LaneIndex , double CurrentTime, double CurrentRate )
{
	 const double Progress = Instance.GetProgress01AtTime(CurrentTime);
	    const double Span = Instance.DurationSeconds / (CurrentRate > 0.0 ? CurrentRate : 1.0);
	    Instance.BeginTimeSeconds = CurrentTime - Progress * Span;
	    Instance.EndTimeSeconds = Instance.BeginTimeSeconds + Span;
	    Instance.FrozenTimeSeconds = CurrentRate > 0.0 ? TNumericLimits<double>::Max() : CurrentTime; Instance.State = ERTSTimedCommandState::Active;
	Instance.LaneIndex = FMath::Max(0, LaneIndex);
	Instance.QueueIndex = 0;
}

void FRTSTimedCommandMath::Queue(FRTSTimedCommandInstance& Instance , double CurrentTime )
{
	 if (!Instance.IsTimeFrozen()) Instance.FrozenTimeSeconds = CurrentTime; Instance.State = ERTSTimedCommandState::Queued;
	Instance.LaneIndex = INDEX_NONE;
}

bool FRTSTimedCommandMath::SetPaused(
	FRTSTimedCommandInstance& Instance,
	 bool bPaused , double CurrentTime, double CurrentRate )
{
	if (Instance.State == ERTSTimedCommandState::Queued)
	
		return false;
	
	const ERTSTimedCommandState NewState = bPaused
		? ERTSTimedCommandState::Paused
		: ERTSTimedCommandState::Active;
	if (Instance.State == NewState)
	
		return false ;
		    if (bPaused)
		    {
		        if (!Instance.IsTimeFrozen()) Instance.FrozenTimeSeconds = CurrentTime;
		    }
		    else if (CurrentRate > 0.0)
		    {
		        const double Shift = CurrentTime - Instance.FrozenTimeSeconds;
		        Instance.BeginTimeSeconds += Shift;
		        Instance.EndTimeSeconds += Shift;
		        Instance.FrozenTimeSeconds = TNumericLimits<double>::Max() ;
	}
	Instance.State = NewState;
	return true;
}



bool FRTSTimedCommandMath::AddProgress(
	FRTSTimedCommandInstance& Instance,
	 float DeltaSeconds , double CurrentTime, double CurrentRate )
{
	const double Elapsed = FMath::Clamp(
		Instance.GetElapsedSecondsAtTime(CurrentTime) + DeltaSeconds,
		0.0,
		 static_cast<double>(Instance.DurationSeconds));
		    const double Progress = Instance.DurationSeconds <= UE_KINDA_SMALL_NUMBER
		        ? 1.0 : Elapsed / Instance.DurationSeconds;
		    const double Span = CurrentRate > 0.0 ? Instance.DurationSeconds / CurrentRate
		        : (Instance.EndTimeSeconds > Instance.BeginTimeSeconds
		            ? Instance.EndTimeSeconds - Instance.BeginTimeSeconds : Instance.DurationSeconds);
	const bool bFrozen = Instance.IsTimeFrozen();
	    const double EffectiveTime = bFrozen ? Instance.FrozenTimeSeconds : CurrentTime; Instance.BeginTimeSeconds = EffectiveTime - Progress * Span;
		    Instance.EndTimeSeconds = Instance.BeginTimeSeconds + Span;
		    Instance.FrozenTimeSeconds = bFrozen ? EffectiveTime : TNumericLimits<double>::Max();
		    return Progress >= 1.0; 
}

bool FRTSTimedCommandInstance::IsTimeFrozen() const { return FrozenTimeSeconds != TNumericLimits<double>::Max(); }

double FRTSTimedCommandInstance::GetProgress01() const
{
    return GetProgress01AtTime (IRTSCommandProgressProvider::Execute_GetCommandProgressTimeSeconds (Controller.Get() ) );
}

double FRTSTimedCommandInstance::GetElapsedSeconds() const
{
    return GetElapsedSecondsAtTime(IRTSCommandProgressProvider::Execute_GetCommandProgressTimeSeconds(Controller.Get()));
}

double FRTSTimedCommandInstance::GetRemainingSeconds() const
{
    return GetRemainingSecondsAtTime (IRTSCommandProgressProvider::Execute_GetCommandProgressTimeSeconds(Controller.Get()));
}

bool FRTSTimedCommandInstance::IsComplete() const
{
    return IsCompleteAtTime(IRTSCommandProgressProvider::Execute_GetCommandProgressTimeSeconds(Controller.Get() ));
}

double FRTSTimedCommandInstance::GetProgress01AtTime(double CurrentTime) const
{
    if (DurationSeconds <= UE_KINDA_SMALL_NUMBER) return 1.0;
    const double Span = EndTimeSeconds - BeginTimeSeconds;
    if (Span <= 0.0) return 0.0;
    const double EffectiveTime = IsTimeFrozen() ? FrozenTimeSeconds : CurrentTime;
    return FMath::Clamp((EffectiveTime - BeginTimeSeconds) / Span, 0.0, 1.0);
}

double FRTSTimedCommandInstance::GetElapsedSecondsAtTime(double CurrentTime) const
{
    return DurationSeconds * GetProgress01AtTime(CurrentTime);
}

double FRTSTimedCommandInstance::GetRemainingSecondsAtTime(double CurrentTime) const
{
    return DurationSeconds * (1.0 - GetProgress01AtTime(CurrentTime));
}

bool FRTSTimedCommandInstance::IsCompleteAtTime(double CurrentTime) const
{
    const double EffectiveTime = IsTimeFrozen() ? FrozenTimeSeconds : CurrentTime;
    return State == ERTSTimedCommandState::Active
        && (DurationSeconds <= UE_KINDA_SMALL_NUMBER
            || (EndTimeSeconds > BeginTimeSeconds && EffectiveTime >= EndTimeSeconds));
}

void FRTSTimedCommandMath::SetSpeedMultiplier(FRTSTimedCommandInstance& Instance, double CurrentTime, double NewRate)
{
    const double EffectiveTime = Instance.IsTimeFrozen() ? Instance.FrozenTimeSeconds : CurrentTime;
    const double Progress = Instance.GetProgress01AtTime(CurrentTime);
    if (NewRate > 0.0)
    {
        const double Anchor = Instance.State == ERTSTimedCommandState::Active ? CurrentTime : EffectiveTime;
        const double Span = Instance.DurationSeconds / NewRate;
        Instance.BeginTimeSeconds = Anchor - Progress * Span;
        Instance.EndTimeSeconds = Instance.BeginTimeSeconds + Span;
        Instance.FrozenTimeSeconds = Instance.State == ERTSTimedCommandState::Active
            ? TNumericLimits<double>::Max() : Anchor;
    }
    else Instance.FrozenTimeSeconds = EffectiveTime;
}
