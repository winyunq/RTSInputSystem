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
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCommanderGridWidget : public URTSActiveGroupWidget
{
	GENERATED_BODY()

public:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
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

    // Desired size for buttons (if enforced by logic, though usually WBP controls this)
    UPROPERTY(EditAnywhere, Category = "RTS Grid")
    FVector2D ButtonSize = FVector2D(144.0f, 144.0f);

	// Internal list of buttons (Keys = Index 0-14)
	UPROPERTY()
	TArray<TObjectPtr<URTSCommandButtonWidget>> GridButtons;

	// Populate the grid based on data
	void RefreshGrid(const TArray<URTSCommandButton*>& Buttons);

	// Generate the 15 empty slots on Init
	void InitGridSlots();

    // 统一网格填充逻辑（支持 PreferredIndex 与自动空位）
    void PopulateSparseButtons(URTSCommandGridAsset* Grid, TArray<URTSCommandButton*>& OutButtons);

	UFUNCTION()
	void OnGridButtonClicked(const FGameplayTag& CommandTag);
	
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
	void ExecuteCommandPanelSlot(int32 SlotIndex);
	void HandleCommandPanelHotkeyPressed(int32 SlotIndex, const FKey& Hotkey);
	void HandleCommandPanelHotkeyRepeated(int32 SlotIndex, const FKey& Hotkey);
	void HandleCommandPanelHotkeyReleased(const FKey& Hotkey);
	void ClearHeldCommandHotkeyState();
	void ConfirmPendingTargetWithHotkey(URTSSelector* Selector, const FKey& Hotkey, bool bRapidFire = false);
	void UpdateCommandStateVisuals();
	void PositionSharedTooltip();

    // Cache the active actor for context
    TWeakObjectPtr<AActor> ActiveActorPtr;

	UPROPERTY(Transient)
	TObjectPtr<class UInputComponent> CommandPanelInputComponent;

	TWeakObjectPtr<APlayerController> CommandPanelInputOwner;
	FKey HeldCommandHotkey;
	int32 HeldCommandSlotIndex = INDEX_NONE;

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

public:
    // Called by child buttons
    void NotifyButtonHovered(URTSCommandButtonWidget* Btn, URTSCommandButton* Data);
    void NotifyButtonUnhovered(URTSCommandButtonWidget* Btn);

	/** The left activity panel uses the same visual button class, not this grid's widget instances. */
	TSubclassOf<URTSCommandButtonWidget> GetCommandButtonWidgetClass() const
	{
		return ButtonParams;
	}

};
