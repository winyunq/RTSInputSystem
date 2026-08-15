#include "UI/RTSUnitPanelWidget.h"
#include "UI/RTSCommandButtonWidget.h"
#include "UI/RTSCommanderGridWidget.h"
#include "UI/RTSFormationListWidget.h"
#include "UI/RTSUnitIconWidget.h"
#include "RTSInputPanelSettings.h"
#include "RTSSelectionSubsystem.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Interfaces/RTSCommandProgressProvider.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/WrapBox.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Layout/SBox.h"

namespace
{
	FString GetSelectionWidgetUnitGroupKey(const FRTSUnitData& Data)
	{
		return Data.GroupKey.IsEmpty() ? Data.Name : Data.GroupKey;
	}

}

void URTSUnitPanelWidget::NativePreConstruct()
{
	Super::NativePreConstruct();

	ApplySelectionPanelLayoutSettings();
}

void URTSUnitPanelWidget::ApplySelectionPanelLayoutSettings()
{
	FMargin PanelContentPadding(16.0f, 4.0f, 16.0f, 4.0f);
	if (const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>())
	{
		MaxRows = FMath::Max(1, Settings->SelectionGridRows);
		MaxColumns = FMath::Max(1, Settings->SelectionGridColumns);
		IconSlotSize = FMath::Max(1, Settings->SelectionIconSize);
		PanelHeaderHeight = FMath::Max(0.0f, Settings->SelectionPanelHeaderHeight);
		PanelContentPadding = Settings->SelectionPanelContentPadding;
	}

	if (UnitPanelFrame)
	{
		UnitPanelFrame->SetPadding(PanelContentPadding);
	}

	if (IconContainer)
	{
		if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(IconContainer->Slot))
		{
			OverlaySlot->SetHorizontalAlignment(HAlign_Center);
			OverlaySlot->SetVerticalAlignment(VAlign_Center);
			OverlaySlot->SetPadding(PanelContentPadding);
		}
	}

	ApplyFixedPanelSlotLayout();
	ApplyFixedPanelBounds();
}

