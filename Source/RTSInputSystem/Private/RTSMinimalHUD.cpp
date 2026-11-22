// Copyright Winyunq, 2026. All Rights Reserved.

#include "RTSMinimalHUD.h"

#include "RTSSelector.h"
#include "UI/RTSActiveGroupWidget.h"
#include "UI/RTSCommanderGridWidget.h"
#include "UI/RTSMinimapJumpWidget.h"
#include "UI/RTSSelectionQueryButton.h"
#include "UI/RTSUnitPanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/SafeZone.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/WrapBox.h"
#include "GameFramework/PlayerController.h"

namespace
{
	const FLinearColor PanelColor(0.008f, 0.024f, 0.038f, 0.94f);
	const FLinearColor AccentColor(0.1f, 0.72f, 0.95f, 1.0f);

	URTSSelectionQueryButton* AddSelectionButton(
		UWidgetTree* WidgetTree,
		UHorizontalBox* Parent,
		TSubclassOf<URTSSelectionQueryButton> ButtonClass,
		const FName Name,
		const FText& Label,
		const FName RequiredTag = NAME_None,
		const bool bCompact = false)
	{
		URTSSelectionQueryButton* Button = WidgetTree->ConstructWidget<URTSSelectionQueryButton>(
			ButtonClass, Name);
		Button->SetBackgroundColor(FLinearColor(0.025f, 0.09f, 0.13f, 1.0f));
		if (!RequiredTag.IsNone())
		{
			Button->SelectionQuery.RequiredSelectionTag =
				FGameplayTag::RequestGameplayTag(RequiredTag, false);
		}

		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(*FString::Printf(TEXT("%sLabel"), *Name.ToString())));
		Text->SetText(Label);
		Text->SetJustification(ETextJustify::Center);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Text->SetAutoWrapText(false);
		Button->AddChild(Text);

		if (UHorizontalBoxSlot* Slot = Parent->AddChildToHorizontalBox(Button))
		{
			Slot->SetPadding(FMargin(bCompact ? 2.0f : 4.0f));
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetVerticalAlignment(VAlign_Fill);
		}
		return Button;
	}

	UScaleBox* AddScaledPanel(
		UWidgetTree* WidgetTree,
		UHorizontalBox* Parent,
		UWidget* Content,
		const FName Name,
		const float Width,
		const float Height,
		const bool bCompact = false,
		const float CompactScale = 1.0f)
	{
		USizeBox* Host = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), FName(*FString::Printf(TEXT("%sHost"), *Name.ToString())));
		Host->SetWidthOverride(Width);
		Host->SetHeightOverride(Height);

		UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>(
			UScaleBox::StaticClass(), Name);
		Scale->SetStretch(bCompact ? EStretch::UserSpecified : EStretch::ScaleToFit);
		Scale->SetStretchDirection(bCompact ? EStretchDirection::Both : EStretchDirection::DownOnly);
		Scale->SetUserSpecifiedScale(CompactScale);
		Scale->SetContent(Content);
		if (bCompact)
		{
			UScrollBox* Vertical = WidgetTree->ConstructWidget<UScrollBox>();
			UScrollBox* Horizontal = WidgetTree->ConstructWidget<UScrollBox>();
			Horizontal->SetOrientation(Orient_Horizontal);
			Horizontal->AddChild(Scale);
			Vertical->AddChild(Horizontal);
			Host->SetContent(Vertical);
		}
		else
		{
			Host->AddChild(Scale);
		}

		if (UHorizontalBoxSlot* Slot = Parent->AddChildToHorizontalBox(Host))
		{
			Slot->SetPadding(FMargin(5.0f, 0.0f));
			Slot->SetHorizontalAlignment(HAlign_Center);
			Slot->SetVerticalAlignment(VAlign_Bottom);
		}
		return Scale;
	}
}

TSharedRef<SWidget> URTSMinimalHUDWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
		bool bCompact = ViewportSize.X > 0.0f && ViewportSize.X <= 1100.0f;
#if PLATFORM_ANDROID
		bCompact = true;
