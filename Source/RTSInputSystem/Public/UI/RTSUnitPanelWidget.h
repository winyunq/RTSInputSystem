// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSSelectionStructs.h"
#include "RTSUnitPanelWidget.generated.h"

class UPanelWidget;
class UBorder;
class UTextBlock;
class UWidget;
class URTSCommandButtonWidget;
class URTSUnitIconWidget;
class UProgressBar;
class USizeBox;
class SBox;
class SWidget;

/**
 * Main Selection Panel. Handles Empty, Single, List, and Summary routes.
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSUnitPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	UFUNCTION()
	void OnSelectionUpdated(const FRTSSelectionView& View);

	UFUNCTION()
	void OnControlGroupsUpdated(const FRTSControlGroupsView& View);

	void OnCommandProgressChanged(AActor* ProgressProvider);

	/**
	* Class of the item widget to spawn in the list.
	* Must be set in Blueprint (WBP_RTSUnitIcon).
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	TSubclassOf<URTSUnitIconWidget> UnitIconClass;

	// The class to use for each unit icon. 
	// If set in Editor, we use this. If nullptr, we try to detect from the first child in Designer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	TSubclassOf<UUserWidget> IconWidgetClass;

	// Optional: The class for the "Count" widget in Summary mode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	TSubclassOf<UUserWidget> CountWidgetClass;

	/** Same button Blueprint used by the right-side command card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection|Activity")
	TSubclassOf<URTSCommandButtonWidget> CommandButtonWidgetClass;

	// -- Bind Widgets --
	
	/**
	 * Max items to show in the grid. 
	 * Calculated automatically as MaxRows * MaxColumns.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS Selection")
	int32 ItemsPerPage = 24;

	/** Icon artwork size only. The full cell uses the shared command button footprint and padding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	int32 IconSlotSize = 128;

	/** Fixed header reserve for formation/control-group information inside UnitPanel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	float PanelHeaderHeight = 44.0f;

	/** Fixed column count, at least 8. Template children never determine capacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	int32 MaxColumns = 8;

	/** Fixed row count, at least 3. Template children never determine capacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Selection")
	int32 MaxRows = 3;

	// Optional UnitPanel visual shell. The C++ widget owns fixed outer bounds.
	UPROPERTY(meta = (BindWidgetOptional))
	UBorder* UnitPanelFrame;

	// Three overlaid routes share one fixed content area.
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* SingleUnitPanel;

	// Research and weapons/armor share the right half of the single-unit body.
	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* UnitRosterPane;

	UPROPERTY(meta = (BindWidgetOptional))
	UWidget* WeaponArmorPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	USizeBox* UnitPanelRouteBounds;

	UPROPERTY(meta = (BindWidgetOptional))
	USizeBox* UnitPanelHeaderBounds;

	UPROPERTY(meta = (BindWidgetOptional))
	UPanelWidget* SummaryIconContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	UPanelWidget* ActivityQueueContainer;

	// Individual-unit list grid; summary and production use their own containers.
	// Child 0 can be a unit icon template; runtime builds the fixed grid pool from it.
	UPROPERTY(meta = (BindWidget))
	UPanelWidget* IconContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	UProgressBar* ActiveProgress0;

	UPROPERTY(meta = (BindWidgetOptional))
	UProgressBar* ActiveProgress1;

private:
	void ApplySelectionPanelLayoutSettings();
	void ApplyFixedPanelSlotLayout();
	FVector2D CalculateFixedPanelSize() const;
	FVector2D GetSelectionCellSize() const;
	void ApplyFixedPanelBounds();
	void BuildSelectionGrid(UPanelWidget* Container, TArray<URTSUnitIconWidget*>& Slots, bool bSummary);
	void SetContentRoute(ERTSSelectionMode Mode);

	// Pool of re-usable icon widgets
	UPROPERTY()
	TArray<URTSUnitIconWidget*> IconSlots;

	UPROPERTY()
	TArray<URTSUnitIconWidget*> SummaryIconSlots;

	// References to actual moved or copied command buttons currently shown here.
	UPROPERTY()
	TArray<URTSCommandButtonWidget*> ProgressButtonSlots;

	// Empty fixed queue cells are frames, never stand-ins for live research buttons.
	UPROPERTY()
	TArray<URTSCommandButtonWidget*> EmptyQueueFrames;


	// Pool of re-usable count widgets (for Summary mode)
	UPROPERTY()
	TArray<UTextBlock*> CountSlots;

	TSharedPtr<SBox> FixedPanelBoundsBox;
	FVector2D SelectionButtonSize = FVector2D(144.0f, 144.0f);
	FMargin SelectionSlotPadding = FMargin(4.0f);
	bool bHasAssignedControlGroups = false;
	TWeakObjectPtr<AActor> DisplayedProgressProvider;
	FRTSUnitData DisplayedSingleUnitData;
	FDelegateHandle CommandProgressChangedHandle;

	void RefreshGrid(const FRTSSelectionView& View);
	void ShowEmptyContent();
	void ShowSingleContent(const FRTSUnitData& Data);
	void ShowGridContent(const FRTSSelectionView& View);
	void ShowCommandProgressItems(const FRTSUnitData& OwnerData);
	void RefreshSingleUnitDetail(const FRTSUnitData& Data);
	void RefreshSingleUnitActivity(const FRTSUnitData& Data);
	void HideGridSlots();
	UWidget* FindDescendantWidgetByName(UWidget* RootWidget, FName WidgetName) const;
};
