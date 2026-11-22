// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RTSCommandButton.h"
#include "RTSCommandGridAsset.generated.h"

/**
 * @brief 集中保存一个命令面板的按钮定义。
 * @details 面板读取 GetAllButtons 获得该组可显示的按钮，派生面板可通过同一入口提供自己的定义。
 * 
 * ```
 * 面板资产 / 派生列表 → GetAllButtons → 槽位布局和选区命令索引
 * ```
 * @par Example / 用例
 * Give an infantry unit a grid containing its Move and Attack buttons, then select the unit in the test map and check that both buttons appear. / 给步兵单位的网格配置移动和攻击按钮，在测试地图里选中该单位，确认两个按钮都出现在命令面板。
 * @ProductDocument
 */
UCLASS(BlueprintType)
class RTSINPUTSYSTEM_API URTSCommandGridAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// A unified collection of buttons defined in this grid.
    // Placement is determined by the PreferredIndex (0-14) of each button.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grid")
	TArray<TObjectPtr<URTSCommandButton>> Buttons;

	// Returns all buttons in this grid. Can be overridden by subclasses for code-defined grids.
	UFUNCTION(BlueprintCallable, Category = "Grid")
	virtual TArray<URTSCommandButton*> GetAllButtons() const
	{
		TArray<URTSCommandButton*> Result;
		for (auto& Btn : Buttons) if (Btn) Result.Add(Btn);
		return Result;
	}
};
