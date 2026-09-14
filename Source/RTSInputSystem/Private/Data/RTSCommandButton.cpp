#include "Data/RTSCommandButton.h"
#include "Interfaces/RTSCommandInterface.h"
#include "RTSSelectionSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

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

void URTSCommandButton::Execute_Implementation(AActor* Executor)
{
    // 二进制逻辑：按钮通过信号中心（Subsystem）广播自己的意图。
    // 这消除了对特定 UI 或 选拔 插件的硬依赖。
    if (!CommandTag.IsValid()) return;

    UWorld* World = Executor ? Executor->GetWorld() : nullptr;
    if (!World && GEngine)
    {
        // Mass/landmark buttons usually execute without an Actor. Prefer the
        // live game world explicitly; choosing the editor world first makes
        // GetFirstLocalPlayerFromController() return null and drops the click.
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
            {
                World = Context.World();
                break;
            }
        }

        if (!World)
        {
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::Editor)
                {
                    World = Context.World();
                    break;
                }
            }
        }
    }
    if (!World)
    {
        // Fallback: 在无上下文输入时优先从本地玩家控制器拿世界。
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            World = PC->GetWorld();
        }
    }

    if (!World) return;

    if (ULocalPlayer* LP = World->GetFirstLocalPlayerFromController())
    {
        if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
        {
            // The selection subsystem is the shared dispatch boundary for both
            // Mass entities and actor-backed units. Going straight to the Mass
            // command subsystem drops instant commands for a single actor unit.
            Selection->IssueCommand(CommandTag);
        }
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
