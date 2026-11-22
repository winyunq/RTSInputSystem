// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/UniformGridPanel.h"
#include "Data/RTSCommandGridAsset.h"
#include "RTSCommandButtonWidget.h"
#include "RTSActiveGroupWidget.h" 
#include "RTSCommanderGridWidget.generated.h"

class URTSSelector;

/**
 * The 3x5 Grid Container.
 * Manages 15 RTSCommandButtonWidgets.
 * @brief 显示命令面板并把点击和热键交给命令入口。
 * @details 按面板定义安排槽位并复用按钮控件。点击交付控件持有的原按钮。
 * 
 * ```
 * 面板配置 → 槽位 → 命令控件
 * 鼠标 / 热键 → 点击原按钮 ┬→ 需要目标：开始瞄准
 *                          └→ 即时命令：交给选区执行
 * ```
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCommanderGridWidget : public URTSActiveGroupWidget
{
	GENERATED_BODY()

public:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativePreConstruct() override;
	virtual void SynchronizeProperties() override;

	// Override to update grid when active group changes
	virtual void OnSelectionUpdated(const FRTSSelectionView& View) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// The Grid Panel to hold buttons
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUniformGridPanel> CommandGridPanel;

	// Class to spawn for each button. 
    // MUST be set to a valid WBP in the details panel for the grid to appear.
	UPROPERTY(EditAnywhere, Category = "RTS Grid")
	TSubclassOf<URTSCommandButtonWidget> ButtonParams;
    
    // Padding between buttons
    UPROPERTY(EditAnywhere, Category = "RTS Grid")
    FMargin SlotPadding = FMargin(4.0f);

    // Button footprint, excluding SlotPadding. Applied to the shared button widget.
    UPROPERTY(EditAnywhere, Category = "RTS Grid")
    FVector2D ButtonSize = FVector2D(144.0f, 144.0f);

	// Internal list of buttons (Keys = Index 0-14)
	UPROPERTY()
	TArray<TObjectPtr<URTSCommandButtonWidget>> GridButtons;

	/** Fixed card cells own layout; button instances move independently between parents. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<class USizeBox>> GridSlots;

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UTextBlock>> GridHotkeyLabels;

	void UpdateSlotHotkey(int32 SlotIndex);

	UPROPERTY(Transient)
	TArray<TObjectPtr<URTSCommandButtonWidget>> CommandButtonInstances;

	/** Widget ownership only. Timing, capacity and cancellation remain in the existing command providers. */
	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<URTSCommandButtonWidget>> ResearchButtons;

	/**
	 * @brief 为命令定义取得可复用的界面按钮。
	 * @details 写入当前单位的业务上下文，按定义查找已创建控件。单次研究还按进度实例或拥有者区分。需要新控件时绑定统一的点击回调。
	 * @param Definition 用于显示和执行的原按钮定义。
	 * @param Owner 当前面板单位的展示和业务上下文。
	 * @return 可用于该定义的控件。缺少定义、控件类型或创建失败时为空。
	 */
	URTSCommandButtonWidget* GetCommandButtonInstance(URTSCommandButton* Definition, const FRTSUnitData& Owner);

	// Populate the grid based on data
	void RefreshGrid(const TArray<URTSCommandButton*>& Buttons);

	// Generate the 15 empty slots on Init
	void InitGridSlots();

    // 统一网格填充逻辑（支持 PreferredIndex 与自动空位）
    // @brief 将命令定义填入面板槽位。
    // @details 优先使用有效且空闲的首选槽位，其余定义按顺序填入空位。
    // @param Grid 提供命令定义的面板。
    // @param OutButtons 按槽位排列的输出数组。空槽保留空指针。
    void PopulateSparseButtons(URTSCommandGridAsset* Grid, TArray<URTSCommandButton*>& OutButtons);

	/**
	 * @brief 将被点击的原按钮交给命令输入流程。
	 * @details 从控件读取命令定义。能开始瞄准时保留定义等待目标，否则以空目标调用选区执行入口。
	 * @param Button 发出点击通知的命令控件。
	 */
	UFUNCTION()
		void OnGridButtonClicked(URTSCommandButtonWidget* Button);
	
    /** 响应 Actor 内部触发的网格变更通知 */
    UFUNCTION()
    void OnActorGridChanged();

    /** 响应全局导航请求（瞬态子菜单） */
    UFUNCTION()
    void OnCommandNavigationRequested(URTSCommandGridAsset* NewGrid);
	
	// Command resolver: Find asset for given unit ID
	// For now, this will just use a hardcoded reference or basic logic until we add the Interface/Trait
	URTSCommandGridAsset* ResolveGridForUnit(const FString& UnitName);

    // 纯 Push (Set/Reset) 模型辅助函数
    UFUNCTION(BlueprintCallable, Category = "RTS Grid")
    void UpdateGrid(URTSCommandGridAsset* NewGrid);

    UFUNCTION(BlueprintCallable, Category = "RTS Grid")
    void RefreshVisuals();

	void RegisterCommandPanelHotkeys();
	void RebuildCommandPanelHotkeys();
	void UnregisterCommandPanelHotkeys();
	/**
	 * @brief 执行命令热键对应槽位的点击。
	 * @details 槽位有效且按钮可见时，调用和鼠标点击相同的 HandleClicked 入口。
	 * @param SlotIndex 热键对应的命令面板槽位下标。
	 */
	void ExecuteCommandPanelSlot(int32 SlotIndex);
	void HandleCommandPanelHotkeyPressed(int32 SlotIndex, const FKey& Hotkey);
	void HandleCommandPanelHotkeyRepeated(int32 SlotIndex, const FKey& Hotkey);
	void HandleCommandPanelHotkeyReleased(const FKey& Hotkey);
	void ClearHeldCommandHotkeyState();
	void ConfirmPendingTargetWithHotkey(URTSSelector* Selector, const FKey& Hotkey, bool bRapidFire = false);
	/**
	 * @brief 让命令面板标记正在瞄准或正在执行的命令。
	 * @details 瞄准期间优先使用待提交命令标签，其余时刻读取活动组单位的共同命令。逐个可用槽位按精确标签匹配设置状态。
	 */
	void UpdateCommandStateVisuals();
	void PositionSharedTooltip();

    // Cache the active actor for context
    TWeakObjectPtr<AActor> ActiveActorPtr;

	UPROPERTY(Transient)
	TObjectPtr<class UInputComponent> CommandPanelInputComponent;

	TWeakObjectPtr<APlayerController> CommandPanelInputOwner;
	FKey HeldCommandHotkey;
	int32 HeldCommandSlotIndex = INDEX_NONE;
	double LastCommandHotkeyRepeatTime = -1.0;
	bool bHasRepeatedCommandHotkey = false;

    /** 当前正在显示的网格资产 (托管状态) */
    UPROPERTY()
    TWeakObjectPtr<URTSCommandGridAsset> CurrentGridAsset;

    // 保存当前的视图数据，以便在点击子网格后刷新时复用
    FRTSSelectionView LastSelectionView;

	FDelegateHandle CommandNavigationHandle;

	// Test Asset for debugging
	UPROPERTY(EditAnywhere, Category = "Debug")
	TObjectPtr<URTSCommandGridAsset> DebugGridAsset;

    // --- Shared Tooltip Logic ---
