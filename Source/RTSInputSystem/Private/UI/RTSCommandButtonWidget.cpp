// Copyright 2024 Winy unq All Rights Reserved.

#include "UI/RTSCommandButtonWidget.h"
#include "UI/RTSTooltipWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Interfaces/RTSCommandInterface.h"
#include "Interfaces/RTSCommandProgressController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/RTSCommanderGridWidget.h"

namespace
{
	URTSCommanderGridWidget* FindTooltipGrid(URTSCommandButtonWidget* Button)
	{
		if (auto* Grid = Button->GetTypedOuter<URTSCommanderGridWidget>()) return Grid;
		TArray<UUserWidget*> Grids;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Button, Grids, URTSCommanderGridWidget::StaticClass(), false);
		for (UUserWidget* Widget : Grids)
			if (Widget->GetOwningPlayer() == Button->GetOwningPlayer())
				return Cast<URTSCommanderGridWidget>(Widget);
		return nullptr;
	}
}

TSharedRef<SWidget> URTSCommandButtonWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* RootSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("DefaultCommandButtonSize"));
		RootSize->SetWidthOverride(144.0f);
		RootSize->SetHeightOverride(144.0f);

		MainButton = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(), TEXT("MainButton"));
		MainButton->SetBackgroundColor(FLinearColor(0.035f, 0.08f, 0.12f, 0.96f));
		RootSize->AddChild(MainButton);

		UOverlay* Face = WidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(), TEXT("CommandButtonFace"));
		MainButton->AddChild(Face);

		IconImage = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), TEXT("IconImage"));
		if (UOverlaySlot* IconSlot = Face->AddChildToOverlay(IconImage))
		{
			IconSlot->SetPadding(FMargin(10.0f, 10.0f, 10.0f, 32.0f));
			IconSlot->SetHorizontalAlignment(HAlign_Fill);
			IconSlot->SetVerticalAlignment(VAlign_Fill);
		}

		CooldownImage = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), TEXT("CooldownImage"));
		CooldownImage->SetColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.65f));
		CooldownImage->SetVisibility(ESlateVisibility::Collapsed);
		Face->AddChildToOverlay(CooldownImage);

		DisplayNameText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("DisplayNameText"));
		DisplayNameText->SetJustification(ETextJustify::Center);
		DisplayNameText->SetAutoWrapText(true);
		DisplayNameText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		if (UOverlaySlot* LabelSlot = Face->AddChildToOverlay(DisplayNameText))
		{
			LabelSlot->SetPadding(FMargin(6.0f, 78.0f, 6.0f, 20.0f));
			LabelSlot->SetHorizontalAlignment(HAlign_Fill);
			LabelSlot->SetVerticalAlignment(VAlign_Fill);
		}

		HotkeyText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("HotkeyText"));
		HotkeyText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.78f, 0.2f, 1.0f)));
		if (UOverlaySlot* HotkeySlot = Face->AddChildToOverlay(HotkeyText))
		{
			HotkeySlot->SetPadding(FMargin(6.0f));
			HotkeySlot->SetHorizontalAlignment(HAlign_Right);
			HotkeySlot->SetVerticalAlignment(VAlign_Bottom);
		}

		WidgetTree->RootWidget = RootSize;
	}

	return Super::RebuildWidget();
}

void URTSCommandButtonWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (MainButton)
	{
		MainButton->OnClicked.RemoveDynamic(this, &URTSCommandButtonWidget::HandleClicked);
		MainButton->OnClicked.RemoveDynamic(this, &URTSCommandButtonWidget::HandleProgressClicked);
		if (bProgressItemMode) MainButton->OnClicked.AddUniqueDynamic(this, &URTSCommandButtonWidget::HandleProgressClicked);
		else MainButton->OnClicked.AddUniqueDynamic(this, &URTSCommandButtonWidget::HandleClicked);
		DefaultBackgroundColor = MainButton->GetBackgroundColor();
		bHasDefaultBackgroundColor = true;
		DefaultButtonStyle = MainButton->GetStyle();
		bHasDefaultButtonStyle = true;
		ApplyInteractionVisualState();
	}
}

