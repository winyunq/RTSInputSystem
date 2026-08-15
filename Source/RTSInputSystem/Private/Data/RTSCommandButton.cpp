#include "Data/RTSCommandButton.h"
#include "RTSSelectionSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

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

bool URTSCommandButton::IsAvailableForContext_Implementation(
	UObject* WorldContextObject,
	AActor* Executor) const
{
	return true;
}

int32 URTSCommandButton::GetQueueCountForContext_Implementation(UObject* WorldContextObject, AActor* Executor) const
{
	return 0;
}
