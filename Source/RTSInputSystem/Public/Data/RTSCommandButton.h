// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "Commands/RTSCommandRequirements.h"
#include "RTSSelectionStructs.h"
#include "GameFramework/PlayerController.h"
#include "RTSCommandButton.generated.h"

class UMassBattleAgentConfigDataAsset;

UENUM(BlueprintType)
enum class ERTSCommandTargetType : uint8
{
	Instant UMETA(DisplayName = "Instant"),
	Location UMETA(DisplayName = "Location"),
	TargetActor UMETA(DisplayName = "Target Actor"),
	LocationOrTarget UMETA(DisplayName = "Location or Target")
};

/**
 * @brief 保存命令卡按钮的配置并提供执行入口。
 * @details 定义保存命令标签、目标种类和可用条件。选区构建索引时写入 CommandSelection，派生按钮可以覆盖 Execute。
 * 
 * ```
 * 面板配置 → 原按钮 → 命令控件
 * 已匹配目标 → Execute → 当前执行实现
 *                      └→ 基类经 CommandSelection 提交
 * ```
 * @ProductDocument
 * @note 待接入的调用契约。选区或命令面板变化时确定每个按钮的受令组，将组引用和执行动作各绑定一次。两个 Execute 重载只接收目标，执行阶段直接使用该按钮的绑定。单位命令与确认反馈沿用已有发令入口。
 */
UCLASS(BlueprintType)
class RTSINPUTSYSTEM_API URTSCommandButton : public UDataAsset
{
	GENERATED_BODY()

public:

	// The logical command to execute when clicked (e.g. RTS.Command.Move)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Command")
	FGameplayTag CommandTag;

	/** Local presentation context; a command owner may be a component as well as an Actor. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> CommandContext;

	/** A running instance of this button replaces weapons/armor with research progress. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Command")
	bool bIsResearch = false;

	/** Repeatable research keeps its command-card button and queues a copy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Command", meta = (EditCondition = "bIsResearch"))
	bool bRepeatableResearch = false;

	// Does this command require a target location or actor?
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Command")
	ERTSCommandTargetType TargetType = ERTSCommandTargetType::Instant;

	// The icon to display in the grid
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	TObjectPtr<UTexture2D> Icon;

	// The tooltip title
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FText DisplayName;

	// The detailed tooltip description
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FText Description;

	// The preferred index in the 15-grid (0-14). (-1 = Auto)
	// Row 1: 0-4 (Basic Commands)
	// Row 2: 5-9 (Advanced/Stop/Hold)
	// Row 3: 10-14 (Abilities)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (ClampMin = "-1", ClampMax = "14"))
	int32 PreferredIndex = -1;

	// The hotkey for this button
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	FKey Hotkey;

	/**
	 * Building footprint measured in logical placement-grid cells.
	 * Zero uses URTSInputPanelSettings::HashGridSelectionFootprintCells.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FIntPoint PlacementFootprintCells = FIntPoint::ZeroValue;

	// Should this button be hidden if the command is unavailable?
	// If false, it will be shown as disabled (greyed out).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	bool bHideIfUnavailable = false;

    // Base cooldown in seconds
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GameLogic")
    float DefaultCooldown = 0.0f;

    // Does this command support auto-cast toggle?
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GameLogic")
    bool bAllowAutoCast = false;

    // --- Costs ---
    // Minerals / Low Value Resource
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cost")
    int32 LowValueCost = 0;

    // Gas / High Value Resource
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cost")
    int32 HighValueCost = 0;

    // --- Fluff / Info ---
    // E.g. "Production Building", "Infantry", "Vehicle"
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Info")
    FText Role;

    // Legacy descriptive tags only; executable conditions belong to the command definition.
    // The Tag Name (GetTagName) remains visible in legacy tooltips.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Logic")
    TArray<FGameplayTag> Requirements;

	/** Final building configuration used for the placement preview. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	TSoftObjectPtr<UMassBattleAgentConfigDataAsset> PlacementPreviewUnit;

    
    

	/**
	 * Optional alternate-click hook used by non-Actor command sources.
	 * Return true when the button handled the alternate click.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
	bool HandleAlternateClick(UObject* WorldContextObject, AActor* Executor);
	virtual bool HandleAlternateClick_Implementation(UObject* WorldContextObject, AActor* Executor);

	/**
	 * Optional auto-cast state hook used when there is no Actor command context.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
	bool IsAutoCastEnabledForContext(UObject* WorldContextObject, AActor* Executor) const;
	virtual bool IsAutoCastEnabledForContext_Implementation(UObject* WorldContextObject, AActor* Executor) const;

	/**
	 * One command-state query supplies visibility, availability and its explanation.
	 * @ProductDocument
	 */
	virtual FRTSCommandState GetCommandStateForContext(
		const UObject* WorldContextObject, const AActor* Executor, FName SourceId = NAME_None) const;

