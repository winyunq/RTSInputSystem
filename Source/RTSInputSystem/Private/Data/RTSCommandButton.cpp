/**
 * @file RTSCommandButton.cpp
 * @brief 实现命令按钮的上下文查询和两个目标入口。
 * @details 调用方解析目标后交给 Execute。此版本由按钮持有的 CommandSelection 处理提交，受令组在选择子系统中按标签读取。
 * @code{.unparsed}
 * 单位与命令上下文 → 可用性 / 显示状态
 * FEntityHandle 或 FVector → Execute(Target)
 *                             ↓
 *                      SubmitCommandContext(标签, 目标)
 *                             ↓
 *                      当前受令组 → 已登记的动作
 * @endcode
 */
#include "Data/RTSCommandButton.h"
#include "Interfaces/RTSCommandInterface.h"
#include "RTSSelectionSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

void URTSCommandButton::GetCommandStateDependencies(const UObject* WorldContextObject,
	const AActor* Executor, TArray<UObject*>& OutDependencies) const
{
	if (UObject* Context = CommandContext.Get())
	{
		if (bIsResearch || (Context->Implements<URTSCommandInterface>()
			&& IRTSCommandInterface::Execute_QueryCommandState(Context, CommandTag, NAME_None).bHandled))
			OutDependencies.AddUnique(Context);
	}
}



bool URTSCommandButton::HandleAlternateClick_Implementation(UObject* WorldContextObject, AActor* Executor)
{
    return false;
}

bool URTSCommandButton::IsAutoCastEnabledForContext_Implementation(UObject* WorldContextObject, AActor* Executor) const
{
    return false;
}

FRTSCommandState URTSCommandButton::GetCommandStateForContext(
	const UObject* WorldContextObject, const AActor* Executor, FName SourceId) const
{
	if (UObject* Context = CommandContext.Get(); Context && Context->Implements<URTSCommandInterface>())
	{
		const FRTSCommandState State = IRTSCommandInterface::Execute_QueryCommandState(Context, CommandTag, SourceId);
		if (State.bHandled) return State;
	}
	if (Executor && Executor != CommandContext.Get() && Executor->Implements<URTSCommandInterface>())
	{
		const FRTSCommandState State = IRTSCommandInterface::Execute_QueryCommandState(
			const_cast<AActor*>(Executor), CommandTag, SourceId);
		if (State.bHandled) return State;
	}

	// Existing specialized buttons keep their original hooks until their command
	// owner supplies a handled state. This path never calls this query recursively.
	FRTSCommandState State;
	State.bAvailable = IsAvailableForContext(const_cast<UObject*>(WorldContextObject), const_cast<AActor*>(Executor));
	State.Description = GetTooltipDescriptionForContext(WorldContextObject, Executor);
	if (LowValueCost > 0) State.Costs.Add({TEXT("RTS.Technology.Resource.LowValue"), LowValueCost});
	if (HighValueCost > 0) State.Costs.Add({TEXT("RTS.Technology.Resource.HighValue"), HighValueCost});
	return State;
}

bool URTSCommandButton::IsAvailableForContext_Implementation(
	UObject* WorldContextObject,
	AActor* Executor) const
{
	if (UObject* Context = CommandContext.Get(); Context && Context->Implements<URTSCommandInterface>())
	{
		return IRTSCommandInterface::Execute_IsCommandAvailable(Context, CommandTag, NAME_None);
	}
	if (Executor && Executor->Implements<URTSCommandInterface>())
		return IRTSCommandInterface::Execute_IsCommandAvailable(Executor, CommandTag, NAME_None);
	return true;
}

FText URTSCommandButton::GetTooltipDescriptionForContext(
	const UObject* WorldContextObject, const AActor* Executor) const
{
	if (UObject* Context = CommandContext.Get(); Context && Context->Implements<URTSCommandInterface>())
	{
		const FText LiveDescription = IRTSCommandInterface::Execute_GetCommandDescription(Context, CommandTag, NAME_None);
		if (!LiveDescription.IsEmpty()) return LiveDescription;
	}
	if (Executor && Executor != CommandContext.Get() && Executor->Implements<URTSCommandInterface>())
	{
		const FText LiveDescription = IRTSCommandInterface::Execute_GetCommandDescription(
			const_cast<AActor*>(Executor), CommandTag, NAME_None);
		if (!LiveDescription.IsEmpty()) return LiveDescription;
	}
	FString Tooltip = Description.ToString();
	for (const FGameplayTag& Requirement : Requirements)
	{
		Tooltip += FString::Printf(TEXT("\n前置条件：%s"), *Requirement.ToString());
	}
	return FText::FromString(Tooltip);
}

int32 URTSCommandButton::GetQueueCountForContext_Implementation(UObject* WorldContextObject, AActor* Executor) const
{
	return 0;
}





void URTSCommandButton::Execute(FEntityHandle Target)
{
    if (!OnExecuteTarget.ExecuteIfBound(Target))
    {
        CommandSelection->SubmitCommandContext(CommandTag, {}, nullptr, Target);
    }
}

void URTSCommandButton::Execute(const FVector& Target)
{
    CommandSelection->SubmitCommandContext(CommandTag, {}, &Target);
}