protected:
    UPROPERTY(EditAnywhere, Category = "UI")
    TSubclassOf<class URTSTooltipWidget> TooltipClass;

    // If true, tooltip stays at a fixed offset from the Grid instead of following mouse
    UPROPERTY(EditAnywhere, Category = "UI")
    bool bFixedTooltipAboveGrid = true;

    // Y-Offset from Grid top when bFixedTooltipAboveGrid is true
    UPROPERTY(EditAnywhere, Category = "UI")
    float TooltipYOffset = -12.0f;

    UPROPERTY(EditAnywhere, Category = "UI")
    FVector2D TooltipMouseOffset = FVector2D(28.0f, 24.0f);

    UPROPERTY(EditAnywhere, Category = "UI", meta = (ClampMin = "0.0"))
    float TooltipViewportMargin = 12.0f;

    UPROPERTY()
    TObjectPtr<class URTSTooltipWidget> SharedTooltip;

	TWeakObjectPtr<URTSCommandButtonWidget> HoveredTooltipButton;

public:
    // Called by child buttons
    void NotifyButtonHovered(URTSCommandButtonWidget* Btn, URTSCommandButton* Data);
    void NotifyButtonUnhovered(URTSCommandButtonWidget* Btn);

	/** Both card and queue use this button class. */
	TSubclassOf<URTSCommandButtonWidget> GetCommandButtonWidgetClass() const
	{
		return ButtonParams;
	}

	FVector2D GetButtonSize() const { return ButtonSize; }
	FMargin GetSlotPadding() const { return SlotPadding; }

	URTSCommandButtonWidget* AcquireResearchButton(const FRTSTimedCommandInstance& Item, const FRTSUnitData& Owner);
	void ReleaseFinishedResearchButtons(const FRTSUnitData& Owner,
		FGuid ResolvedId = FGuid(), bool bCancelled = false);

	URTSCommandButton* FindDisplayedCommandButton(FGameplayTag Tag) const
	{
		for (const URTSCommandButtonWidget* Widget : GridButtons)
			if (Widget && Widget->GetData() && Widget->GetData()->CommandTag == Tag)
				return Widget->GetData();
		return nullptr;
	}


	UPROPERTY(Transient) TObjectPtr<class URTSSelector> CommandSelector;
};