void URTSCommandButtonWidget::Init(URTSCommandButton* InData, AActor* InContext, FKey InOverrideHotkey)
{
	if (USizeBox* RootSize = WidgetTree ? Cast<USizeBox>(WidgetTree->RootWidget) : nullptr)
	{
		RootSize->SetClipping(EWidgetClipping::ClipToBounds);
		if (UScaleBox* FaceScale = Cast<UScaleBox>(RootSize->GetContent()); FaceScale && MainButton)
		{
			// The panel already supplies the footprint. Scale the authored face to that
			// size, never to the label's desired size or its previous-frame wrap width.
			const FVector2D FaceSize = MainButton->GetStyle().Normal.GetImageSize();
			FaceScale->SetStretch(EStretch::UserSpecified);
			FaceScale->SetUserSpecifiedScale(FMath::Min(
				RootSize->GetWidthOverride() / FMath::Max(1.0, FaceSize.X),
				RootSize->GetHeightOverride() / FMath::Max(1.0, FaceSize.Y)));
		}
	}
	bProgressItemMode = false;
	bReturnOnCancel = false;
	CommandHotkey = InOverrideHotkey.IsValid() ? InOverrideHotkey : InData ? InData->Hotkey : FKey();
	if (MainButton)
	{
		MainButton->OnClicked.RemoveDynamic(this, &URTSCommandButtonWidget::HandleProgressClicked);
		MainButton->OnClicked.AddUniqueDynamic(this, &URTSCommandButtonWidget::HandleClicked);
	}
	bCanCancelProgressItem = false;
	ProgressItemId = NAME_None;
	ProgressActionTarget = nullptr;
	SetRenderOpacity(1.0f);
    ButtonData = InData;
    ContextActor = InContext;

    if (ButtonData)
    {
        UE_LOG(LogTemp, Log, TEXT("Button Init: %s (Tag: %s)"), *ButtonData->DisplayName.ToString(), *ButtonData->CommandTag.ToString());

        // Icons own the button face when present. Text is only a temporary fallback for
        // commands that do not have artwork yet; it must not cover finished icon buttons.
        const bool bHasIcon = IsValid(ButtonData->Icon);
        if (IconImage)
        {
            IconImage->SetBrushFromTexture(ButtonData->Icon);
            IconImage->SetVisibility(bHasIcon
                ? ESlateVisibility::HitTestInvisible
                : ESlateVisibility::Collapsed);
        }

        if (DisplayNameText)
        {
            DisplayNameText->SetText(ButtonData->DisplayName);
            DisplayNameText->SetVisibility(!bHasIcon && !ButtonData->DisplayName.IsEmpty()
                ? ESlateVisibility::HitTestInvisible
                : ESlateVisibility::Collapsed);
        }

        // Set Hotkey Display
        if (HotkeyText)
        {
			const FKey TargetKey = InOverrideHotkey.IsValid() ? InOverrideHotkey : ButtonData->Hotkey;
            
            // Check if key is valid
            if (!TargetKey.IsValid())
            {
                HotkeyText->SetVisibility(ESlateVisibility::Collapsed);
            }
            else
            {
                HotkeyText->SetText(TargetKey.GetDisplayName());
                HotkeyText->SetVisibility(ESlateVisibility::HitTestInvisible);
            }
        }

        // Reset State
        bIsCooldownActive = false;
		bCommandActive = false;
		bKeyboardPressed = false;
        if (CooldownImage)
        {
            CooldownImage->SetVisibility(ESlateVisibility::Hidden);
            if (!CooldownMaterial)
            {
                CooldownMaterial = CooldownImage->GetDynamicMaterial();
            }
        }

        if (AutoCastBorder)
        {
            AutoCastBorder->SetVisibility(ESlateVisibility::Hidden);
        }
		if (QueueCountText)
		{
			QueueCountText->SetVisibility(ESlateVisibility::Collapsed);
		}

        SetIsDisabled(false);
        SetVisibility(ESlateVisibility::Visible);
		ApplyInteractionVisualState();
        
        // Remove Standard Tooltip to allow shared logic
        if (MainButton)
        {
            MainButton->SetToolTip(nullptr);
            if (!MainButton->OnHovered.IsAlreadyBound(this, &URTSCommandButtonWidget::HandleHovered))
                MainButton->OnHovered.AddDynamic(this, &URTSCommandButtonWidget::HandleHovered);
            if (!MainButton->OnUnhovered.IsAlreadyBound(this, &URTSCommandButtonWidget::HandleUnhovered))
                MainButton->OnUnhovered.AddDynamic(this, &URTSCommandButtonWidget::HandleUnhovered);
        }
    }
    else
    {
        // Null data means empty slot
        if (MainButton) MainButton->SetToolTip(nullptr);
        if (IconImage) { IconImage->SetBrushFromTexture(nullptr); IconImage->SetVisibility(ESlateVisibility::Collapsed); }
        if (DisplayNameText) DisplayNameText->SetVisibility(ESlateVisibility::Collapsed);
        if (HotkeyText) HotkeyText->SetVisibility(ESlateVisibility::Collapsed);
        if (QueueCountText) QueueCountText->SetVisibility(ESlateVisibility::Collapsed);
        if (CooldownImage) CooldownImage->SetVisibility(ESlateVisibility::Hidden);
        if (AutoCastBorder) AutoCastBorder->SetVisibility(ESlateVisibility::Hidden);
		bCommandActive = false;
		bKeyboardPressed = false;
		ApplyInteractionVisualState();
        SetVisibility(ESlateVisibility::Hidden);
    }
}

