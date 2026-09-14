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
#include "RTSSelectionSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

namespace
{
	URTSCommanderGridWidget* FindTooltipGrid(URTSCommandButtonWidget* Button)
	{
		UPanelWidget* Parent = Button->GetParent();
		return Parent ? Parent->GetTypedOuter<URTSCommanderGridWidget>() : nullptr;
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

		QueueCountText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("QueueCountText"));
		QueueCountText->SetVisibility(ESlateVisibility::Collapsed);
		if (UOverlaySlot* CountSlot = Face->AddChildToOverlay(QueueCountText))
		{
			CountSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 6.0f));
			CountSlot->SetHorizontalAlignment(HAlign_Right);
			CountSlot->SetVerticalAlignment(VAlign_Bottom);
		}

		WidgetTree->RootWidget = RootSize;
	}

	return Super::RebuildWidget();
}

void URTSCommandButtonWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SubscribeCommandState();

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

void URTSCommandButtonWidget::NativeDestruct()
{
	// Moving a live task out of the visible panel does not end its object binding.
	// AddUObject is weak; a retained task button still receives its actual resolution.
	if (!bProgressItemMode || bProgressResolved)
		if (ULocalPlayer* Player = GetOwningLocalPlayer())
			if (auto* Selection = Player->GetSubsystem<URTSSelectionSubsystem>())
				Selection->OnCommandProgressChanged.RemoveAll(this);
	Super::NativeDestruct();
}

void URTSCommandButtonWidget::BindCommandState(const TArray<FRTSUnitData>& Owners)
{
	BoundProgressSources.Reset();
	for (const FRTSUnitData& Owner : Owners)
		if (UObject* Provider = Owner.GetProgressProvider())
			BoundProgressSources.Emplace(Provider, Owner.ProgressSourceId);
	CommandStateDependencies.Reset();
	if (ButtonData)
	{
		TArray<UObject*> Dependencies;
		AActor* Executor = ContextActor.IsValid() ? ContextActor.Get() : GetOwningPlayer();
		ButtonData->GetCommandStateDependencies(this, Executor, Dependencies);
		UObject* CommandContext = ButtonData->CommandContext.Get();
		if (!CommandContext) CommandContext = Executor;
		if (CommandContext && CommandContext->Implements<URTSCommandInterface>())
		{
			if (Owners.IsEmpty())
			{
				TArray<UObject*> OwnerDependencies;
				IRTSCommandInterface::Execute_GetCommandStateDependencies(
					CommandContext, ButtonData->CommandTag, NAME_None, OwnerDependencies);
				Dependencies.Append(OwnerDependencies);
			}
			for (const FRTSUnitData& Owner : Owners)
			{
				TArray<UObject*> OwnerDependencies;
				IRTSCommandInterface::Execute_GetCommandStateDependencies(
					CommandContext, ButtonData->CommandTag, Owner.ProgressSourceId, OwnerDependencies);
				Dependencies.Append(OwnerDependencies);
			}
		}
		for (UObject* Dependency : Dependencies)
			if (Dependency) CommandStateDependencies.AddUnique(Dependency);
	}
	SubscribeCommandState();
	RefreshCommandState();
}

void URTSCommandButtonWidget::SubscribeCommandState()
{
	if (ULocalPlayer* Player = GetOwningLocalPlayer())
		if (auto* Selection = Player->GetSubsystem<URTSSelectionSubsystem>())
		{
			Selection->OnCommandProgressChanged.RemoveAll(this);
			if (ButtonData && (ButtonData->bIsResearch || !CommandStateDependencies.IsEmpty()))
				Selection->OnCommandProgressChanged.AddUObject(this, &URTSCommandButtonWidget::OnCommandProgressChanged);
		}
}