#endif
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("RTSMinimalHUDRoot"));
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		UBorder* QuickSelectionFrame = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("QuickSelectionFrame"));
		QuickSelectionFrame->SetBrushColor(PanelColor);
		QuickSelectionFrame->SetPadding(FMargin(bCompact ? 0.0f : 6.0f));
		UHorizontalBox* QuickSelection = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("QuickSelection"));
		QuickSelectionFrame->SetContent(QuickSelection);

		AddSelectionButton(
			WidgetTree, QuickSelection, URTSSelectAllArmyButton::StaticClass(),
			TEXT("SelectArmy"), bCompact ? FText::FromString(TEXT("全部")) : NSLOCTEXT("RTSInputSystem", "MinimalHUDArmy", "ARMY"), NAME_None, bCompact);
		AddSelectionButton(
			WidgetTree, QuickSelection, URTSSelectAllIdleArmyButton::StaticClass(),
			TEXT("SelectIdle"), bCompact ? FText::FromString(TEXT("待命")) : NSLOCTEXT("RTSInputSystem", "MinimalHUDIdle", "IDLE"), NAME_None, bCompact);
		AddSelectionButton(
			WidgetTree, QuickSelection, URTSSelectionQueryButton::StaticClass(),
			TEXT("SelectInfantry"), bCompact ? FText::FromString(TEXT("步兵")) : NSLOCTEXT("RTSInputSystem", "MinimalHUDInfantry", "INFANTRY"),
			TEXT("RTS.Selection.Army.Ground.Infantry"), bCompact);
		AddSelectionButton(
			WidgetTree, QuickSelection, URTSSelectionQueryButton::StaticClass(),
			TEXT("SelectArmor"), bCompact ? FText::FromString(TEXT("装甲")) : NSLOCTEXT("RTSInputSystem", "MinimalHUDArmor", "ARMOR"),
			TEXT("RTS.Selection.Army.Ground.Armor"), bCompact);
		AddSelectionButton(
			WidgetTree, QuickSelection, URTSSelectionQueryButton::StaticClass(),
			TEXT("SelectStructures"), bCompact ? FText::FromString(TEXT("建筑")) : NSLOCTEXT("RTSInputSystem", "MinimalHUDStructures", "STRUCTURES"),
			TEXT("RTS.Selection.Structure"), bCompact);

		if (UCanvasPanelSlot* QuickSlot = Root->AddChildToCanvas(QuickSelectionFrame))
		{
			QuickSlot->SetAnchors(FAnchors(0.5f, 0.0f));
			QuickSlot->SetAlignment(FVector2D(0.5f, 0.0f));
			QuickSlot->SetPosition(FVector2D(0.0f, 20.0f));
			QuickSlot->SetSize(FVector2D(760.0f, 54.0f));
		}

		UBorder* BottomFrame = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("BottomFrame"));
		BottomFrame->SetBrushColor(bCompact ? FLinearColor::Transparent : PanelColor);
		BottomFrame->SetPadding(FMargin(bCompact ? 4.0f : 8.0f));
		UHorizontalBox* BottomPanels = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("BottomPanels"));
		BottomFrame->SetContent(BottomPanels);

		USizeBox* MinimapHost = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("MinimapHost"));
		MinimapHost->SetWidthOverride(bCompact ? 88.0f : 250.0f);
		MinimapHost->SetHeightOverride(bCompact ? 88.0f : 250.0f);
		UOverlay* MinimapOverlay = WidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(), TEXT("MinimapOverlay"));
		MinimapHost->AddChild(MinimapOverlay);

		UBorder* MinimapBackground = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("MinimapBackground"));
		MinimapBackground->SetBrushColor(FLinearColor(0.025f, 0.07f, 0.08f, 1.0f));
		UTextBlock* MinimapLabel = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("MinimapLabel"));
		MinimapLabel->SetText(NSLOCTEXT("RTSInputSystem", "MinimalHUDMinimapTitle", "地图"));
		MinimapLabel->SetJustification(ETextJustify::Center);
		MinimapLabel->SetColorAndOpacity(FSlateColor(AccentColor));
		MinimapBackground->SetContent(MinimapLabel);
		MinimapOverlay->AddChildToOverlay(MinimapBackground);

		URTSMinimapJumpWidget* MinimapInput = WidgetTree->ConstructWidget<URTSMinimapJumpWidget>(
			URTSMinimapJumpWidget::StaticClass(), TEXT("MinimapInput"));
		if (UOverlaySlot* MinimapInputSlot = MinimapOverlay->AddChildToOverlay(MinimapInput))
		{
			MinimapInputSlot->SetHorizontalAlignment(HAlign_Fill);
			MinimapInputSlot->SetVerticalAlignment(VAlign_Fill);
		}
		if (UHorizontalBoxSlot* MinimapSlot = BottomPanels->AddChildToHorizontalBox(MinimapHost))
		{
			MinimapSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
			MinimapSlot->SetVerticalAlignment(VAlign_Bottom);
		}

		URTSUnitPanelWidget* UnitPanel = WidgetTree->ConstructWidget<URTSUnitPanelWidget>(
			URTSUnitPanelWidget::StaticClass(), TEXT("UnitPanel"));
		AddScaledPanel(WidgetTree, BottomPanels, UnitPanel, TEXT("UnitPanelScale"), bCompact ? 300.0f : 650.0f, bCompact ? 88.0f : 300.0f, bCompact, 0.4f);

		URTSActiveGroupWidget* ActiveGroup = WidgetTree->ConstructWidget<URTSActiveGroupWidget>(
			URTSActiveGroupWidget::StaticClass(), TEXT("ActiveGroup"));
		AddScaledPanel(WidgetTree, BottomPanels, ActiveGroup, TEXT("ActiveGroupScale"), bCompact ? 56.0f : 150.0f, bCompact ? 88.0f : 150.0f, bCompact, 0.2f);

		URTSCommanderGridWidget* CommandGrid = WidgetTree->ConstructWidget<URTSCommanderGridWidget>(
			URTSCommanderGridWidget::StaticClass(), TEXT("CommandGrid"));
		AddScaledPanel(WidgetTree, BottomPanels, CommandGrid, TEXT("CommandGridScale"), bCompact ? 300.0f : 520.0f, bCompact ? 88.0f : 300.0f, bCompact, 0.5f);

		if (UCanvasPanelSlot* BottomSlot = Root->AddChildToCanvas(BottomFrame))
		{
			BottomSlot->SetAnchors(FAnchors(0.5f, 1.0f));
			BottomSlot->SetAlignment(FVector2D(0.5f, 1.0f));
			BottomSlot->SetPosition(FVector2D(0.0f, -18.0f));
			BottomSlot->SetSize(FVector2D(1600.0f, 318.0f));
		}

		if (bCompact)
		{
			UWrapBox* MobileBottomPanels = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("MobileBottomPanels"));
			MobileBottomPanels->SetExplicitWrapSize(true);
			MobileBottomPanels->SetInnerSlotPadding(FVector2D(4.0f, 4.0f));
			MobileBottomPanels->SetHorizontalAlignment(HAlign_Center);
			const TArray<UWidget*> Panels = BottomPanels->GetAllChildren();
			BottomPanels->ClearChildren();
			BottomFrame->SetContent(MobileBottomPanels);
			for (UWidget* Panel : Panels)
			{
				MobileBottomPanels->AddChildToWrapBox(Panel);
			}
			CompactHudScale = WidgetTree->ConstructWidget<UScaleBox>();
			CompactHudScale->SetStretch(EStretch::UserSpecified);
			CompactHudScale->SetStretchDirection(EStretchDirection::Both);
			CompactHudSize = WidgetTree->ConstructWidget<USizeBox>();
			CompactHudSize->SetContent(Root);
			CompactHudScale->SetContent(CompactHudSize);
			USafeZone* SafeZone = WidgetTree->ConstructWidget<USafeZone>();
			SafeZone->SetSidesToPad(true, true, true, true);
			SafeZone->SetContent(CompactHudScale);
			WidgetTree->RootWidget = SafeZone;
		}
		else
		{
			WidgetTree->RootWidget = Root;
		}
	}

	return Super::RebuildWidget();
}

void URTSMinimalHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!CompactHudScale || !CompactHudSize)
	{
		return;
	}
	const float ViewportScale = UWidgetLayoutLibrary::GetViewportScale(this);
	const FVector2D SafePixels = CompactHudScale->GetCachedGeometry().GetLocalSize() * ViewportScale;
	if (SafePixels.X <= 0.0f || SafePixels.Y <= 0.0f || ViewportScale <= 0.0f)
	{
		return;
	}
	float LayoutScale = 1.0f;
#if PLATFORM_ANDROID
	LayoutScale = FMath::Min(SafePixels.X, SafePixels.Y) / 390.0f;
#endif
	const FVector2D LogicalSize = SafePixels / LayoutScale;
	if (LogicalSize.Equals(CompactLogicalSize, 0.5f) && FMath::IsNearlyEqual(ViewportScale, CompactViewportScale)
		&& FMath::IsNearlyEqual(LayoutScale, CompactLayoutScale))
	{
		return;
	}
	CompactLogicalSize = LogicalSize;
	CompactViewportScale = ViewportScale;
	CompactLayoutScale = LayoutScale;
	CompactHudScale->SetUserSpecifiedScale(LayoutScale / ViewportScale);
	CompactHudSize->SetWidthOverride(LogicalSize.X);
	CompactHudSize->SetHeightOverride(LogicalSize.Y);
	UCanvasPanelSlot* QuickSlot = CastChecked<UCanvasPanelSlot>(WidgetTree->FindWidget(TEXT("QuickSelectionFrame"))->Slot);
	QuickSlot->SetPosition(FVector2D(0.0f, 8.0f));
	QuickSlot->SetSize(FVector2D(FMath::Min(500.0f, LogicalSize.X - 16.0f), 48.0f));
	UCanvasPanelSlot* BottomSlot = CastChecked<UCanvasPanelSlot>(WidgetTree->FindWidget(TEXT("BottomFrame"))->Slot);
	BottomSlot->SetPosition(FVector2D(0.0f, -8.0f));
	const bool bPortrait = LogicalSize.Y > LogicalSize.X;
	BottomSlot->SetSize(FVector2D(LogicalSize.X - 16.0f, bPortrait ? 188.0f : 96.0f));
	const float InnerWidth = LogicalSize.X - 24.0f;
	CastChecked<UWrapBox>(WidgetTree->FindWidget(TEXT("MobileBottomPanels")))->SetWrapSize(InnerWidth);
	const float AvailableWidth = InnerWidth - 156.0f;
	const float CommandWidth = bPortrait ? InnerWidth : FMath::Min(300.0f, AvailableWidth * 0.6f);
	CastChecked<USizeBox>(WidgetTree->FindWidget(TEXT("UnitPanelScaleHost")))->SetWidthOverride(bPortrait ? AvailableWidth + 4.0f : AvailableWidth - CommandWidth);
	CastChecked<USizeBox>(WidgetTree->FindWidget(TEXT("CommandGridScaleHost")))->SetWidthOverride(CommandWidth);
	for (const TCHAR* Name : { TEXT("SelectArmyLabel"), TEXT("SelectIdleLabel"), TEXT("SelectInfantryLabel"), TEXT("SelectArmorLabel"), TEXT("SelectStructuresLabel"), TEXT("MinimapLabel") })
	{
		UTextBlock* Text = CastChecked<UTextBlock>(WidgetTree->FindWidget(Name));
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = 14;
		Text->SetFont(Font);
	}
}

void ARTSMinimalHUD::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController || MinimalHUDWidget)
	{
		return;
	}

	if (!MinimalHUDWidgetClass)
	{
		MinimalHUDWidgetClass = URTSMinimalHUDWidget::StaticClass();
	}

	MinimalHUDWidget = CreateWidget<URTSMinimalHUDWidget>(
		PlayerController, MinimalHUDWidgetClass);
	if (MinimalHUDWidget)
	{
		MinimalHUDWidget->AddToViewport(0);
	}
}

ARTSMinimalPlayerController::ARTSMinimalPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	RTSSelector = CreateDefaultSubobject<URTSSelector>(TEXT("RTSSelector"));
}

ARTSMinimalGameMode::ARTSMinimalGameMode()
{
	HUDClass = ARTSMinimalHUD::StaticClass();
	PlayerControllerClass = ARTSMinimalPlayerController::StaticClass();
}
