// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "RTSSelectionStructs.h"
#include "RTSSelectionQueryButton.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRTSSelectionQueryExecuted, int32, SelectedUnitCount);

/** Generic, directly placeable UMG button for Mass-first world-wide selection queries. 
 * @brief 通过配置查询条件触发一次快速选区。
 * @details 点击时从按钮配置和键盘修饰键取得查询模式，交给选区系统并发送查询事件。
 * 
 * ```
 * 查询配置 + 修饰键 → ExecuteSelectionQuery → 选区结果
 * ```
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RTS Selection Query Button"))
class RTSINPUTSYSTEM_API URTSSelectionQueryButton : public UButton
{
	GENERATED_BODY()

public:
	URTSSelectionQueryButton();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Quick Selection")
	FRTSSelectionQuery SelectionQuery;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Quick Selection")
	ERTSSelectionModifier SelectionModifier = ERTSSelectionModifier::Replace;

	/** Shift/Ctrl at click time override Add/Remove behavior when enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Quick Selection")
	bool bUseKeyboardModifiers = true;

	UPROPERTY(BlueprintAssignable, Category = "RTS Quick Selection")
	FOnRTSSelectionQueryExecuted OnSelectionQueryExecuted;

	/**
	 * @brief 用配置的查询条件改变本地选区。
	 * @details 读取有效查询定义。可在按住 Shift 或 Ctrl 时覆盖为追加或移除，结果通过 OnSelectionQueryExecuted 发送。
	 * @return 选区查询返回的单位数量。取不到选区子系统时返回 0。
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Quick Selection")
	int32 ExecuteSelectionQuery();

protected:
	virtual void OnWidgetRebuilt() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual FRTSSelectionQuery GetEffectiveSelectionQuery() const;

private:
	UFUNCTION()
	void HandleClicked();
};

/** Convenience palette control: selects every controllable military unit. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RTS Select All Army Button"))
class RTSINPUTSYSTEM_API URTSSelectAllArmyButton : public URTSSelectionQueryButton
{
	GENERATED_BODY()

protected:
	virtual FRTSSelectionQuery GetEffectiveSelectionQuery() const override;
};

/** Convenience palette control: selects every Mass unit carrying the Idle flag in the army category. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RTS Select All Idle Army Button"))
class RTSINPUTSYSTEM_API URTSSelectAllIdleArmyButton : public URTSSelectionQueryButton
{
	GENERATED_BODY()

protected:
	virtual FRTSSelectionQuery GetEffectiveSelectionQuery() const override;
};