void URTSUnitPanelWidget::ApplyFixedPanelSlotLayout()
{
	auto SetFillSlot = [](UWidget* Widget)
	{
		if (!Widget || !Widget->Slot)
		{
			return;
		}

		if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
		{
			FSlateChildSize FillSize;
			FillSize.SizeRule = ESlateSizeRule::Fill;
			FillSize.Value = 1.0f;
			HorizontalSlot->SetSize(FillSize);
			HorizontalSlot->SetHorizontalAlignment(HAlign_Fill);
			HorizontalSlot->SetVerticalAlignment(VAlign_Fill);
		}
		else if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
		{
			FSlateChildSize FillSize;
			FillSize.SizeRule = ESlateSizeRule::Fill;
			FillSize.Value = 1.0f;
			VerticalSlot->SetSize(FillSize);
			VerticalSlot->SetHorizontalAlignment(HAlign_Fill);
			VerticalSlot->SetVerticalAlignment(VAlign_Fill);
		}
		else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Widget->Slot))
		{
			OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
			OverlaySlot->SetVerticalAlignment(VAlign_Fill);
		}
		else if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(Widget->Slot))
		{
			BorderSlot->SetHorizontalAlignment(HAlign_Fill);
			BorderSlot->SetVerticalAlignment(VAlign_Fill);
		}
	};

	auto SetAutoCenterSlot = [](UWidget* Widget)
	{
		if (!Widget || !Widget->Slot)
		{
			return;
		}

		if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
		{
			FSlateChildSize AutoSize;
			AutoSize.SizeRule = ESlateSizeRule::Automatic;
			AutoSize.Value = 1.0f;
			HorizontalSlot->SetSize(AutoSize);
			HorizontalSlot->SetHorizontalAlignment(HAlign_Center);
			HorizontalSlot->SetVerticalAlignment(VAlign_Center);
		}
		else if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
		{
			FSlateChildSize AutoSize;
			AutoSize.SizeRule = ESlateSizeRule::Automatic;
			AutoSize.Value = 1.0f;
			VerticalSlot->SetSize(AutoSize);
			VerticalSlot->SetHorizontalAlignment(HAlign_Center);
			VerticalSlot->SetVerticalAlignment(VAlign_Center);
		}
		else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Widget->Slot))
		{
			OverlaySlot->SetHorizontalAlignment(HAlign_Center);
			OverlaySlot->SetVerticalAlignment(VAlign_Center);
		}
		else if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(Widget->Slot))
		{
			BorderSlot->SetHorizontalAlignment(HAlign_Center);
			BorderSlot->SetVerticalAlignment(VAlign_Center);
		}
	};

	auto SetAutoFillWidthCenterSlot = [](UWidget* Widget)
	{
		if (!Widget || !Widget->Slot)
		{
			return;
		}

		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
		{
			FSlateChildSize AutoSize;
			AutoSize.SizeRule = ESlateSizeRule::Automatic;
			AutoSize.Value = 1.0f;
			VerticalSlot->SetSize(AutoSize);
			VerticalSlot->SetHorizontalAlignment(HAlign_Fill);
			VerticalSlot->SetVerticalAlignment(VAlign_Center);
		}
	};

	auto SetHorizontalAutoSlot = [](UWidget* Widget, EHorizontalAlignment HorizontalAlignment)
	{
		if (!Widget || !Widget->Slot)
		{
			return;
		}

		if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
		{
			FSlateChildSize AutoSize;
			AutoSize.SizeRule = ESlateSizeRule::Automatic;
			AutoSize.Value = 1.0f;
			HorizontalSlot->SetSize(AutoSize);
			HorizontalSlot->SetHorizontalAlignment(HorizontalAlignment);
			HorizontalSlot->SetVerticalAlignment(VAlign_Center);
		}
	};

	auto SetHorizontalFillSlot = [](UWidget* Widget)
	{
		if (!Widget || !Widget->Slot)
		{
			return;
		}

		if (UHorizontalBoxSlot* HorizontalSlot = Cast<UHorizontalBoxSlot>(Widget->Slot))
		{
			FSlateChildSize FillSize;
			FillSize.SizeRule = ESlateSizeRule::Fill;
			FillSize.Value = 1.0f;
			HorizontalSlot->SetSize(FillSize);
			HorizontalSlot->SetHorizontalAlignment(HAlign_Fill);
			HorizontalSlot->SetVerticalAlignment(VAlign_Fill);
		}
	};

	auto SetAutoLeftCenterSlot = [](UWidget* Widget)
	{
		if (!Widget || !Widget->Slot)
		{
			return;
		}

		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
		{
			FSlateChildSize AutoSize;
			AutoSize.SizeRule = ESlateSizeRule::Automatic;
			AutoSize.Value = 1.0f;
			VerticalSlot->SetSize(AutoSize);
			VerticalSlot->SetHorizontalAlignment(HAlign_Left);
			VerticalSlot->SetVerticalAlignment(VAlign_Center);
		}
	};

	SetFillSlot(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("LeftArea_Overlay")));
	SetFillSlot(UnitPanelFrame);
	SetFillSlot(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("UnitPanelContentRoot")));
	SetFillSlot(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("UnitPanelBody")));
	SetFillSlot(UnitRosterPane);
	SetFillSlot(IconContainer);
	SetFillSlot(UnitDetailPane);

	SetAutoLeftCenterSlot(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("UnitFormationList")));
	SetFillSlot(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("SingleDetailTopSpacer")));
	SetFillSlot(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("SingleDetailBottomSpacer")));

	if (UBorder* UnitPanelFrameBorder = Cast<UBorder>(UnitPanelFrame))
	{
		UnitPanelFrameBorder->SetBrushColor(FLinearColor(0.006f, 0.018f, 0.028f, 0.98f));
		UnitPanelFrameBorder->SetPadding(FMargin(8.0f));
	}

	if (UBorder* UnitRosterPaneBorder = Cast<UBorder>(UnitRosterPane))
	{
		UnitRosterPaneBorder->SetBrushColor(FLinearColor(0.012f, 0.036f, 0.052f, 0.97f));
		UnitRosterPaneBorder->SetPadding(FMargin(6.0f));
	}

	if (UBorder* UnitIdentityPaneWidget = Cast<UBorder>(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("UnitIdentityPane"))))
	{
		SetAutoFillWidthCenterSlot(UnitIdentityPaneWidget);
		UnitIdentityPaneWidget->SetBrushColor(FLinearColor(0.018f, 0.052f, 0.072f, 0.98f));
		UnitIdentityPaneWidget->SetPadding(FMargin(18.0f, 14.0f, 18.0f, 14.0f));
	}

	SetFillSlot(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("UnitIdentityRow")));

	if (USizeBox* UnitIconContainerBox = Cast<USizeBox>(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("UnitIconContainer"))))
	{
		SetHorizontalAutoSlot(UnitIconContainerBox, HAlign_Left);
		UnitIconContainerBox->SetWidthOverride(IconSlotSize);
		UnitIconContainerBox->SetHeightOverride(IconSlotSize);
	}

	if (USpacer* IconToInfoSpacer = Cast<USpacer>(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("Spacer_IconToInfo"))))
	{
		SetHorizontalFillSlot(IconToInfoSpacer);
		IconToInfoSpacer->SetSize(FVector2D(48.0f, 1.0f));
	}

	SetHorizontalAutoSlot(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("InfoVerticalBox")), HAlign_Right);

	if (UTextBlock* NameText = Cast<UTextBlock>(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("UnitNameText"))))
	{
		NameText->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 1.0f, 0.91f, 1.0f)));
		NameText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
		NameText->SetShadowOffset(FVector2D(1.0f, 1.0f));
	}

	if (UProgressBar* Health = Cast<UProgressBar>(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("HealthBar"))))
	{
		Health->SetFillColorAndOpacity(FLinearColor(0.22f, 1.0f, 0.42f, 1.0f));
	}
	if (UProgressBar* Energy = Cast<UProgressBar>(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("EnergyBar"))))
	{
		Energy->SetFillColorAndOpacity(FLinearColor(0.15f, 0.62f, 1.0f, 1.0f));
	}
	if (UProgressBar* Shield = Cast<UProgressBar>(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("ShieldBar"))))
	{
		Shield->SetFillColorAndOpacity(FLinearColor(0.62f, 0.82f, 1.0f, 1.0f));
	}
	if (UProgressBar* Activity = Cast<UProgressBar>(FindDescendantWidgetByName(Cast<UWidget>(this), TEXT("ActivityBar"))))
	{
		Activity->SetFillColorAndOpacity(FLinearColor(1.0f, 0.72f, 0.16f, 1.0f));
	}
}

