// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "RTSSelectionStructs.h"
#include "RTSControlGroupButton.generated.h"

class UImage;
class UTextBlock;
class URTSSelectionSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRTSControlGroupButtonStateChanged, const FRTSControlGroupView&, GroupView);

/**
 * A directly placeable UMG control-group slot.
 *
 * Plain click recalls, double-click recalls and centers, Ctrl-click replaces,
 * Shift-click toggles membership, and Alt-click steals the selected units from
 * every other group before replacing this one.
 * @brief 呈现并操作一个编号控制组。
 * @details 订阅控制组视图更新，用当前快照调整外观、数量和提示。点击交给选区子系统处理编组操作。
 * 
 * ```
 * 控制组视图 → 当前槽位 → 图标 / 数量 / 状态
 * 点击及修饰键 → 控制组选区操作
 * ```
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RTS Control Group Button"))
class RTSINPUTSYSTEM_API URTSControlGroupButton : public UButton
{
	GENERATED_BODY()

public:
	URTSControlGroupButton();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Control Groups", meta = (ClampMin = "0", ClampMax = "9"))
	int32 ControlGroupIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Control Groups")
	bool bUseKeyboardModifiers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Control Groups")
	bool bCenterOnDoubleClick = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Control Groups|Style")
	FLinearColor EmptyGroupColor = FLinearColor(0.025f, 0.045f, 0.07f, 0.75f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Control Groups|Style")
	FLinearColor AssignedGroupColor = FLinearColor(0.035f, 0.08f, 0.12f, 0.96f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Control Groups|Style")
	FLinearColor ActiveGroupColor = FLinearColor(0.35f, 1.0f, 0.55f, 1.0f);

	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	FRTSControlGroupView CurrentGroupView;

	UPROPERTY(BlueprintAssignable, Category = "RTS Control Groups")
	FOnRTSControlGroupButtonStateChanged OnControlGroupStateChanged;

	UFUNCTION(BlueprintCallable, Category = "RTS Control Groups")
	void SetControlGroupIndex(int32 InGroupIndex);

	UFUNCTION(BlueprintCallable, Category = "RTS Control Groups")
	void ApplyControlGroupView(const FRTSControlGroupView& GroupView);

	UFUNCTION(BlueprintCallable, Category = "RTS Control Groups")
	void RefreshControlGroupState();

	/** Lets an owning native bar supply its compact portrait/number/count widgets. */
	void SetPresentationWidgets(UImage* InIcon, UTextBlock* InNumber, UTextBlock* InCount, UImage* InActiveFrame = nullptr);

protected:
	virtual void OnWidgetRebuilt() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UImage> GroupIcon;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GroupNumberText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GroupCountText;

	UPROPERTY(Transient)
	TObjectPtr<UImage> GroupActiveFrame;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DefaultLabel;

	double LastRecallClickTime = -1.0;

	/**
	 * @brief 根据点击修饰键执行该槽位的控制组动作。
	 * @details Alt 转移并替换成员，Ctrl 替换成员，Shift 切换成员。普通点击召回控制组，连续召回满足设置的时间阈值时请求聚焦镜头。
	 */
	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleControlGroupsChanged(const FRTSControlGroupsView& GroupsView);

	URTSSelectionSubsystem* GetSelectionSubsystem() const;
	void BindToSelectionSubsystem();
	void UnbindFromSelectionSubsystem();
	FText BuildTooltip(const FRTSControlGroupView& GroupView) const;
};
