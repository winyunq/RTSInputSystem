// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSSelectionStructs.h"
#include "RTSFormationListWidget.generated.h"

class UPanelWidget;
class UHorizontalBox;
class URTSControlGroupButton;

/** Persistent 0-9 control-group strip for the bottom UnitDetailPanel header. 
 * @brief 显示玩家的控制组列表。
 * @details 复用槽位控件，控制组变化时用快照刷新，并按当前配置应用显示样式。
 * 
 * ```
 * 控制组视图 + 编队样式配置 → 控件槽位池 → 编队列表
 * ```
 */
UCLASS(BlueprintType, Blueprintable)
class RTSINPUTSYSTEM_API URTSFormationListWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UFUNCTION()
	virtual void OnControlGroupsUpdated(const FRTSControlGroupsView& View);

	/** Optional visual subclass. Native URTSControlGroupButton is used when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Control Groups")
	TSubclassOf<URTSControlGroupButton> ControlGroupButtonClass;

	/** Maximum slot width; the shared command-button proportions determine the final size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Control Groups", meta = (ClampMin = "1"))
	float FormationSlotWidth = 102.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Control Groups", meta = (ClampMin = "1"))
	int32 FormationSlotHeight = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Control Groups", meta = (ClampMin = "0"))
	float FormationSlotGap = 4.0f;

	/** Existing UnitFormationList asset already authors this exact root name. */
	UPROPERTY(meta = (BindWidget))
	UPanelWidget* FormationSlotContainer;

	UFUNCTION(BlueprintImplementableEvent, Category = "RTS Control Groups")
	void OnControlGroupListChanged(const FRTSControlGroupsView& View);

private:
	UPROPERTY()
	TArray<URTSControlGroupButton*> ControlGroupButtons;


	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> ControlGroupGrid;

	FMargin FormationEdgePadding;

	/**
	 * @brief 计算编队列表的槽位宽高和间距。
	 * @details 用命令面板按钮宽高比、外围留白和当前设置共同限制槽位宽度，再按同一宽高比计算高度。
	 */
	void ApplyFormationSettings();
	void BuildSlotPool();
	void RefreshControlGroups(const FRTSControlGroupsView& View);
};
