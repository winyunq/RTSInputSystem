// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Data/RTSCommandGridAsset.h"
#include "Commands/RTSCommandRequirements.h"
#include "RTSSelectionStructs.h"
#include "RTSCommandInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class URTSCommandInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * @brief 定义命令上下文可提供的面板、条件、状态和执行入口。
 * @details 按钮或面板可以向实现读取可用条件及状态，接口另外提供无目标、位置和实体目标执行形式。
 * 
 * ```
 * 业务上下文 → 面板 / 命令条件 / 当前状态
 * 调用方的命令 + 对应目标 → 实现的执行接口
 * ```
 * @par Example / 用例
 * An engineer can implement a location command for construction and query availability for the corresponding button. Its command grid returns the configured build button. / 工程兵可实现按位置建造的命令，并为对应按钮提供可用状态。该单位的命令网格返回配置好的建造按钮。
 * @ProductDocument
 */
class RTSINPUTSYSTEM_API IRTSCommandInterface
{
	GENERATED_BODY()

public:
	
	/**
	 * Returns the Command Grid Asset for this object.
	 * This defines the buttons shown in the 15-grid UI.
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
	class URTSCommandGridAsset* GetCommandGrid();

    // Returns time remaining in seconds. 0 = Ready. -1 = Not Applicable.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    float GetCooldownRemaining(FGameplayTag CommandTag);

    // Returns true if auto-cast is active for this command.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    bool IsAutoCastEnabled(FGameplayTag CommandTag);

    // Toggles auto-cast state. 
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void ToggleAutoCast(FGameplayTag CommandTag);

    /**
     * Read-only command result shared by the command button and tooltip.
     * @ProductDocument
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    FRTSCommandState QueryCommandState(FGameplayTag CommandTag, FName SourceId = NAME_None);

    /**
     * State objects read by this command; UI retains its original dependency subscriptions.
     * @ProductDocument
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void GetCommandStateDependencies(FGameplayTag CommandTag, FName SourceId, TArray<UObject*>& OutDependencies);

    /**
     * Supplies existing state facts; the common evaluator owns condition composition.
     * @ProductDocument
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    bool QueryCommandRequirementFact(const FRTSRequirementNode& Node, FName SourceId, FRTSRequirementFact& OutFact);

    /**
     * @brief 查询指定命令和来源当前是否可用。
     * @ProductDocument
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    bool IsCommandAvailable(FGameplayTag CommandTag, FName SourceId = NAME_None);

    /**
     * Optional live description from the command owner. Empty keeps the authored button tooltip.
     * @ProductDocument
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    FText GetCommandDescription(FGameplayTag CommandTag, FName SourceId = NAME_None);

    /**
     * @brief 执行无需目标的命令。
     * @ProductDocument
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void ExecuteCommand(FGameplayTag CommandTag);

    /**
     * @brief 对指定世界位置执行命令。
     * @ProductDocument
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void ExecuteCommandWithLocation(FGameplayTag CommandTag, FVector TargetLocation);

    /**
     * @brief 对指定 Actor 执行命令。
     * @ProductDocument
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void ExecuteCommandWithTarget(FGameplayTag CommandTag, AActor* TargetActor);

    /** 
     * Sets a specific grid asset to be displayed for this actor. 
     * Used for sub-menus / hierarchical navigation.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void SetCommandGrid(class URTSCommandGridAsset* NewGrid);

	
};