TSharedRef<SWidget> URTSUnitPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UnitPanelFrame = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("UnitPanelFrame"));
		UnitPanelFrame->SetBrushColor(FLinearColor(0.012f, 0.035f, 0.052f, 0.97f));
		UnitPanelFrame->SetPadding(FMargin(8.0f));

		UVerticalBox* ContentRoot = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("UnitPanelContentRoot"));
		UnitPanelFrame->SetContent(ContentRoot);

		URTSFormationListWidget* FormationList = WidgetTree->ConstructWidget<URTSFormationListWidget>(
			URTSFormationListWidget::StaticClass(), TEXT("UnitFormationList"));
		ContentRoot->AddChildToVerticalBox(FormationList);

		UHorizontalBox* DetailPane = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("UnitDetailPane"));
		UnitDetailPane = DetailPane;
		if (UVerticalBoxSlot* DetailSlot = ContentRoot->AddChildToVerticalBox(DetailPane))
		{
			DetailSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			DetailSlot->SetVerticalAlignment(VAlign_Center);
		}

		USizeBox* DetailIconSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("UnitIconContainer"));
		DetailIconSize->SetWidthOverride(IconSlotSize);
		DetailIconSize->SetHeightOverride(IconSlotSize);
		UImage* DetailIcon = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), TEXT("UnitIconImage"));
		DetailIconSize->AddChild(DetailIcon);
		DetailPane->AddChildToHorizontalBox(DetailIconSize);

		UVerticalBox* DetailText = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("InfoVerticalBox"));
		if (UHorizontalBoxSlot* DetailTextSlot = DetailPane->AddChildToHorizontalBox(DetailText))
		{
			DetailTextSlot->SetPadding(FMargin(18.0f, 0.0f, 0.0f, 0.0f));
			DetailTextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			DetailTextSlot->SetVerticalAlignment(VAlign_Center);
		}

		auto AddDetailText = [this, DetailText](const FName Name, const FLinearColor Color)
		{
			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			Text->SetColorAndOpacity(FSlateColor(Color));
			DetailText->AddChildToVerticalBox(Text);
			return Text;
		};
		AddDetailText(TEXT("UnitNameText"), FLinearColor(0.78f, 1.0f, 0.91f, 1.0f));
		AddDetailText(TEXT("UnitRoleText"), FLinearColor(0.72f, 0.78f, 0.82f, 1.0f));
		AddDetailText(TEXT("UnitTypeText"), FLinearColor(0.48f, 0.62f, 0.72f, 1.0f));

		auto AddDetailBar = [this, DetailText](const FName Name, const FLinearColor Color)
		{
			UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), Name);
			Bar->SetFillColorAndOpacity(Color);
			DetailText->AddChildToVerticalBox(Bar);
		};
		AddDetailBar(TEXT("HealthBar"), FLinearColor(0.18f, 0.9f, 0.32f, 1.0f));
		AddDetailBar(TEXT("EnergyBar"), FLinearColor(0.15f, 0.55f, 1.0f, 1.0f));
		AddDetailBar(TEXT("ShieldBar"), FLinearColor(0.55f, 0.78f, 1.0f, 1.0f));
		AddDetailText(TEXT("ActivityText"), FLinearColor(1.0f, 0.72f, 0.16f, 1.0f));
		AddDetailBar(TEXT("ActivityBar"), FLinearColor(1.0f, 0.72f, 0.16f, 1.0f));

		UUniformGridPanel* Roster = WidgetTree->ConstructWidget<UUniformGridPanel>(
			UUniformGridPanel::StaticClass(), TEXT("IconContainer"));
		UnitRosterPane = Roster;
		IconContainer = Roster;
		if (UVerticalBoxSlot* RosterSlot = ContentRoot->AddChildToVerticalBox(Roster))
		{
			RosterSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		UnitIconClass = URTSUnitIconWidget::StaticClass();
		IconWidgetClass = URTSUnitIconWidget::StaticClass();
		CommandButtonWidgetClass = URTSCommandButtonWidget::StaticClass();
		WidgetTree->RootWidget = UnitPanelFrame;
	}

	const TSharedRef<SWidget> BuiltWidget = Super::RebuildWidget();
	const FVector2D FixedSize = CalculateFixedPanelSize();

	FixedPanelBoundsBox = SNew(SBox)
		.WidthOverride(FixedSize.X)
		.HeightOverride(FixedSize.Y)
		[
			BuiltWidget
		];

	return FixedPanelBoundsBox.ToSharedRef();
}

void URTSUnitPanelWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	FixedPanelBoundsBox.Reset();
}

FVector2D URTSUnitPanelWidget::CalculateFixedPanelSize() const
{
	const int32 Rows = FMath::Max(1, MaxRows);
	const int32 Columns = FMath::Max(1, MaxColumns);
	const int32 CellSize = FMath::Max(1, IconSlotSize);
	const float HeaderHeight = FMath::Max(0.0f, PanelHeaderHeight);
	FMargin PanelContentPadding(16.0f, 4.0f, 16.0f, 4.0f);

	if (const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>())
	{
		PanelContentPadding = Settings->SelectionPanelContentPadding;
	}

	const float Width = static_cast<float>(Columns * CellSize) + PanelContentPadding.Left + PanelContentPadding.Right;
	const float Height = HeaderHeight + static_cast<float>(Rows * CellSize) + PanelContentPadding.Top + PanelContentPadding.Bottom;
	return FVector2D(Width, Height);
}

void URTSUnitPanelWidget::ApplyFixedPanelBounds()
{
	if (!FixedPanelBoundsBox.IsValid())
	{
		return;
	}

	const FVector2D FixedSize = CalculateFixedPanelSize();
	FixedPanelBoundsBox->SetWidthOverride(FixedSize.X);
	FixedPanelBoundsBox->SetHeightOverride(FixedSize.Y);
}

void URTSUnitPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ApplySelectionPanelLayoutSettings();

	// Template Extraction Logic
	if (IconContainer)
	{
		int32 ExplicitRows = MaxRows;
		int32 ExplicitColumns = MaxColumns;
		bool bHasExplicitRows = false;
		bool bHasExplicitColumns = false;

		// UnitPanel capacity is a panel-level contract. Designer children are templates,
		// so child count must not resize the fixed UnitPanel shell.
		if (UGridPanel* GridPanel = Cast<UGridPanel>(IconContainer))
		{
			if (GridPanel->RowFill.Num() > 0)
			{
				ExplicitRows = GridPanel->RowFill.Num();
				bHasExplicitRows = true;
			}
			if (GridPanel->ColumnFill.Num() > 0)
			{
				ExplicitColumns = GridPanel->ColumnFill.Num();
				bHasExplicitColumns = true;
			}
		}

		// Scan children only for class templates. Their positions and count do not define capacity.
		const int32 ChildrenCount = IconContainer->GetChildrenCount();
		for (int32 i = 0; i < ChildrenCount; ++i)
		{
			UWidget* Child = IconContainer->GetChildAt(i);
			if (!Child) continue;

			if (!IconWidgetClass)
			{
				if (URTSUnitIconWidget* IconWidget = Cast<URTSUnitIconWidget>(Child))
				{
					IconWidgetClass = IconWidget->GetClass();
				}
			}
			if (!CountWidgetClass)
			{
				if (UTextBlock* TextBlock = Cast<UTextBlock>(Child))
				{
					CountWidgetClass = TextBlock->GetClass();
				}
			}
		}

		if (bHasExplicitRows || bHasExplicitColumns)
		{
			MaxRows = FMath::Max(1, ExplicitRows);
			MaxColumns = FMath::Max(1, ExplicitColumns);
			UE_LOG(LogTemp, Log, TEXT("RTSUnitPanelWidget: Using explicit grid capacity: %d Rows x %d Cols"),
				MaxRows, MaxColumns);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("RTSUnitPanelWidget: Using configured grid capacity: %d Rows x %d Cols"),
				MaxRows, MaxColumns);
		}

		ApplyFixedPanelBounds();

		// Clear templates from view so we can populate fresh data
		IconContainer->ClearChildren();
	}

	if (!IconWidgetClass && UnitIconClass)
	{
		IconWidgetClass = UnitIconClass;
	}

	if (!IconWidgetClass)
	{
		IconWidgetClass = LoadClass<URTSUnitIconWidget>(
			nullptr,
			TEXT("/Game/UI/HeadUpDisplay/UnitDetails/Unit.Unit_C")
		);
	}
	if (!IconContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSUnitPanelWidget: IconContainer is not bound. UnitPanel shell can remain visible, but list/summary content cannot be built."));
	}
	else if (IconWidgetClass)
	{
		UE_LOG(LogTemp, Log, TEXT("RTSUnitPanelWidget: IconWidgetClass resolved to %s"), *IconWidgetClass->GetName());

		// --- Initialize Widget Pool ---
		// Calculate Total Slots based on Grid Dimensions
		ItemsPerPage = MaxRows * MaxColumns;
		UE_LOG(LogTemp, Log, TEXT("RTSUnitPanelWidget: Initializing Pool for %d x %d = %d slots."), MaxRows, MaxColumns, ItemsPerPage);

		IconSlots.Reset();
		ProgressButtonSlots.Reset();
		CountSlots.Reset();

		// Support for various container types
		UUniformGridPanel* UniformGrid = Cast<UUniformGridPanel>(IconContainer);
		UGridPanel* GenericGrid = Cast<UGridPanel>(IconContainer);
		UWrapBox* WrapBox = Cast<UWrapBox>(IconContainer);

		if (UniformGrid)
		{
			UniformGrid->SetSlotPadding(FMargin(0.0f));
			UniformGrid->SetMinDesiredSlotWidth(IconSlotSize);
			UniformGrid->SetMinDesiredSlotHeight(IconSlotSize);
		}

		// Helper for layout
		int32 CurrentCol = 0;
		int32 CurrentRow = 0;
		auto AdvanceCursor = [&]() {
			CurrentCol++;
			if (CurrentCol >= MaxColumns)
			{
				CurrentCol = 0;
				CurrentRow++;
			}
		};

		// Create fixed grid pools. Summary mode uses two cells per group: icon cell, then count cell.

		for (int32 i = 0; i < ItemsPerPage; i++)
		{
			if (IconWidgetClass->IsChildOf(URTSUnitIconWidget::StaticClass()))
			{
				URTSUnitIconWidget* NewWidget = CreateWidget<URTSUnitIconWidget>(this, IconWidgetClass);
				if (NewWidget)
				{
					// Add to Container
					if (UniformGrid)
					{
						UUniformGridSlot* NewSlot = UniformGrid->AddChildToUniformGrid(NewWidget, CurrentRow, CurrentCol);
						if (NewSlot) { NewSlot->SetHorizontalAlignment(HAlign_Fill); NewSlot->SetVerticalAlignment(VAlign_Fill); }
						AdvanceCursor();
					}
					else if (GenericGrid)
					{
						UGridSlot* NewSlot = GenericGrid->AddChildToGrid(NewWidget, CurrentRow, CurrentCol);
						if (NewSlot) { NewSlot->SetHorizontalAlignment(HAlign_Fill); NewSlot->SetVerticalAlignment(VAlign_Fill); }
						AdvanceCursor();
					}
					else if (WrapBox)
					{
						WrapBox->AddChildToWrapBox(NewWidget);
					}
					else
					{
						IconContainer->AddChild(NewWidget);
					}

					// Hidden keeps the fixed grid structure while removing empty slot visuals.
					NewWidget->SetVisibility(ESlateVisibility::Hidden);
					
					IconSlots.Add(NewWidget);
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("RTSUnitPanelWidget: IconWidgetClass %s is not a URTSUnitIconWidget subclass."), *IconWidgetClass->GetName());
				break;
			}
		}

		for (int32 i = 0; i < ItemsPerPage; ++i)
		{
			UTextBlock* NewCount = WidgetTree
				? WidgetTree->ConstructWidget<UTextBlock>(
					UTextBlock::StaticClass(),
					FName(*FString::Printf(TEXT("SelectionSummaryCount_%02d"), i)))
				: NewObject<UTextBlock>(this);

			if (!NewCount)
			{
				continue;
			}

			NewCount->SetVisibility(ESlateVisibility::Collapsed);
			NewCount->SetJustification(ETextJustify::Center);
			NewCount->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 1.0f, 0.72f, 1.0f)));
			NewCount->SetShadowColorAndOpacity(FLinearColor::Black);
			NewCount->SetShadowOffset(FVector2D(1.0f, 1.0f));

			FSlateFontInfo FontInfo = NewCount->GetFont();
			FontInfo.Size = FMath::Max(22, IconSlotSize / 2);
			NewCount->SetFont(FontInfo);

			if (UniformGrid)
			{
				UUniformGridSlot* CountSlot = UniformGrid->AddChildToUniformGrid(NewCount, 0, 0);
				if (CountSlot) { CountSlot->SetHorizontalAlignment(HAlign_Fill); CountSlot->SetVerticalAlignment(VAlign_Fill); }
			}
			else if (GenericGrid)
			{
				UGridSlot* CountSlot = GenericGrid->AddChildToGrid(NewCount, 0, 0);
				if (CountSlot) { CountSlot->SetHorizontalAlignment(HAlign_Fill); CountSlot->SetVerticalAlignment(VAlign_Fill); }
			}
			else if (WrapBox)
			{
				WrapBox->AddChildToWrapBox(NewCount);
			}
			else if (IconContainer)
			{
				IconContainer->AddChild(NewCount);
			}

			CountSlots.Add(NewCount);
		}
		UE_LOG(LogTemp, Log, TEXT("RTSUnitPanelWidget: Initialized Pool with %d widgets."), IconSlots.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSUnitPanelWidget: IconWidgetClass is NULL! Grid will be empty. Set it in Details or add a template child."));
	}

	// The panel shell itself must not remain as an empty white rectangle.
	ShowEmptyContent();

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* Subsystem = LP->GetSubsystem<URTSSelectionSubsystem>())
			{
				Subsystem->OnSelectionChanged.AddUniqueDynamic(this, &URTSUnitPanelWidget::OnSelectionUpdated);
				Subsystem->OnControlGroupsChanged.AddUniqueDynamic(this, &URTSUnitPanelWidget::OnControlGroupsUpdated);
				CommandProgressChangedHandle =
					Subsystem->OnCommandProgressChanged.AddUObject(
						this,
						&URTSUnitPanelWidget::OnCommandProgressChanged);
				OnControlGroupsUpdated(Subsystem->GetControlGroupsView());
				if (Subsystem->HasSelectedActors() || Subsystem->HasSelectedMass())
				{
					Subsystem->RequestSelectionRefresh();
				}
			}
		}
	}
}