	/** Optional availability hook for specialized command-button subclasses. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
	bool IsAvailableForContext(UObject* WorldContextObject, AActor* Executor) const;
	/**
	 * @brief 按当前业务对象的接口判断按钮是否可用。
	 * @details 优先询问 CommandContext 中的命令接口，其次询问 Executor 的同一接口。没有可询问对象时返回 true。
	 * @param WorldContextObject 沿用的世界上下文参数。
	 * @param Executor 备用命令上下文。
	 * @return 命令接口给出的可用状态，或缺少接口时的默认值 true。
	 */
	virtual bool IsAvailableForContext_Implementation(
		UObject* WorldContextObject,
		AActor* Executor) const;

	/** Description and prerequisites evaluated for the player viewing the shared tooltip. */
	virtual FText GetTooltipDescriptionForContext(
		const UObject* WorldContextObject, const AActor* Executor) const;

	/** Presentation name evaluated for this viewer without changing the shared definition. */
	virtual FText GetDisplayNameForContext(const UObject* WorldContextObject, const AActor* Executor) const
	{
		return DisplayName;
	}

	/**
	 * State owners read by this definition's availability query; binding is managed by the shared widget.
	 * @ProductDocument
	 * @brief 为命令控件收集应观察的状态对象。
	 * @details 研究按钮添加 CommandContext。普通按钮在其命令接口能提供当前状态时添加同一对象。
	 * @param WorldContextObject 沿用的世界上下文参数。
	 * @param Executor 沿用的执行者上下文。
	 * @param OutDependencies 追加状态对象的数组。使用唯一追加。
	 */
	virtual void GetCommandStateDependencies(const UObject* WorldContextObject,
		const AActor* Executor, TArray<UObject*>& OutDependencies) const;

	/** Optional queue badge value. Zero hides the badge. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
	int32 GetQueueCountForContext(UObject* WorldContextObject, AActor* Executor) const;
	virtual int32 GetQueueCountForContext_Implementation(UObject* WorldContextObject, AActor* Executor) const;

	

	/**
	 * @brief 当前给该按钮定义建立索引的选区子系统。
	 * @details 由 @ref URTSSelectionSubsystem::RebuildCommandIndex 写入。基类 Execute 使用此引用提交命令。
	 */
	UPROPERTY(Transient) TObjectPtr<class URTSSelectionSubsystem> CommandSelection;

	/**
	 * @brief 执行无目标或以单位为目标的命令。
	 * @details 基类把按钮的命令标签和目标句柄交给 @ref URTSSelectionSubsystem::SubmitCommandContext 。选区子系统在提交时按标签取得已建立索引的响应组。
	 * @param Target 被命令指向的 Mass 实体句柄。无目标命令传空句柄。
	 */
	virtual void Execute(FEntityHandle Target = {});
	/**
	 * @brief 执行以世界坐标为目标的命令。
	 * @details 基类将按钮的命令标签和位置交给 @ref URTSSelectionSubsystem::SubmitCommandContext 。响应组由提交时的选区命令索引提供。
	 * @param Target 玩家指定的目标世界坐标。
	 */
	virtual void Execute(const FVector& Target);

    TDelegate<void(FEntityHandle)> OnExecuteTarget;
};