void URTSCommandButtonWidget::OnCommandProgressChanged(UObject* Provider, FName SourceId,
	FGuid ResolvedId, bool bCancelled)
{
	if (!ButtonData || !Provider) return;
	const bool bOwnSource = BoundProgressSources.ContainsByPredicate([Provider, SourceId](const auto& Source)
		{ return Source.Key.Get() == Provider && Source.Value == SourceId; });
	if (bProgressItemMode)
	{
		if (bOwnSource && ResolvedId.IsValid()
			&& ProgressItemId == FName(*ResolvedId.ToString(EGuidFormats::Digits)))
		{
			bProgressResolved = true;
			bReturnOnCancel = bCancelled;
		}
		return;
	}
	if (!bOwnSource && !CommandStateDependencies.Contains(Provider)) return;
	RefreshCommandState();
	if (IsHovered()) HandleHovered();
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
	CommandState = FRTSCommandState();
	bProgressItemMode = false;
	bReturnOnCancel = false;
	bProgressResolved = false;
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
            const FText Name = ButtonData->GetDisplayNameForContext(this, GetOwningPlayer());
            DisplayNameText->SetText(Name);
            DisplayNameText->SetVisibility(!bHasIcon && !Name.IsEmpty()
                ? ESlateVisibility::HitTestInvisible
                : ESlateVisibility::Collapsed);
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
            MainButton->OnHovered.RemoveDynamic(this, &URTSCommandButtonWidget::HandleHovered);
            MainButton->OnUnhovered.RemoveDynamic(this, &URTSCommandButtonWidget::HandleUnhovered);
        }
    }
    else
    {
        // Null data means empty slot
        if (MainButton) MainButton->SetToolTip(nullptr);
        if (IconImage) { IconImage->SetBrushFromTexture(nullptr); IconImage->SetVisibility(ESlateVisibility::Collapsed); }
        if (DisplayNameText) DisplayNameText->SetVisibility(ESlateVisibility::Collapsed);
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
	bProgressResolved = false;
	if (MainButton)
	{
		MainButton->OnClicked.RemoveDynamic(this, &URTSCommandButtonWidget::HandleClicked);
		MainButton->OnClicked.AddUniqueDynamic(this, &URTSCommandButtonWidget::HandleProgressClicked);
	}
	bCanCancelProgressItem = ProgressItem.CommandButton && ProgressItem.bCanCancel;
	CommandState.bHandled = true;
	CommandState.bAvailable = bCanCancelProgressItem;
	if (ProgressItem.InstanceId.IsValid())
	{
		// Keep the same button's prepared description and effects when it enters the queue.
		CommandState.Costs.Reset();
		CommandState.DurationSeconds = ProgressItem.DurationSeconds;
		CommandState.StatusDescription = FText::FromString(ProgressItem.State == ERTSTimedCommandState::Queued ? TEXT("已排队，等待研发")
			: ProgressItem.State == ERTSTimedCommandState::Paused ? TEXT("研发已暂停") : TEXT("正在研发"));
	}
	ProgressItemId = ProgressItem.InstanceId.IsValid()
		? FName(*ProgressItem.InstanceId.ToString(EGuidFormats::Digits))
		: NAME_None;
	ProgressActionTarget = ProgressItem.Controller
		? ProgressItem.Controller
		: InContext;

	if (QueueCountText)
	{
		QueueCountText->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetIsDisabled(ProgressItem.CommandButton == nullptr);
	SetVisibility(ESlateVisibility::Visible);
	if (URTSTooltipWidget* Tooltip = Cast<URTSTooltipWidget>(GetToolTip()))
		Tooltip->UpdateTooltip(ButtonData, ContextActor.Get(), this);
}

void URTSCommandButtonWidget::InitEmptyProgressSlot(int32 SlotNumber, float IconSize)
{
	TakeWidget();
	Init(nullptr);
	FRTSTimedCommandInstance Empty;
	Empty.bCanCancel = false;
	InitProgressItem(Empty, nullptr, IconSize);
	if (QueueCountText)
	{
		QueueCountText->SetText(FText::AsNumber(SlotNumber));
		QueueCountText->SetJustification(ETextJustify::Center);
		if (UOverlaySlot* NumberSlot = Cast<UOverlaySlot>(QueueCountText->Slot))
		{
			NumberSlot->SetPadding(FMargin(0.0f));
			NumberSlot->SetHorizontalAlignment(HAlign_Center);
			NumberSlot->SetVerticalAlignment(VAlign_Center);
		}
		QueueCountText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void URTSCommandButtonWidget::NativeOnMouseEnter(
    const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
    Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
    // Slate removes disabled children from the hit path, but keeps this enabled
    // wrapper. Show the existing shared tooltip without enabling the command.
    HandleHovered();
}

void URTSCommandButtonWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
    Super::NativeOnMouseLeave(InMouseEvent);
    HandleUnhovered();
}

void URTSCommandButtonWidget::HandleHovered()
{
	if (!ButtonData) return;
	if (URTSCommanderGridWidget* Grid = FindTooltipGrid(this))
	{
		SetToolTip(nullptr);
		Grid->NotifyButtonHovered(this, ButtonData);
	}
	else
	{
		URTSTooltipWidget* Tooltip = Cast<URTSTooltipWidget>(GetToolTip());
		if (!Tooltip)
		{
			const TSubclassOf<URTSTooltipWidget> TooltipClass = LoadClass<URTSTooltipWidget>(nullptr,
				TEXT("/Game/UI/HeadUpDisplay/ControlGird/ButtonMessage.ButtonMessage_C"));
			Tooltip = CreateWidget<URTSTooltipWidget>(this, TooltipClass);
			SetToolTip(Tooltip);
		}
		if (Tooltip) Tooltip->UpdateTooltip(ButtonData, ContextActor.Get(), this);
	}
}

void URTSCommandButtonWidget::HandleUnhovered()
{
	if (URTSCommanderGridWidget* Grid = FindTooltipGrid(this))
		Grid->NotifyButtonUnhovered(this);
}

FRTSCommandState URTSCommandButtonWidget::ResolveCommandState() const
{
	if (!ButtonData) return FRTSCommandState();
	const AActor* Executor = ContextActor.IsValid() ? ContextActor.Get() : GetOwningPlayer();
	if (BoundProgressSources.IsEmpty())
		return ButtonData->GetCommandStateForContext(this, Executor, NAME_None);
	FRTSCommandState SelectedState;
	bool bFoundVisibleSource = false;
	for (int32 Index = 0; Index < BoundProgressSources.Num(); ++Index)
	{
		const FRTSCommandState State = ButtonData->GetCommandStateForContext(
			this, Executor, BoundProgressSources[Index].Value);
		if (Index == 0) SelectedState = State;
		if (!State.bVisible) continue;
		if (State.bAvailable) return State;
		if (!bFoundVisibleSource)
		{
			SelectedState = State;
			bFoundVisibleSource = true;
		}
	}
	return SelectedState;
}

FText URTSCommandButtonWidget::GetTooltipDescription() const
{
	return CommandState.Description;
}

void URTSCommandButtonWidget::RefreshCommandState()
{
	if (bProgressItemMode || !ButtonData) return;

	if (DisplayNameText)
	{
		const FText Name = ButtonData->GetDisplayNameForContext(this, GetOwningPlayer());
		DisplayNameText->SetText(Name);
		DisplayNameText->SetVisibility(!IsValid(ButtonData->Icon) && !Name.IsEmpty()
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	CommandState = ResolveCommandState();
	const FRTSCommandState& State = CommandState;
	SetVisibility(!State.bVisible || (!State.bAvailable && ButtonData->bHideIfUnavailable)
		? ESlateVisibility::Hidden : ESlateVisibility::Visible);
	SetIsDisabled(!ButtonData->bIsResearch && !State.bAvailable);

	UObject* CommandContext = ButtonData->CommandContext.Get();
	const bool bHasCommandContext = CommandContext && CommandContext->Implements<URTSCommandInterface>();
	if (!bHasCommandContext && ContextActor.IsValid() && ContextActor->Implements<URTSCommandInterface>())
	{
		const float Remaining = IRTSCommandInterface::Execute_GetCooldownRemaining(ContextActor.Get(), ButtonData->CommandTag);
		const bool bCurrentlyCooling = Remaining > 0.0f;
		if (bCurrentlyCooling && !bIsCooldownActive)
		{
			if (CooldownMaterial && ButtonData->DefaultCooldown > 0.1f)
				CooldownMaterial->SetScalarParameterValue(FName("CD_TotalDuration"), ButtonData->DefaultCooldown);
			if (CooldownImage) CooldownImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else if (!bCurrentlyCooling && bIsCooldownActive)
		{
			if (CooldownImage) CooldownImage->SetVisibility(ESlateVisibility::Hidden);
		}
		if (bCurrentlyCooling && CooldownMaterial)
		{
			const float Total = FMath::Max(ButtonData->DefaultCooldown, 0.001f);
			const float Phase = FMath::Clamp(Remaining / Total, 0.0f, 1.0f);
			CooldownMaterial->SetScalarParameterValue(FName("CD_Phase"), Phase);
			CooldownMaterial->SetScalarParameterValue(FName("CD_EndTime"), Phase);
		}
		bIsCooldownActive = bCurrentlyCooling;

		if (ButtonData->bAllowAutoCast && AutoCastBorder)
		{
			const bool bEnabled = IRTSCommandInterface::Execute_IsAutoCastEnabled(ContextActor.Get(), ButtonData->CommandTag);
			AutoCastBorder->SetVisibility(bEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		}
	}
	else if (ButtonData->bAllowAutoCast && AutoCastBorder)
	{
		const bool bEnabled = ButtonData->IsAutoCastEnabledForContext(this, GetOwningPlayer());
		AutoCastBorder->SetVisibility(bEnabled ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	if (QueueCountText)
	{
		const int32 QueueCount = ButtonData->GetQueueCountForContext(this, ContextActor.Get());
		QueueCountText->SetText(FText::AsNumber(QueueCount));
		QueueCountText->SetVisibility(QueueCount > 0
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
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
		IRTSCommandProgressController::Execute_RequestCancelCommandProgressItem(ProgressActionTarget, ProgressItemId);
	}
}
