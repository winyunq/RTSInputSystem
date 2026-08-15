// Copyright 2024 Winy unq All Rights Reserved.

#include "UI/RTSCommandButtonWidget.h"
#include "UI/RTSTooltipWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Interfaces/RTSCommandInterface.h"
#include "Interfaces/RTSCommandProgressController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/RTSCommanderGridWidget.h"
#include "Engine/World.h"
#include "Subsystems/MassBattleSubsystem.h"
#include "TimerManager.h"

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

		ActivityProgressBar = WidgetTree->ConstructWidget<UProgressBar>(
			UProgressBar::StaticClass(), TEXT("ActivityProgressBar"));
		ActivityProgressBar->SetFillColorAndOpacity(FLinearColor(0.2f, 0.8f, 1.0f, 1.0f));
		ActivityProgressBar->SetVisibility(ESlateVisibility::Collapsed);
		if (UOverlaySlot* ProgressSlot = Face->AddChildToOverlay(ActivityProgressBar))
		{
			ProgressSlot->SetPadding(FMargin(4.0f));
			ProgressSlot->SetHorizontalAlignment(HAlign_Fill);
			ProgressSlot->SetVerticalAlignment(VAlign_Bottom);
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
		MainButton->OnClicked.AddUniqueDynamic(this, &URTSCommandButtonWidget::HandleClicked);
		DefaultBackgroundColor = MainButton->GetBackgroundColor();
		bHasDefaultBackgroundColor = true;
		DefaultButtonStyle = MainButton->GetStyle();
		bHasDefaultButtonStyle = true;
		ApplyInteractionVisualState();
	}
}

void URTSCommandButtonWidget::NativeTick(
	const FGeometry& MyGeometry,
	const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bProgressItemMode
		|| ProgressState != ERTSTimedCommandState::Active
		|| !ActivityProgressBar
		|| ProgressDurationSeconds <= 0.0f)
	{
		return;
	}

	if (ProgressSimulationStartTick != INDEX_NONE
		&& ProgressSimulationEndTick > ProgressSimulationStartTick)
	{
		const UMassBattleSubsystem* MassBattle =
			UMassBattleSubsystem::GetPtr(this);
		const int32 CurrentTick = MassBattle
			? MassBattle->GetTickCount() : ProgressSimulationStartTick;
		const float Progress = static_cast<float>(
			CurrentTick - ProgressSimulationStartTick)
			/ static_cast<float>(
				ProgressSimulationEndTick - ProgressSimulationStartTick);
		ActivityProgressBar->SetPercent(FMath::Clamp(Progress, 0.0f, 0.999f));
		return;
	}

	const UWorld* World = GetWorld();
	const float PresentationElapsed = ProgressSnapshotElapsedSeconds
		+ (World
			? FMath::Max(0.0f, World->GetTimeSeconds() - ProgressSnapshotWorldSeconds)
			: 0.0f);
	ActivityProgressBar->SetPercent(FMath::Min(
		PresentationElapsed / ProgressDurationSeconds,
		0.999f));
}

void URTSCommandButtonWidget::Init(URTSCommandButton* InData, AActor* InContext, FKey InOverrideHotkey)
{
	bProgressItemMode = false;
	bCanCancelProgressItem = false;
	ProgressItemId = NAME_None;
	ProgressActionTarget = nullptr;
	ProgressQueueIndex = 0;
	ProgressState = ERTSTimedCommandState::Active;
	ProgressSnapshotElapsedSeconds = 0.0f;
	ProgressDurationSeconds = 0.0f;
	ProgressSnapshotWorldSeconds = 0.0f;
	ProgressSimulationStartTick = INDEX_NONE;
	ProgressSimulationEndTick = INDEX_NONE;
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
		KeyboardPressFeedbackRemaining = 0.0f;
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
		if (ActivityProgressBar)
		{
			ActivityProgressBar->SetIsMarquee(false);
			ActivityProgressBar->SetPercent(0.0f);
			ActivityProgressBar->SetVisibility(ESlateVisibility::Collapsed);
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
		bCommandActive = false;
		KeyboardPressFeedbackRemaining = 0.0f;
		ApplyInteractionVisualState();
        SetVisibility(ESlateVisibility::Hidden);
    }
}