void URTSUnitPanelWidget::NativeDestruct()
{
	HideGridSlots();

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* Subsystem = LP->GetSubsystem<URTSSelectionSubsystem>())
			{
				Subsystem->OnSelectionChanged.RemoveDynamic(this, &URTSUnitPanelWidget::OnSelectionUpdated);
				Subsystem->OnControlGroupsChanged.RemoveDynamic(this, &URTSUnitPanelWidget::OnControlGroupsUpdated);
				if (CommandProgressChangedHandle.IsValid())
				{
					Subsystem->OnCommandProgressChanged.Remove(
						CommandProgressChangedHandle);
					CommandProgressChangedHandle.Reset();
				}
			}
		}
	}

	Super::NativeDestruct();
}

void URTSUnitPanelWidget::OnSelectionUpdated(const FRTSSelectionView& View)
{
	DisplayedProgressProvider = View.Mode == ERTSSelectionMode::Single
		? View.SingleUnit.ActorPtr
		: nullptr;
	RefreshGrid(View);
}

void URTSUnitPanelWidget::OnCommandProgressChanged(
	AActor* ProgressProvider)
{
	if (!ProgressProvider
		|| DisplayedProgressProvider.Get() != ProgressProvider
		|| !ProgressProvider->Implements<URTSCommandProgressProvider>())
	{
		return;
	}

	TArray<FRTSTimedCommandInstance> ProgressItems;
	IRTSCommandProgressProvider::Execute_GetCommandProgressItems(
		ProgressProvider,
		ProgressItems);
	const bool bHasProgressItems = !ProgressItems.IsEmpty();
	const FRTSTimedCommandInstance* ActiveItem =
		ProgressItems.FindByPredicate(
			[](const FRTSTimedCommandInstance& Item)
			{
				return Item.State != ERTSTimedCommandState::Queued;
			});

	// Update only the activity fields. Health, portrait and the rest of the single
	// selection view remain untouched, so an activity change never rebuilds the
	// whole panel.
	DisplayedSingleUnitData.ActorPtr = ProgressProvider;
	DisplayedSingleUnitData.CommandProgressItems = ProgressItems;
	DisplayedSingleUnitData.bHasActivity = ActiveItem != nullptr;
	DisplayedSingleUnitData.ActivityLabel =
		ActiveItem
			? (ActiveItem->CommandButton
				? ActiveItem->CommandButton->DisplayName
				: (!ActiveItem->DisplayName.IsEmpty()
					? ActiveItem->DisplayName
					: FText::FromName(ActiveItem->PayloadId)))
			: FText::GetEmpty();
	DisplayedSingleUnitData.ActivityProgress =
		ActiveItem ? ActiveItem->GetProgress01() : 0.0f;
	DisplayedSingleUnitData.ActivityRemainingSeconds =
		ActiveItem ? ActiveItem->GetRemainingSeconds() : 0.0f;
	DisplayedSingleUnitData.ActivityQueueCount = ProgressItems.Num();
	RefreshSingleUnitActivity(DisplayedSingleUnitData);

	if (bHasProgressItems)
	{
		FRTSUnitData ProgressData;
		ProgressData.ActorPtr = ProgressProvider;
		ProgressData.CommandProgressItems = MoveTemp(ProgressItems);
		ShowCommandProgressItems(ProgressData);
	}
	else
	{
		HideGridSlots();
	}

	if (IconContainer)
	{
		IconContainer->SetVisibility(
			bHasProgressItems
				? ESlateVisibility::Visible
				: ESlateVisibility::Collapsed);
	}
	if (UnitRosterPane)
	{
		UnitRosterPane->SetVisibility(
			bHasProgressItems
				? ESlateVisibility::SelfHitTestInvisible
				: ESlateVisibility::Collapsed);
	}
	if (UnitPanelBodyGap)
	{
		UnitPanelBodyGap->SetVisibility(
			bHasProgressItems
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
	}
}

void URTSUnitPanelWidget::OnControlGroupsUpdated(const FRTSControlGroupsView& View)
{
	int32 AssignedGroupCount = 0;
	for (const FRTSControlGroupView& Group : View.Groups)
	{
		AssignedGroupCount += Group.bAssigned ? 1 : 0;
	}

	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const int32 Columns = Settings ? FMath::Max(1, Settings->SelectionGridColumns) : 8;
	const float CardHeight = Settings ? FMath::Max(1, Settings->FormationListSlotHeight) : 64.0f;
	const float RowGap = Settings ? FMath::Max(0.0f, Settings->FormationListSlotGap) : 4.0f;
	const float MinimumVisibleHeaderHeight = Settings ? FMath::Max(0.0f, Settings->SelectionPanelHeaderHeight) : 68.0f;
	const int32 VisibleRows = AssignedGroupCount > 0 ? FMath::DivideAndRoundUp(AssignedGroupCount, Columns) : 0;

	PanelHeaderHeight = VisibleRows > 0
		? FMath::Max(MinimumVisibleHeaderHeight, VisibleRows * (CardHeight + RowGap))
		: 0.0f;
	ApplyFixedPanelBounds();
	InvalidateLayoutAndVolatility();
}

void URTSUnitPanelWidget::RefreshGrid(const FRTSSelectionView& View)
{
	const TArray<FRTSUnitData>& AllItems = View.Items;
	
	UE_LOG(LogTemp, Log, TEXT("RTSUnitPanelWidget::RefreshGrid - Mode: %d, Items: %d, ActiveKey: %s"), (int32)View.Mode, AllItems.Num(), *View.ActiveGroupKey);

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (View.Mode == ERTSSelectionMode::Empty)
	{
		ShowEmptyContent();
		return;
	}

	if (View.Mode == ERTSSelectionMode::Single)
	{
		ShowSingleContent(View.SingleUnit);
		return;
	}

	if (AllItems.Num() == 0)
	{
		ShowEmptyContent();
		return;
	}

	ShowGridContent(View);
}

void URTSUnitPanelWidget::ShowEmptyContent()
{
	HideGridSlots();
	DisplayedSingleUnitData = FRTSUnitData();

	if (IconContainer)
	{
		IconContainer->SetVisibility(ESlateVisibility::Hidden);
	}

	if (UnitRosterPane)
	{
		UnitRosterPane->SetVisibility(ESlateVisibility::Hidden);
	}

	if (UnitPanelBodyGap)
	{
		UnitPanelBodyGap->SetVisibility(ESlateVisibility::Hidden);
	}

	if (UnitDetailPane)
	{
		UnitDetailPane->SetVisibility(ESlateVisibility::Hidden);
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void URTSUnitPanelWidget::ShowSingleContent(const FRTSUnitData& Data)
{
	DisplayedSingleUnitData = Data;
	const bool bHasProgressItems = !Data.CommandProgressItems.IsEmpty();
	for (URTSUnitIconWidget* IconSlot : IconSlots)
	{
		if (IconSlot)
		{
			IconSlot->SetVisibility(ESlateVisibility::Hidden);
		}
	}
	for (UTextBlock* CountSlot : CountSlots)
	{
		if (CountSlot)
		{
			CountSlot->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (!bHasProgressItems)
	{
		for (URTSCommandButtonWidget* ProgressButton : ProgressButtonSlots)
		{
			if (ProgressButton)
			{
				ProgressButton->SetVisibility(ESlateVisibility::Hidden);
			}
		}
	}

	if (IconContainer)
	{
		IconContainer->SetVisibility(bHasProgressItems
			? ESlateVisibility::Visible
			: ESlateVisibility::Collapsed);
	}

	if (UnitRosterPane)
	{
		UnitRosterPane->SetVisibility(bHasProgressItems
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);
	}

	if (UnitPanelBodyGap)
	{
		UnitPanelBodyGap->SetVisibility(bHasProgressItems
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}

	if (UnitDetailPane)
	{
		UnitDetailPane->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	RefreshSingleUnitDetail(Data);
	if (bHasProgressItems)
	{
		ShowCommandProgressItems(Data);
	}
	else
	{
		HideGridSlots();
	}
}

void URTSUnitPanelWidget::ShowCommandProgressItems(const FRTSUnitData& OwnerData)
{
	if (!IconContainer)
	{
		return;
	}

	TArray<UUserWidget*> CommanderGridWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
		this,
		CommanderGridWidgets,
		URTSCommanderGridWidget::StaticClass(),
		false);
	TSubclassOf<URTSCommandButtonWidget> ResolvedButtonClass =
		CommandButtonWidgetClass;
	if (!ResolvedButtonClass)
	{
		for (UUserWidget* CandidateWidget : CommanderGridWidgets)
		{
			if (const URTSCommanderGridWidget* CommanderGrid =
				Cast<URTSCommanderGridWidget>(CandidateWidget))
			{
				ResolvedButtonClass =
					CommanderGrid->GetCommandButtonWidgetClass();
				if (ResolvedButtonClass)
				{
					break;
				}
			}
		}
	}
	if (!ResolvedButtonClass)
	{
		ResolvedButtonClass = LoadClass<URTSCommandButtonWidget>(
			nullptr,
			TEXT("/Game/UI/HeadUpDisplay/ControlGird/CommandButton.CommandButton_C"));
	}

	TArray<URTSCommandButtonWidget*> UpdatedProgressButtons;
	const int32 VisibleCount = FMath::Min(
		OwnerData.CommandProgressItems.Num(),
		ItemsPerPage);
	for (int32 Index = 0; Index < VisibleCount; ++Index)
	{
		const FRTSTimedCommandInstance& ProgressItem =
			OwnerData.CommandProgressItems[Index];
		const FName ProgressItemId = ProgressItem.InstanceId.IsValid()
			? FName(*ProgressItem.InstanceId.ToString(EGuidFormats::Digits))
			: NAME_None;
		URTSCommandButtonWidget* ButtonWidget = nullptr;
		for (URTSCommandButtonWidget* ExistingButton : ProgressButtonSlots)
		{
			if (ExistingButton
				&& ExistingButton->GetProgressItemId() == ProgressItemId)
			{
				ButtonWidget = ExistingButton;
				break;
			}
		}

		if (!ButtonWidget)
		{
			ButtonWidget = ResolvedButtonClass
				? CreateWidget<URTSCommandButtonWidget>(
					this, ResolvedButtonClass)
				: nullptr;
		}

		if (!ButtonWidget)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("RTSUnitPanelWidget: could not create activity button for progress item %s."),
				*ProgressItemId.ToString());
			continue;
		}

		if (ButtonWidget->GetParent() != IconContainer)
		{
			ButtonWidget->RemoveFromParent();
			if (UUniformGridPanel* UniformGrid =
				Cast<UUniformGridPanel>(IconContainer))
			{
				UniformGrid->AddChildToUniformGrid(ButtonWidget, 0, 0);
			}
			else if (UGridPanel* GenericGrid =
				Cast<UGridPanel>(IconContainer))
			{
				GenericGrid->AddChildToGrid(ButtonWidget, 0, 0);
			}
			else if (UWrapBox* WrapBox = Cast<UWrapBox>(IconContainer))
			{
				WrapBox->AddChildToWrapBox(ButtonWidget);
			}
			else
			{
				IconContainer->AddChild(ButtonWidget);
			}
		}

		const int32 Row = MaxColumns > 0 ? Index / MaxColumns : 0;
		const int32 Column = MaxColumns > 0 ? Index % MaxColumns : 0;
		if (UUniformGridSlot* UniformSlot =
			Cast<UUniformGridSlot>(ButtonWidget->Slot))
		{
			UniformSlot->SetRow(Row);
			UniformSlot->SetColumn(Column);
			UniformSlot->SetHorizontalAlignment(HAlign_Fill);
			UniformSlot->SetVerticalAlignment(VAlign_Fill);
		}
		else if (UGridSlot* GridSlot = Cast<UGridSlot>(ButtonWidget->Slot))
		{
			GridSlot->SetRow(Row);
			GridSlot->SetColumn(Column);
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
		}

		ButtonWidget->InitProgressItem(
			ProgressItem,
			OwnerData.ActorPtr);
		ButtonWidget->SetRenderOpacity(
			ProgressItem.State == ERTSTimedCommandState::Paused
				? 0.65f
				: 1.0f);
		ButtonWidget->SetVisibility(ESlateVisibility::Visible);
		UpdatedProgressButtons.Add(ButtonWidget);
	}

	for (URTSCommandButtonWidget* PreviousButton : ProgressButtonSlots)
	{
		if (!PreviousButton
			|| UpdatedProgressButtons.Contains(PreviousButton))
		{
			continue;
		}

		PreviousButton->RemoveFromParent();
		PreviousButton->SetVisibility(ESlateVisibility::Hidden);
	}

	ProgressButtonSlots = MoveTemp(UpdatedProgressButtons);
}

void URTSUnitPanelWidget::ShowGridContent(const FRTSSelectionView& View)
{
	HideGridSlots();
	DisplayedSingleUnitData = FRTSUnitData();
	const TArray<FRTSUnitData>& AllItems = View.Items;
	const ERTSSelectionMode Mode = View.Mode;
	const FString ActiveKey = View.ActiveGroupKey;

	if (IconContainer)
	{
		IconContainer->SetVisibility(ESlateVisibility::Visible);
	}

	if (UnitRosterPane)
	{
		UnitRosterPane->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	if (UnitPanelBodyGap)
	{
		UnitPanelBodyGap->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (UnitDetailPane)
	{
		UnitDetailPane->SetVisibility(ESlateVisibility::Collapsed);
	}

	// --- 3. Update Grid from Pool ---
	if (!IconContainer || IconSlots.Num() == 0)
	{
		// UE_LOG(LogTemp, Warning, TEXT("RTSUnitPanelWidget: internal pool empty or container missing."));
		return;
	}

	auto SetGridCell = [this](UWidget* Widget, int32 LinearIndex)
	{
		if (!Widget)
		{
			return;
		}

		const int32 Row = MaxColumns > 0 ? LinearIndex / MaxColumns : 0;
		const int32 Column = MaxColumns > 0 ? LinearIndex % MaxColumns : 0;

		if (UUniformGridSlot* UniformSlot = Cast<UUniformGridSlot>(Widget->Slot))
		{
			UniformSlot->SetRow(Row);
			UniformSlot->SetColumn(Column);
			UniformSlot->SetHorizontalAlignment(HAlign_Fill);
			UniformSlot->SetVerticalAlignment(VAlign_Fill);
		}
		else if (UGridSlot* GridSlot = Cast<UGridSlot>(Widget->Slot))
		{
			GridSlot->SetRow(Row);
			GridSlot->SetColumn(Column);
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
		}
	};

	int32 StartIndex = 0;
	const bool bIsSummaryMode = Mode == ERTSSelectionMode::Summary;

	for (UTextBlock* CountSlot : CountSlots)
	{
		if (CountSlot)
		{
			CountSlot->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (bIsSummaryMode)
	{
		for (URTSUnitIconWidget* SlotWidget : IconSlots)
		{
			if (SlotWidget)
			{
				SlotWidget->SetVisibility(ESlateVisibility::Hidden);
			}
		}

		const int32 SummaryCapacity = FMath::Min(
			FMath::Min(IconSlots.Num(), CountSlots.Num()),
			ItemsPerPage / 2);
		const int32 MaxVisibleSummaries = FMath::Min(AllItems.Num(), SummaryCapacity);

		for (int32 i = 0; i < MaxVisibleSummaries; ++i)
		{
			const int32 DataIndex = StartIndex + i;
			const FRTSUnitData& Data = AllItems[DataIndex];

			URTSUnitIconWidget* SlotWidget = IconSlots[i];
			UTextBlock* CountSlot = CountSlots[i];

			if (SlotWidget)
			{
				SetGridCell(SlotWidget, i * 2);
				SlotWidget->InitData(Data, true, false, false, IconSlotSize);

				const bool bIsActive = ActiveKey.IsEmpty() || (GetSelectionWidgetUnitGroupKey(Data) == ActiveKey);
				SlotWidget->SetIsActive(bIsActive);
				SlotWidget->SetVisibility(ESlateVisibility::Visible);
			}

			if (CountSlot)
			{
				SetGridCell(CountSlot, i * 2 + 1);
				CountSlot->SetText(FText::FromString(FString::Printf(TEXT("x%d"), Data.Count)));
				CountSlot->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
		}

		return;
	}
	
	for (int32 i = 0; i < IconSlots.Num(); i++)
	{
		URTSUnitIconWidget* SlotWidget = IconSlots[i];
		if (!SlotWidget) continue;

		int32 DataIndex = StartIndex + i; // Simple linear mapping

		if (DataIndex < AllItems.Num())
		{
			// Valid Item
			const FRTSUnitData& Data = AllItems[DataIndex];
			
			SetGridCell(SlotWidget, i);

			// Update Data
			SlotWidget->InitData(Data, true, true, true, IconSlotSize);
			
			// Highlight Logic
			bool bIsActive = ActiveKey.IsEmpty() || (GetSelectionWidgetUnitGroupKey(Data) == ActiveKey);
			SlotWidget->SetIsActive(bIsActive);

			// Visible
			SlotWidget->SetVisibility(ESlateVisibility::Visible); // or SelfHitTestInvisible
		}
		else
		{
			// Empty Slot
			SlotWidget->SetVisibility(ESlateVisibility::Hidden); // Hidden = Layout Reserved. Collapsed = Gone.
		}
	}

}

void URTSUnitPanelWidget::HideGridSlots()
{
	TArray<URTSCommandButtonWidget*> ButtonsToRestore =
		MoveTemp(ProgressButtonSlots);
	ProgressButtonSlots.Reset();
	for (URTSCommandButtonWidget* ProgressButton : ButtonsToRestore)
	{
		if (!ProgressButton)
		{
			continue;
		}

		ProgressButton->RemoveFromParent();
		ProgressButton->SetVisibility(ESlateVisibility::Hidden);
	}

	for (URTSUnitIconWidget* SlotWidget : IconSlots)
	{
		if (SlotWidget)
		{
			SlotWidget->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	for (UTextBlock* CountSlot : CountSlots)
	{
		if (CountSlot)
		{
			CountSlot->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void URTSUnitPanelWidget::RefreshSingleUnitDetail(const FRTSUnitData& Data)
{
	UWidget* DetailRoot = UnitDetailPane ? UnitDetailPane : Cast<UWidget>(this);
	if (!DetailRoot)
	{
		return;
	}

	if (UImage* UnitIconImage = Cast<UImage>(FindDescendantWidgetByName(DetailRoot, TEXT("UnitIconImage"))))
	{
		UTexture2D* DetailTexture = Data.Portrait ? Data.Portrait : Data.Icon;
		if (DetailTexture)
		{
			UnitIconImage->SetBrushFromTexture(DetailTexture);
			UnitIconImage->SetColorAndOpacity(FLinearColor::White);
			UnitIconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			UnitIconImage->SetColorAndOpacity(FLinearColor::Transparent);
			UnitIconImage->SetVisibility(ESlateVisibility::Hidden);
		}
		UnitIconImage->SetDesiredSizeOverride(FVector2D(IconSlotSize, IconSlotSize));
	}

	if (UTextBlock* NameText = Cast<UTextBlock>(FindDescendantWidgetByName(DetailRoot, TEXT("UnitNameText"))))
	{
		NameText->SetText(FText::FromString(Data.Name));
		NameText->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 1.0f, 0.91f, 1.0f)));
		NameText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
		NameText->SetShadowOffset(FVector2D(1.0f, 1.0f));
		NameText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	auto SetOptionalDetailText = [this, DetailRoot](FName WidgetName, const FString& Value)
	{
		if (UTextBlock* TextBlock = Cast<UTextBlock>(FindDescendantWidgetByName(DetailRoot, WidgetName)))
		{
			TextBlock->SetText(FText::FromString(Value));
			TextBlock->SetVisibility(Value.TrimStartAndEnd().IsEmpty()
				? ESlateVisibility::Collapsed
				: ESlateVisibility::HitTestInvisible);
		}
	};

	SetOptionalDetailText(TEXT("UnitRoleText"), Data.Role);
	SetOptionalDetailText(TEXT("RoleText"), Data.Role);
	SetOptionalDetailText(TEXT("RoleValue"), Data.Role);
	SetOptionalDetailText(TEXT("UnitTypeText"), Data.TypeKey);
	SetOptionalDetailText(TEXT("TypeText"), Data.TypeKey);
	SetOptionalDetailText(TEXT("TypeValue"), Data.TypeKey);
	SetOptionalDetailText(TEXT("AnnouncerText"), Data.AnnouncerId.ToString());
	SetOptionalDetailText(TEXT("AnnouncerValue"), Data.AnnouncerId.ToString());

	RefreshSingleUnitActivity(Data);

	auto UpdateProgressBar = [](UProgressBar* Bar, float Current, float Max)
	{
		if (!Bar)
		{
			return;
		}

		if (Max > 0.0f)
		{
			Bar->SetPercent(FMath::Clamp(Current / Max, 0.0f, 1.0f));
			Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			Bar->SetVisibility(ESlateVisibility::Collapsed);
		}
	};

	UpdateProgressBar(Cast<UProgressBar>(FindDescendantWidgetByName(DetailRoot, TEXT("HealthBar"))), Data.Health, Data.MaxHealth);
	UpdateProgressBar(Cast<UProgressBar>(FindDescendantWidgetByName(DetailRoot, TEXT("EnergyBar"))), Data.Energy, Data.MaxEnergy);
	UpdateProgressBar(Cast<UProgressBar>(FindDescendantWidgetByName(DetailRoot, TEXT("ShieldBar"))), Data.Shield, Data.MaxShield);
}

void URTSUnitPanelWidget::RefreshSingleUnitActivity(const FRTSUnitData& Data)
{
	UWidget* DetailRoot = UnitDetailPane ? UnitDetailPane : Cast<UWidget>(this);
	if (!DetailRoot)
	{
		return;
	}

	FString ActivityText;
	if (Data.bHasProductionCapacity)
	{
		ActivityText = FString::Printf(TEXT("产能 %d/%d"),
			Data.ProductionBusyLanes,
			Data.ProductionTotalLanes);
		if (Data.ProductionQueuedOrders > 0)
		{
			ActivityText += FString::Printf(TEXT("  等待 %d"), Data.ProductionQueuedOrders);
		}
	}
	if (Data.bHasActivity)
	{
		if (!ActivityText.IsEmpty())
		{
			ActivityText += TEXT("  ·  ");
		}
		ActivityText += Data.ActivityLabel.ToString();
		if (Data.ActivityRemainingSeconds > 0.0f)
		{
			ActivityText += FString::Printf(TEXT("  %.1fs"), Data.ActivityRemainingSeconds);
		}
		if (Data.ActivityQueueCount > 1)
		{
			ActivityText += FString::Printf(TEXT("  队列 %d"), Data.ActivityQueueCount);
		}
	}
	if (UTextBlock* TextBlock = Cast<UTextBlock>(
		FindDescendantWidgetByName(DetailRoot, TEXT("ActivityText"))))
	{
		TextBlock->SetText(FText::FromString(ActivityText));
		TextBlock->SetVisibility(
			ActivityText.TrimStartAndEnd().IsEmpty()
				? ESlateVisibility::Collapsed
				: ESlateVisibility::HitTestInvisible);
	}
	if (UProgressBar* ActivityBar = Cast<UProgressBar>(FindDescendantWidgetByName(DetailRoot, TEXT("ActivityBar"))))
	{
		ActivityBar->SetIsMarquee(false);
		ActivityBar->SetPercent(FMath::Clamp(Data.ActivityProgress, 0.0f, 1.0f));
		ActivityBar->SetVisibility(Data.bHasActivity
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
}

UWidget* URTSUnitPanelWidget::FindDescendantWidgetByName(UWidget* RootWidget, FName WidgetName) const
{
	if (!RootWidget)
	{
		return nullptr;
	}

	if (RootWidget->GetFName() == WidgetName)
	{
		return RootWidget;
	}

	if (UUserWidget* UserWidget = Cast<UUserWidget>(RootWidget))
	{
		if (UserWidget->WidgetTree)
		{
			TArray<UWidget*> Widgets;
			UserWidget->WidgetTree->GetAllWidgets(Widgets);
			for (UWidget* Widget : Widgets)
			{
				if (Widget && Widget->GetFName() == WidgetName)
				{
					return Widget;
				}
			}
		}
	}

	if (UPanelWidget* Panel = Cast<UPanelWidget>(RootWidget))
	{
		const int32 ChildrenCount = Panel->GetChildrenCount();
		for (int32 Index = 0; Index < ChildrenCount; ++Index)
		{
			if (UWidget* FoundWidget = FindDescendantWidgetByName(Panel->GetChildAt(Index), WidgetName))
			{
				return FoundWidget;
			}
		}
	}

	return nullptr;
}