void URTSCommandButtonWidget::InitProgressItem(
	const FRTSTimedCommandInstance& ProgressItem,
	AActor* InContext, float IconSize)
{

	if (WidgetTree)
		if (USizeBox* IconBox = Cast<USizeBox>(WidgetTree->RootWidget))
		{
			IconBox->SetWidthOverride(IconSize);
			IconBox->SetHeightOverride(IconSize);
		}
	if (ButtonData != ProgressItem.CommandButton)
		Init(ProgressItem.CommandButton, InContext, CommandHotkey);

	// The widget is the exact command-card button moved into the activity area.
	// None of the command-card-only interaction state may travel with it: otherwise
	// Hovered/Pressed materials become its persistent Normal face and queued items
	// look like several simultaneous active researches.
	bCommandActive = false;
	bKeyboardPressed = false;
	bIsCooldownActive = false;
	SetRenderOpacity(1.0f);
	if (CooldownImage)
	{
		CooldownImage->SetVisibility(ESlateVisibility::Hidden);
	}
	if (AutoCastBorder)
	{
		AutoCastBorder->SetVisibility(ESlateVisibility::Hidden);
	}
	ApplyInteractionVisualState();

	bProgressItemMode = true;
	if (MainButton)
	{
		MainButton->OnClicked.RemoveDynamic(this, &URTSCommandButtonWidget::HandleClicked);
		MainButton->OnClicked.AddUniqueDynamic(this, &URTSCommandButtonWidget::HandleProgressClicked);
	}
	bCanCancelProgressItem = ProgressItem.CommandButton && ProgressItem.bCanCancel;
	ProgressItemId = ProgressItem.InstanceId.IsValid()
		? FName(*ProgressItem.InstanceId.ToString(EGuidFormats::Digits))
		: NAME_None;
	ProgressActionTarget = ProgressItem.Controller
		? ProgressItem.Controller
		: InContext;

	if (QueueCountText)
	{
		if (ProgressItem.CommandButton && ProgressItem.State == ERTSTimedCommandState::Queued)
		{
			QueueCountText->SetText(FText::AsNumber(ProgressItem.QueueIndex));
			QueueCountText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			QueueCountText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	SetIsDisabled(ProgressItem.CommandButton == nullptr);
	SetVisibility(ESlateVisibility::Visible);
}

void URTSCommandButtonWidget::HandleHovered()
{
	if (ButtonData)
		if (URTSCommanderGridWidget* Grid = FindTooltipGrid(this))
			Grid->NotifyButtonHovered(this, ButtonData);
}

void URTSCommandButtonWidget::HandleUnhovered()
{
	if (URTSCommanderGridWidget* Grid = FindTooltipGrid(this))
		Grid->NotifyButtonUnhovered(this);
}

void URTSCommandButtonWidget::RefreshCommandState()
{
if (bProgressItemMode)
{
	return;
}

// Update Availability, Cooldown & AutoCast State from Context
if (ButtonData && ContextActor.IsValid() && ContextActor->Implements<URTSCommandInterface>())
{
    // 0. Availability & Visibility logic (Scheme A)
    bool bAvailable = IRTSCommandInterface::Execute_IsCommandAvailable(ContextActor.Get(), ButtonData->CommandTag);
    
    if (!bAvailable)
    {
        if (ButtonData->bHideIfUnavailable)
        {
            SetVisibility(ESlateVisibility::Hidden);
        }
        else
        {
            SetVisibility(ESlateVisibility::Visible);
            SetIsDisabled(true);
        }
    }
    else
    {
        SetVisibility(ESlateVisibility::Visible);
        SetIsDisabled(false);
    }

    // 1. Cooldown Logic
    float Remaining = IRTSCommandInterface::Execute_GetCooldownRemaining(ContextActor.Get(), ButtonData->CommandTag);
    bool bCurrentlyCooling = Remaining > 0.0f;

    // Edge Detection: Cooldown Started (or Widget just initialized on active CD)
    if (bCurrentlyCooling && !bIsCooldownActive)
    {
        if (CooldownMaterial && ButtonData->DefaultCooldown > 0.1f)
        {
            // Optional: Send Total Duration if needed for other effects (like shimmer speed)
             CooldownMaterial->SetScalarParameterValue(FName("CD_TotalDuration"), ButtonData->DefaultCooldown);
        }

        if (CooldownImage) CooldownImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    // Edge Detection: Cooldown Ended
    else if (!bCurrentlyCooling && bIsCooldownActive)
    {
        if (CooldownImage) CooldownImage->SetVisibility(ESlateVisibility::Hidden);
    }

    if (bCurrentlyCooling && CooldownMaterial)
    {
        // Calculate Phase (0.0 to 1.0)
        float Total = FMath::Max(ButtonData->DefaultCooldown, 0.001f);
        float Phase = FMath::Clamp(Remaining / Total, 0.0f, 1.0f);
        
        // Protocol Change: Send normalized "CD_Phase"
        CooldownMaterial->SetScalarParameterValue(FName("CD_Phase"), Phase);
        CooldownMaterial->SetScalarParameterValue(FName("CD_EndTime"), Phase);
    }

    bIsCooldownActive = bCurrentlyCooling;


        // 2. Auto-Cast
        if (ButtonData->bAllowAutoCast && AutoCastBorder)
        {
             bool bEnabled = IRTSCommandInterface::Execute_IsAutoCastEnabled(ContextActor.Get(), ButtonData->CommandTag);
             // Flash or Show
             AutoCastBorder->SetVisibility(bEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
        }
    }
else if (ButtonData)
{
    const bool bAvailable = ButtonData->IsAvailableForContext(this, GetOwningPlayer());
    SetVisibility(!bAvailable && ButtonData->bHideIfUnavailable
        ? ESlateVisibility::Hidden : ESlateVisibility::Visible);
    SetIsDisabled(!bAvailable);
    if (ButtonData->bAllowAutoCast && AutoCastBorder)
    {
        const bool bEnabled = ButtonData->IsAutoCastEnabledForContext(this, GetOwningPlayer());
        AutoCastBorder->SetVisibility(bEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    }
}

if (ButtonData && QueueCountText)
{
	const int32 QueueCount = ButtonData->GetQueueCountForContext(this, ContextActor.Get());
	QueueCountText->SetText(FText::AsNumber(QueueCount));
	QueueCountText->SetVisibility(QueueCount > 0
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed);
}
}

FReply URTSCommandButtonWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    // Handle Right Click for Auto-Cast
    if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
    {
        if (!bProgressItemMode && ButtonData && ButtonData->bAllowAutoCast)
        {
            if (ContextActor.IsValid() && ContextActor->Implements<URTSCommandInterface>())
            {
                IRTSCommandInterface::Execute_ToggleAutoCast(ContextActor.Get(), ButtonData->CommandTag);
                return FReply::Handled();
            }

            if (ButtonData->HandleAlternateClick(this, nullptr))
            {
                return FReply::Handled();
            }
        }
    }

    return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void URTSCommandButtonWidget::SetIsDisabled(bool bDisabled)
{
	if (MainButton)
	{
		MainButton->SetIsEnabled(!bDisabled);
	}
}

void URTSCommandButtonWidget::SetCommandActive(bool bActive)
{
	if (bCommandActive == bActive)
	{
		return;
	}

	bCommandActive = bActive;
	ApplyInteractionVisualState();
}

void URTSCommandButtonWidget::SetKeyboardPressed(const bool bPressed)
{
	if (bKeyboardPressed == bPressed)
	{
		return;
	}

	bKeyboardPressed = bPressed;
	ApplyInteractionVisualState();
}

void URTSCommandButtonWidget::ApplyInteractionVisualState()
{
	if (!MainButton)
	{
		return;
	}

	if (!bHasDefaultBackgroundColor)
	{
		DefaultBackgroundColor = MainButton->GetBackgroundColor();
		bHasDefaultBackgroundColor = true;
	}
	if (!bHasDefaultButtonStyle)
	{
		DefaultButtonStyle = MainButton->GetStyle();
		bHasDefaultButtonStyle = true;
	}

	FButtonStyle VisualStyle = DefaultButtonStyle;
	if (bKeyboardPressed)
	{
		VisualStyle.SetNormal(DefaultButtonStyle.Pressed);
		VisualStyle.SetHovered(DefaultButtonStyle.Pressed);
	}
	else if (bCommandActive)
	{
		VisualStyle.SetNormal(DefaultButtonStyle.Hovered);
	}
	MainButton->SetStyle(VisualStyle);

	const FLinearColor Tint = bKeyboardPressed
		? KeyboardPressedTint
		: (bCommandActive ? ActiveCommandTint : FLinearColor::White);
	MainButton->SetBackgroundColor(DefaultBackgroundColor * Tint);
}

void URTSCommandButtonWidget::HandleClicked()
{
	if (ButtonData)
	{
        UE_LOG(LogTemp, Verbose, TEXT("RTSCommandButtonWidget: Clicked %s"), *ButtonData->CommandTag.ToString());
		OnCommandClicked.Broadcast(ButtonData->CommandTag);
	}
}

void URTSCommandButtonWidget::HandleProgressClicked()
{
	if (bCanCancelProgressItem && ProgressActionTarget
		&& ProgressActionTarget->Implements<URTSCommandProgressController>())
	{
		// On provider acknowledgement the original moves home, or the copy is deleted.
		bReturnOnCancel = true;
		IRTSCommandProgressController::Execute_RequestCancelCommandProgressItem(ProgressActionTarget, ProgressItemId);
	}
}