void URTSCommandButtonWidget::InitProgressItem(
	const FRTSTimedCommandInstance& ProgressItem,
	AActor* InContext)
{
	URTSCommandButton* Presentation = ProgressItem.CommandButton;
	if (!Presentation)
	{
		if (!TransientProgressButtonData)
		{
			TransientProgressButtonData =
				NewObject<URTSCommandButton>(this, TEXT("ProgressButtonPresentation"));
		}
		TransientProgressButtonData->CommandTag = ProgressItem.CommandTag;
		TransientProgressButtonData->DisplayName =
			!ProgressItem.DisplayName.IsEmpty()
				? ProgressItem.DisplayName
				: ProgressItem.PayloadId.IsNone()
				? FText::FromName(ProgressItem.CommandTag.GetTagName())
				: FText::FromName(ProgressItem.PayloadId);
		TransientProgressButtonData->Icon = ProgressItem.Icon;
		Presentation = TransientProgressButtonData;
	}

	const bool bPresentationChanged =
		ButtonData != Presentation;
	if (bPresentationChanged)
	{
		Init(Presentation, InContext, FKey());
	}

	// The widget is the exact command-card button moved into the activity area.
	// None of the command-card-only interaction state may travel with it: otherwise
	// Hovered/Pressed materials become its persistent Normal face and queued items
	// look like several simultaneous active researches.
	bCommandActive = false;
	KeyboardPressFeedbackRemaining = 0.0f;
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
	bCanCancelProgressItem = ProgressItem.bCanCancel;
	ProgressItemId = ProgressItem.InstanceId.IsValid()
		? FName(*ProgressItem.InstanceId.ToString(EGuidFormats::Digits))
		: NAME_None;
	ProgressActionTarget = ProgressItem.Controller
		? ProgressItem.Controller
		: InContext;
	ProgressQueueIndex = ProgressItem.QueueIndex;
	ProgressState = ProgressItem.State;
	ProgressSnapshotElapsedSeconds = FMath::Max(0.0f, ProgressItem.ElapsedSeconds);
	ProgressDurationSeconds = FMath::Max(0.0f, ProgressItem.DurationSeconds);
	ProgressSnapshotWorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	ProgressSimulationStartTick = ProgressItem.SimulationStartTick;
	ProgressSimulationEndTick = ProgressItem.SimulationEndTick;

	if (HotkeyText)
	{
		HotkeyText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ActivityProgressBar)
	{
		ActivityProgressBar->SetIsMarquee(false);
		ActivityProgressBar->SetPercent(ProgressItem.GetProgress01());
		ActivityProgressBar->SetVisibility(
			ProgressItem.State == ERTSTimedCommandState::Queued
				? ESlateVisibility::Collapsed
				: ESlateVisibility::HitTestInvisible);
	}
	if (QueueCountText)
	{
		if (ProgressItem.State == ERTSTimedCommandState::Queued)
		{
			QueueCountText->SetText(FText::AsNumber(ProgressItem.QueueIndex));
			QueueCountText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			QueueCountText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	SetIsDisabled(!bCanCancelProgressItem);
	SetVisibility(ESlateVisibility::Visible);
}

void URTSCommandButtonWidget::HandleHovered()
{
    // Notify Parent Grid
    if (GetOuter())
    {
        if (URTSCommanderGridWidget* Grid = GetTypedOuter<URTSCommanderGridWidget>())
        {
            Grid->NotifyButtonHovered(this, ButtonData);
        }
    }
}

void URTSCommandButtonWidget::HandleUnhovered()
{
    if (GetOuter())
    {
        if (URTSCommanderGridWidget* Grid = GetTypedOuter<URTSCommanderGridWidget>())
        {
            Grid->NotifyButtonUnhovered(this);
        }
    }
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
            SetVisibility(ESlateVisibility::Collapsed);
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
else if (ButtonData && ButtonData->bAllowAutoCast && AutoCastBorder)
{
    const bool bEnabled = ButtonData->IsAutoCastEnabledForContext(this, nullptr);
    AutoCastBorder->SetVisibility(bEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
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
        if (ButtonData && ButtonData->bAllowAutoCast)
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

void URTSCommandButtonWidget::PlayKeyboardPressFeedback()
{
	KeyboardPressFeedbackRemaining = FMath::Max(0.05f, KeyboardPressFeedbackDuration);
	ApplyInteractionVisualState();

	if (UWorld* World = GetWorld())
	{
		FTimerHandle FeedbackTimer;
		const TWeakObjectPtr<URTSCommandButtonWidget> WeakThis(this);
		World->GetTimerManager().SetTimer(
			FeedbackTimer,
			[WeakThis]()
			{
				if (URTSCommandButtonWidget* Button = WeakThis.Get())
				{
					Button->KeyboardPressFeedbackRemaining = 0.0f;
					Button->ApplyInteractionVisualState();
				}
			},
			KeyboardPressFeedbackRemaining,
			false);
	}
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
	if (KeyboardPressFeedbackRemaining > 0.0f)
	{
		VisualStyle.SetNormal(DefaultButtonStyle.Pressed);
		VisualStyle.SetHovered(DefaultButtonStyle.Pressed);
	}
	else if (bCommandActive)
	{
		VisualStyle.SetNormal(DefaultButtonStyle.Hovered);
	}
	MainButton->SetStyle(VisualStyle);

	const FLinearColor Tint = KeyboardPressFeedbackRemaining > 0.0f
		? KeyboardPressedTint
		: (bCommandActive ? ActiveCommandTint : FLinearColor::White);
	MainButton->SetBackgroundColor(DefaultBackgroundColor * Tint);
}

void URTSCommandButtonWidget::HandleClicked()
{
	if (bProgressItemMode)
	{
		if (bCanCancelProgressItem
			&& ProgressActionTarget
			&& ProgressActionTarget->Implements<URTSCommandProgressController>())
		{
			IRTSCommandProgressController::
				Execute_RequestCancelCommandProgressItem(
					ProgressActionTarget,
					ProgressItemId);
		}
		return;
	}

	if (ButtonData)
	{
        UE_LOG(LogTemp, Verbose, TEXT("RTSCommandButtonWidget: Clicked %s"), *ButtonData->CommandTag.ToString());
		OnCommandClicked.Broadcast(ButtonData->CommandTag);
	}
}
