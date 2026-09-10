// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/RTSCommandButton.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "RTSSelectionStructs.h"
#include "RTSCommandButtonWidget.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCommandButtonClicked, const FGameplayTag&, CommandTag);

class URTSCommanderGridWidget;

/**
 * A Single Button in the Command Grid.
 * Displays Icon, handles clicks, shows tooltip.
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCommandButtonWidget : public UUserWidget
{
	GENERATED_BODY()
	friend class URTSCommanderGridWidget;

public:
	
	virtual void NativeConstruct() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	UFUNCTION(BlueprintCallable, Category = "RTS Command")
	void Init(URTSCommandButton* InData, AActor* InContext = nullptr, FKey InOverrideHotkey = FKey());

	/** Shows the same command button inside a production/research activity slot. */
	void InitProgressItem(const FRTSTimedCommandInstance& ProgressItem, AActor* InContext, float IconSize = 144.0f);

	/** Returns the underlying data asset for this button. */
    UFUNCTION(BlueprintCallable, Category = "RTS Command")
    URTSCommandButton* GetData() const { return ButtonData; }

	FName GetProgressItemId() const { return ProgressItemId; }
	bool IsProgressItemMode() const { return bProgressItemMode; }

	UFUNCTION(BlueprintCallable, Category = "RTS Command")
	void SetIsDisabled(bool bDisabled);

	/** Applies the persistent highlight for the command currently owning the selection. */
	void SetCommandActive(bool bActive);

	/** Mirrors the physical pressed/released state of the keyboard shortcut. */
	void SetKeyboardPressed(bool bPressed);

	/** Explicit state pull used only by the owning command card. */
	void RefreshCommandState();

	// Event for click
	UPROPERTY(BlueprintAssignable, Category = "RTS Command")
	FOnCommandButtonClicked OnCommandClicked;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> MainButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IconImage;

    // Cooldown Overlay (Image with Dynamic Material)
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UImage> CooldownImage;

    // Hotkey Display
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<class UTextBlock> HotkeyText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> DisplayNameText;

    // Auto-Cast Border (Image or Border)
    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> AutoCastBorder;

	/** Optional numeric badge used by queued commands such as unit production. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> QueueCountText;

	// The data asset backing this button
	UPROPERTY()
	TObjectPtr<URTSCommandButton> ButtonData;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CooldownMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UObject> ProgressActionTarget;

	FName ProgressItemId = NAME_None;
	bool bProgressItemMode = false;
	bool bCanCancelProgressItem = false;
	bool bResearchCopy = false;
	bool bReturnOnCancel = false;
	FKey CommandHotkey;
	FEntityHandle CommandOwnerEntity;
	FEntityHandle ProgressOwnerEntity;
	TWeakObjectPtr<AActor> ProgressOwnerActor;

    // State tracking for efficient updates
	bool bIsCooldownActive = false;
	bool bCommandActive = false;
	bool bKeyboardPressed = false;
	bool bHasDefaultBackgroundColor = false;
	bool bHasDefaultButtonStyle = false;
	FLinearColor DefaultBackgroundColor = FLinearColor::White;
	FButtonStyle DefaultButtonStyle;

	UPROPERTY(EditAnywhere, Category = "RTS Command|Feedback")
	FLinearColor ActiveCommandTint = FLinearColor(0.35f, 1.0f, 0.55f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "RTS Command|Feedback")
	FLinearColor KeyboardPressedTint = FLinearColor(1.0f, 0.78f, 0.18f, 1.0f);

    // The context actor (to query state)
    UPROPERTY()
    TWeakObjectPtr<AActor> ContextActor;

    // The class to use for tooltips - MOVED TO GRID
    // UPROPERTY(EditAnywhere, Category = "UI")
    // TSubclassOf<class URTSTooltipWidget> TooltipWidgetClass;

	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleProgressClicked();
    
    UFUNCTION()
    void HandleHovered();

	UFUNCTION()
	void HandleUnhovered();

	void ApplyInteractionVisualState();
};
