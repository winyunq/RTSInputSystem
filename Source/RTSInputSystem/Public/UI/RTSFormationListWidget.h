// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSSelectionStructs.h"
#include "RTSFormationListWidget.generated.h"

class UPanelWidget;
class UHorizontalBox;
class URTSControlGroupButton;

/** Persistent 0-9 control-group strip for the bottom UnitDetailPanel header. */
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

	/** Fixed card width; surplus strip width is distributed between cards. */
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

	void ApplyFormationSettings();
	void BuildSlotPool();
	void RefreshControlGroups(const FRTSControlGroupsView& View);
};
