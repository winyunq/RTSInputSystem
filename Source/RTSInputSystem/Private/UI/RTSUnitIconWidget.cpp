#include "UI/RTSUnitIconWidget.h"
#include "RTSSelectionSubsystem.h"
#include "UI/RTSTooltipWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

namespace
{
	FString GetUnitIconGroupKey(const FRTSUnitData& Data)
	{
		return Data.GroupKey.IsEmpty() ? Data.Name : Data.GroupKey;
	}

	FString BuildUnitProductionLine(const FRTSUnitData& Data)
	{
		TArray<FString> Parts;
		if (Data.bHasProductionCapacity)
		{
			Parts.Add(FString::Printf(TEXT("产能 %d/%d"),
				Data.ProductionBusyLanes,
				Data.ProductionTotalLanes));
			if (Data.ProductionQueuedOrders > 0)
			{
				Parts.Add(FString::Printf(TEXT("等待 %d"), Data.ProductionQueuedOrders));
			}
		}
		if (Data.bHasActivity)
		{
			FString Activity = Data.ActivityLabel.ToString();
			if (Data.ActivityRemainingSeconds > 0.0f)
			{
				Activity += FString::Printf(TEXT(" %.1fs"), Data.ActivityRemainingSeconds);
			}
			if (Data.ActivityQueueCount > 1)
			{
				Activity += FString::Printf(TEXT(" 队列%d"), Data.ActivityQueueCount);
			}
			Parts.Add(MoveTemp(Activity));
		}
		return FString::Join(Parts, TEXT(" · "));
	}

	FString BuildUnitTooltipDescription(const FRTSUnitData& Data)
	{
		TArray<FString> Lines;

		if (!Data.Role.IsEmpty())
		{
			Lines.Add(FString::Printf(TEXT("<RichText.Yellow>%s</>"), *Data.Role));
		}
		if (Data.Count > 1)
		{
			Lines.Add(FString::Printf(TEXT("数量: <RichText.Yellow>%d</>"), Data.Count));
		}
		if (Data.MaxHealth > 0)
		{
			Lines.Add(FString::Printf(TEXT("生命值: <RichText.Green>%.0f / %.0f</>"), Data.Health, Data.MaxHealth));
		}
		if (Data.MaxEnergy > 0)
		{
			Lines.Add(FString::Printf(TEXT("能量: <RichText.Green>%.0f / %.0f</>"), Data.Energy, Data.MaxEnergy));
		}
		if (Data.MaxShield > 0)
		{
			Lines.Add(FString::Printf(TEXT("护盾: <RichText.Green>%.0f / %.0f</>"), Data.Shield, Data.MaxShield));
		}
		const FString ProductionLine = BuildUnitProductionLine(Data);
		if (!ProductionLine.IsEmpty())
		{
			Lines.Add(FString::Printf(TEXT("<RichText.Yellow>%s</>"), *ProductionLine));
		}

		return FString::Join(Lines, TEXT("<n/>"));
	}
}

TSharedRef<SWidget> URTSUnitIconWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* RootSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("DefaultUnitIconSize"));
		RootSize->SetWidthOverride(128.0f);
		RootSize->SetHeightOverride(128.0f);

		UBorder* Background = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("DefaultUnitIconBackground"));
		Background->SetBrushColor(FLinearColor(0.025f, 0.065f, 0.085f, 0.96f));
		Background->SetPadding(FMargin(4.0f));
		RootSize->AddChild(Background);

		UOverlay* Face = WidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(), TEXT("DefaultUnitIconFace"));
		Background->SetContent(Face);

		UnitIcon = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), TEXT("UnitIcon"));
		if (UOverlaySlot* IconSlot = Face->AddChildToOverlay(UnitIcon))
		{
			IconSlot->SetPadding(FMargin(2.0f, 2.0f, 2.0f, 36.0f));
			IconSlot->SetHorizontalAlignment(HAlign_Fill);
			IconSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UVerticalBox* StatusStack = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("DefaultUnitStatusStack"));
		if (UOverlaySlot* StackSlot = Face->AddChildToOverlay(StatusStack))
		{
			StackSlot->SetPadding(FMargin(4.0f));
			StackSlot->SetHorizontalAlignment(HAlign_Fill);
			StackSlot->SetVerticalAlignment(VAlign_Bottom);
		}

		UnitNameText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("UnitNameText"));
		UnitNameText->SetJustification(ETextJustify::Center);
		UnitNameText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		StatusStack->AddChildToVerticalBox(UnitNameText);

		HealthBar = WidgetTree->ConstructWidget<UProgressBar>(
			UProgressBar::StaticClass(), TEXT("HealthBar"));
		HealthBar->SetFillColorAndOpacity(FLinearColor(0.18f, 0.9f, 0.32f, 1.0f));
		StatusStack->AddChildToVerticalBox(HealthBar);

		EnergyBar = WidgetTree->ConstructWidget<UProgressBar>(
			UProgressBar::StaticClass(), TEXT("EnergyBar"));
		EnergyBar->SetFillColorAndOpacity(FLinearColor(0.15f, 0.55f, 1.0f, 1.0f));
		StatusStack->AddChildToVerticalBox(EnergyBar);

		ShieldBar = WidgetTree->ConstructWidget<UProgressBar>(
			UProgressBar::StaticClass(), TEXT("ShieldBar"));
		ShieldBar->SetFillColorAndOpacity(FLinearColor(0.55f, 0.78f, 1.0f, 1.0f));
		StatusStack->AddChildToVerticalBox(ShieldBar);

		CountText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("CountText"));
		CountText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.82f, 0.2f, 1.0f)));
		if (UOverlaySlot* CountSlot = Face->AddChildToOverlay(CountText))
		{
			CountSlot->SetPadding(FMargin(6.0f));
			CountSlot->SetHorizontalAlignment(HAlign_Right);
			CountSlot->SetVerticalAlignment(VAlign_Top);
		}

		WidgetTree->RootWidget = RootSize;
	}

	return Super::RebuildWidget();
}

void URTSUnitIconWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Slate asks for the rich tooltip only when it is about to open. This keeps
	// tooltip Blueprint loading and widget construction out of selection frames.
	ToolTipWidgetDelegate.BindDynamic(this, &URTSUnitIconWidget::GetOrCreateTooltipWidget);
}

void URTSUnitIconWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UnitSlotFrame)
	{
		UnitSlotFrame->SetVisibility(ESlateVisibility::Hidden);
	}

	if (!UnitIcon)
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSUnitIconWidget: 'UnitIcon' (Image) is NOT bound! Check your WBP naming. Expecting variable named 'UnitIcon'."));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("RTSUnitIconWidget: NativeConstruct - UnitIcon is bound."));
	}
}

void URTSUnitIconWidget::InitData(const FRTSUnitData& Data, bool bShowIcon, bool bShowBars, bool bShowCount, int32 DesiredIconSize)
{
	// Set Icon
	if (UnitIcon)
	{
		if (UnitSlotFrame)
		{
			UnitSlotFrame->SetVisibility(ESlateVisibility::Hidden);
		}

		if (UOverlaySlot* IconSlot = Cast<UOverlaySlot>(UnitIcon->Slot))
		{
			IconSlot->SetPadding(FMargin(0.0f));
			IconSlot->SetHorizontalAlignment(HAlign_Center);
			IconSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (!bShowIcon)
		{
			UnitIcon->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			UnitIcon->SetVisibility(ESlateVisibility::Visible);
			
			UTexture2D* DisplayTexture = Data.Icon ? Data.Icon : Data.Portrait;
			if (DisplayTexture)
			{
				UnitIcon->SetBrushFromTexture(DisplayTexture);
				// Reset color to white (in case it was tinted differently)
				UnitIcon->SetColorAndOpacity(FLinearColor::White);
			}
			else
			{
				UnitIcon->SetColorAndOpacity(FLinearColor::Transparent);
				UnitIcon->SetVisibility(ESlateVisibility::Hidden);
				UE_LOG(LogTemp, Verbose, TEXT("RTSUnitIconWidget: Icon and portrait are null for %s. Hiding icon placeholder."), *Data.Name);
			}

			if (DesiredIconSize > 0)
			{
				UnitIcon->SetDesiredSizeOverride(FVector2D(DesiredIconSize, DesiredIconSize));
			}
		}
	}

	UProgressBar* EffectiveActivityBar = ActivityBar;
	if (!EffectiveActivityBar && Data.MaxShield <= 0.0f)
	{
		// Existing Unit.uasset predates ActivityBar.  Reuse its otherwise-unused
		// shield strip so production progress is visible without an asset migration.
		EffectiveActivityBar = ShieldBar;
	}

	// Update Status Bars
	if (bShowBars)
	{
		UpdateBar(HealthBar, Data.Health, Data.MaxHealth);
		UpdateBar(EnergyBar, Data.Energy, Data.MaxEnergy);
		if (ShieldBar != EffectiveActivityBar)
		{
			UpdateBar(ShieldBar, Data.Shield, Data.MaxShield);
		}
	}
	else
	{
		if(HealthBar) HealthBar->SetVisibility(ESlateVisibility::Collapsed);
		if(EnergyBar) EnergyBar->SetVisibility(ESlateVisibility::Collapsed);
		if(ShieldBar) ShieldBar->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (EffectiveActivityBar)
	{
		EffectiveActivityBar->SetPercent(FMath::Clamp(Data.ActivityProgress, 0.0f, 1.0f));
		EffectiveActivityBar->SetVisibility(Data.bHasActivity
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}

	if (ActivityText)
	{
		ActivityText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (CancelHintText)
	{
		CancelHintText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (UnitNameText)
	{
		UnitNameText->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (CountText)
	{
		if (bShowCount && Data.Count > 1)
		{
			CountText->SetText(FText::AsNumber(Data.Count));
			CountText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			CountText->SetText(FText::GetEmpty());
			CountText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Store for Interaction
	StoredData = Data;

	UpdateTooltip(Data);
}

void URTSUnitIconWidget::SetIsActive(bool bActive)
{
	// Visual feedback for Active vs Inactive group
	// Starcraft style: Inactive groups are dimmed.
	SetRenderOpacity(bActive ? 1.0f : 0.3f);
}

void URTSUnitIconWidget::UpdateBar(UProgressBar* Bar, float Current, float Max)
{
	if (!Bar) return;

	if (Max > 0.0f)
	{
		Bar->SetPercent(FMath::Clamp(Current / Max, 0.0f, 1.0f));
		Bar->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		Bar->SetVisibility(ESlateVisibility::Collapsed);
	}
}

TSubclassOf<URTSTooltipWidget> URTSUnitIconWidget::ResolveTooltipClass() const
{
	if (TooltipClass)
	{
		return TooltipClass;
	}

	static TWeakObjectPtr<UClass> CachedTooltipClass;
	if (!CachedTooltipClass.IsValid())
	{
		CachedTooltipClass = LoadClass<URTSTooltipWidget>(
			nullptr,
			TEXT("/Game/UI/HeadUpDisplay/ControlGird/ButtonMessage.ButtonMessage_C")
		);
	}

	TSubclassOf<URTSTooltipWidget> ResolvedClass;
	ResolvedClass = CachedTooltipClass.Get();
	return ResolvedClass;
}

void URTSUnitIconWidget::UpdateTooltip(const FRTSUnitData& Data)
{
	// Unit icon widgets are pooled. If this slot has already been hovered, keep
	// its existing tooltip content current without constructing a new widget.
	if (UnitTooltipWidget)
	{
		UnitTooltipWidget->SetTooltipContent(
			FText::FromString(Data.Name),
			FText::FromString(BuildUnitTooltipDescription(Data)),
			FText::GetEmpty(),
			Data.Icon
		);
	}
}

UWidget* URTSUnitIconWidget::GetOrCreateTooltipWidget()
{
	if (!UnitTooltipWidget)
	{
		if (TSubclassOf<URTSTooltipWidget> ResolvedTooltipClass = ResolveTooltipClass())
		{
			if (APlayerController* PC = GetOwningPlayer())
			{
				UnitTooltipWidget = CreateWidget<URTSTooltipWidget>(PC, ResolvedTooltipClass);
			}
			else if (UWorld* World = GetWorld())
			{
				UnitTooltipWidget = CreateWidget<URTSTooltipWidget>(World, ResolvedTooltipClass);
			}
		}
	}

	if (UnitTooltipWidget)
	{
		UpdateTooltip(StoredData);
	}
	return UnitTooltipWidget;
}

FReply URTSUnitIconWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Check for Left Click
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			if (ULocalPlayer* LP = PC->GetLocalPlayer())
			{
				if (URTSSelectionSubsystem* Subsystem = LP->GetSubsystem<URTSSelectionSubsystem>())
				{
					// --- Starcraft Logic ---
					
					// Shift + Click = Remove (Exclude)
					if (InMouseEvent.IsShiftDown())
					{
						Subsystem->RemoveUnit(StoredData);
						return FReply::Handled();
					}

					// Ctrl + Click = Select Type (Keep only this group)
					if (InMouseEvent.IsControlDown())
					{
						Subsystem->SelectGroup(GetUnitIconGroupKey(StoredData));
						return FReply::Handled();
					}

					// Normal Click = Select This Unit (Exclusive)
					// We need to construct a single selection.
					TArray<AActor*> NewActors;
					TArray<FEntityHandle> NewEntities;
					
					if (StoredData.ActorPtr) NewActors.Add(StoredData.ActorPtr);
					if (StoredData.EntityHandle.Index > 0) NewEntities.Add(StoredData.EntityHandle);
					
					// If Summary Item (Count > 1), normal click usually Selects the GROUP?
					// In SC2: 
					// - Wireframe (List): Click selects unit.
					// - Summary: Click selects ALL of that type (same as Ctrl+Click in Wireframe).
					// If Count > 1, treating as Ctrl+Click (Group Select).
					
					if (StoredData.Count > 1)
					{
						Subsystem->SelectGroup(GetUnitIconGroupKey(StoredData));
					}
					else
					{
						Subsystem->SetSelectedUnits(NewActors, NewEntities, ERTSSelectionModifier::Replace);
					}
					
					return FReply::Handled();
				}
			}
		}
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
