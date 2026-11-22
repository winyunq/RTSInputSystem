// Copyright 2026 Winyunq. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/RTSBuiltinCommandButton.h"
#include "RTSCmd_SubMenu.generated.h"

/**
 * A specialized button that opens another command grid (Sub-menu).
 * @brief 让当前命令面板进入子菜单。
 * @details Execute 优先通过 TargetLoadoutId 解析子面板，也可提交已配置的 TargetGrid 导航请求。
 * 
 * ```
 * Execute → TargetLoadoutId → 按配置进入子面板
 *        └→ TargetGrid → 发送面板导航请求
 * ```
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_SubMenu : public URTSBuiltinCommandButton
{
	GENERATED_BODY()

public:
    /** The grid to open when this button is clicked. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Command")
    TObjectPtr<class URTSCommandGridAsset> TargetGrid;

    /**
     * @brief 进入所配置的子命令面板。
     * @details 优先按 TargetLoadoutId 解析目标。空标识时将非空 TargetGrid 交给命令子系统的导航事件。
     * @param Target 保留的无目标命令输入，子面板从按钮配置取得。
     */
    virtual void Execute(FEntityHandle Target = {}) override;

    

    UPROPERTY() FName TargetLoadoutId;
};
