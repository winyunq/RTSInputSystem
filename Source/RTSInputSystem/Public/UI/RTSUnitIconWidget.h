// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSSelectionStructs.h"
#include "RTSUnitIconWidget.generated.h"

class UImage;
class UTextBlock;
class URTSTooltipWidget;

/**
 * Represents a single unit icon or group summary icon in the selection panel.
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSUnitIconWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Updates the widget with data.
	 * @param bShowIcon  If true, forces icon visibility (if valid). If false, hides icon.
	 * @param bShowBars  If true, shows status bars.
	 * @param bShowCount If true, shows the embedded count label when Data.Count > 1.
	 * @param DesiredIconSize If greater than zero, forces a square desired icon size in pixels.
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void InitData(const FRTSUnitData& Data, bool bShowIcon = true, bool bShowBars = true, bool bShowCount = true, int32 DesiredIconSize = 0);

	/**
	 * Sets the visual active state (e.g. for Tab toggling).
	 * Active: Default appearance.
	 * Inactive: Dimmed opacity.
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void SetIsActive(bool bActive);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	// Input Handling
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// UI Bindings
	UPROPERTY(meta = (BindWidgetOptional))
	class UImage* UnitIcon;

	UPROPERTY(meta = (BindWidgetOptional))
	class UImage* ActiveFrame;

	UPROPERTY(meta = (BindWidgetOptional))
	class UWidget* UnitSlotFrame;

	UPROPERTY(meta = (BindWidgetOptional))
	class UTextBlock* UnitNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	class UTextBlock* CountText;

	// -- Status Bars (Optional) --

	// -- Status Bars (Optional) --
	// If the unit has valid MaxHealth/Energy/Shield, these bars will update. 
	// Otherwise they will be hidden.

	UPROPERTY(meta = (BindWidgetOptional))
	class UProgressBar* HealthBar;

	UPROPERTY(meta = (BindWidgetOptional))
	class UProgressBar* EnergyBar;

	UPROPERTY(meta = (BindWidgetOptional))
	class UProgressBar* ShieldBar;

	/** Optional dedicated production progress widgets. Legacy UMG assets fall back to ShieldBar/UnitNameText. */
	UPROPERTY(meta = (BindWidgetOptional))
	class UProgressBar* ActivityBar;

	UPROPERTY(meta = (BindWidgetOptional))
	class UTextBlock* ActivityText;

	/** Visible × affordance on cancellable production/research cards. */
	UPROPERTY(meta = (BindWidgetOptional))
	class UTextBlock* CancelHintText;

	UPROPERTY(EditAnywhere, Category = "RTS Selection|Tooltip")
	TSubclassOf<URTSTooltipWidget> TooltipClass;

	UPROPERTY(Transient)
	TObjectPtr<URTSTooltipWidget> UnitTooltipWidget;

private:
	// Internal Copy of Data for Interaction
	FRTSUnitData StoredData;

	void UpdateBar(class UProgressBar* Bar, float Current, float Max);
	void UpdateTooltip(const FRTSUnitData& Data);
	TSubclassOf<URTSTooltipWidget> ResolveTooltipClass() const;

	UFUNCTION()
	UWidget* GetOrCreateTooltipWidget();
};
