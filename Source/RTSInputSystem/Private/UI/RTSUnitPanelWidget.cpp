#include "UI/RTSUnitPanelWidget.h"
#include "UI/RTSCommandButtonWidget.h"
#include "UI/RTSCommanderGridWidget.h"
#include "UI/RTSFormationListWidget.h"
#include "UI/RTSUnitIconWidget.h"
#include "RTSInputPanelSettings.h"
#include "RTSSelectionSubsystem.h"
#include "Engine/World.h"
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
#include "Components/Overlay.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
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
		MaxRows = FMath::Max(3, Settings->SelectionGridRows);
		MaxColumns = FMath::Max(8, Settings->SelectionGridColumns);
		IconSlotSize = FMath::Max(1, Settings->SelectionIconSize);
		PanelHeaderHeight = FMath::Max(Settings->SelectionPanelHeaderHeight,
			Settings->FormationListSlotHeight + Settings->FormationListSlotGap);
		PanelContentPadding = Settings->SelectionPanelContentPadding;
	}

	// Reuse the command card's complete button footprint, including its border.
	const UClass* GridClass = LoadClass<URTSCommanderGridWidget>(nullptr,
		TEXT("/Game/UI/HeadUpDisplay/ControlGird/ControlGrid.ControlGrid_C"));
	const URTSCommanderGridWidget* GridDefaults = GridClass
		? GridClass->GetDefaultObject<URTSCommanderGridWidget>()
		: GetDefault<URTSCommanderGridWidget>();
	SelectionButtonSize = GridDefaults->GetButtonSize();
	SelectionSlotPadding = GridDefaults->GetSlotPadding();
	const FVector2D CellSize = GetSelectionCellSize();

	if (UnitPanelFrame)
	{
		UnitPanelFrame->SetPadding(PanelContentPadding);
	}

	if (UnitPanelRouteBounds)
	{
		UnitPanelRouteBounds->SetWidthOverride(MaxColumns * CellSize.X);
		UnitPanelRouteBounds->SetHeightOverride(MaxRows * CellSize.Y);
		UnitPanelRouteBounds->SetClipping(EWidgetClipping::ClipToBounds);
	}
	if (UnitPanelHeaderBounds)
	{
		UnitPanelHeaderBounds->SetWidthOverride(MaxColumns * CellSize.X);
		UnitPanelHeaderBounds->SetHeightOverride(PanelHeaderHeight);
	}

	ApplyFixedPanelSlotLayout();
	ApplyFixedPanelBounds();
}

void URTSUnitPanelWidget::ApplyFixedPanelSlotLayout()
{
	const FVector2D Cell = GetSelectionCellSize();
	const float Width = MaxColumns * Cell.X;
	const float MiddleHeight = 2.0f * Cell.Y;
	const float CaptionHeight = (MaxRows * Cell.Y - MiddleHeight) * 0.5f;
	auto Box = [this](const TCHAR* Name, float X, float Y)
	{
		if (USizeBox* Size = Cast<USizeBox>(FindDescendantWidgetByName(this, Name)))
		{
			Size->SetWidthOverride(X); Size->SetHeightOverride(Y);
			Size->SetClipping(EWidgetClipping::ClipToBounds);
			if (UVerticalBoxSlot* LayoutSlot = Cast<UVerticalBoxSlot>(Size->Slot))
			{ LayoutSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); LayoutSlot->SetPadding(FMargin(0)); }
			if (UHorizontalBoxSlot* LayoutSlot = Cast<UHorizontalBoxSlot>(Size->Slot))
			{ LayoutSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); LayoutSlot->SetPadding(FMargin(0)); }
		}
	};
	Box(TEXT("UnitTitleBounds"), Width, CaptionHeight);
	Box(TEXT("UnitClassificationBounds"), Width, CaptionHeight);
	Box(TEXT("UnitMiddleBounds"), Width, MiddleHeight);
	Box(TEXT("UnitIdentityBounds"), 2 * Cell.X, MiddleHeight);
	Box(TEXT("UnitEquipmentBounds"), Width - 2 * Cell.X, MiddleHeight);
	Box(TEXT("WeaponCardBounds"), (Width - 2 * Cell.X) * 0.5f, MiddleHeight);
	Box(TEXT("ArmorCardBounds"), (Width - 2 * Cell.X) * 0.5f, MiddleHeight);
	Box(TEXT("ActiveProductionBounds0"), 3 * Cell.X, Cell.Y);
	Box(TEXT("ActiveProductionBounds1"), 3 * Cell.X, Cell.Y);
	Box(TEXT("ActiveIconHost0"), SelectionButtonSize.X, SelectionButtonSize.Y);
	Box(TEXT("ActiveIconHost1"), SelectionButtonSize.X, SelectionButtonSize.Y);
	// Portrait and vitals share one vertical column inside the fixed two-cell identity area.
	const float PortraitWidth = FMath::Min(2.0f * IconSlotSize, 2 * Cell.X - SelectionSlotPadding.GetTotalSpaceAlong<Orient_Horizontal>());
	if (UWidget* Identity = FindDescendantWidgetByName(this, TEXT("UnitIdentityContent")))
		if (USizeBoxSlot* IdentitySlot = Cast<USizeBoxSlot>(Identity->Slot))
		{ IdentitySlot->SetPadding(FMargin(0)); IdentitySlot->SetHorizontalAlignment(HAlign_Center); IdentitySlot->SetVerticalAlignment(VAlign_Fill); }
	if (USizeBox* Portrait = Cast<USizeBox>(FindDescendantWidgetByName(this, TEXT("UnitIconContainer"))))
	{
		Portrait->SetWidthOverride(PortraitWidth);
		Portrait->ClearHeightOverride();
		if (UVerticalBoxSlot* PortraitSlot = Cast<UVerticalBoxSlot>(Portrait->Slot))
		{ PortraitSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); PortraitSlot->SetPadding(FMargin(0)); }
	}
	if (UScaleBox* PortraitScale = Cast<UScaleBox>(FindDescendantWidgetByName(this, TEXT("UnitPortraitScale"))))
		PortraitScale->SetStretch(EStretch::ScaleToFit);
	if (UPanelWidget* Vitals = Cast<UPanelWidget>(FindDescendantWidgetByName(this, TEXT("InfoVerticalBox"))))
	{
		if (UVerticalBoxSlot* VitalsSlot = Cast<UVerticalBoxSlot>(Vitals->Slot))
		{ VitalsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); VitalsSlot->SetPadding(FMargin(0, 4, 0, 0)); }
		for (UWidget* Vital : Vitals->GetAllChildren())
		{
			if (UVerticalBoxSlot* VitalSlot = Cast<UVerticalBoxSlot>(Vital->Slot))
			{ VitalSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); VitalSlot->SetPadding(FMargin(0)); VitalSlot->SetHorizontalAlignment(HAlign_Fill); }
			if (UTextBlock* Text = Cast<UTextBlock>(Vital))
			{
				Text->SetJustification(ETextJustify::Center);
				Text->SetAutoWrapText(false);
				FSlateFontInfo Font = Text->GetFont(); Font.Size = Vital->GetFName() == TEXT("HealthValueText") ? 18 : 14; Text->SetFont(Font);
			}
			if (UProgressBar* Bar = Cast<UProgressBar>(Vital))
			{
				FProgressBarStyle Style = Bar->GetWidgetStyle();
				const FVector2D BarSize(PortraitWidth, Vital->GetFName() == TEXT("HealthBar") ? 12 : 6);
				Style.BackgroundImage.ImageSize = Style.FillImage.ImageSize = Style.MarqueeImage.ImageSize = BarSize;
				Style.BackgroundImage.TintColor = FSlateColor(FLinearColor(0.07f, 0.09f, 0.08f));
				Bar->SetWidgetStyle(Style);
			}
		}
	}
	for (const TCHAR* Name : {TEXT("ActiveProductionSlot0"), TEXT("ActiveProductionSlot1"), TEXT("WeaponCard"), TEXT("ArmorCard")})
		if (UWidget* Content = FindDescendantWidgetByName(this, Name))
			if (USizeBoxSlot* ContentSlot = Cast<USizeBoxSlot>(Content->Slot))
			{
				ContentSlot->SetPadding(FMargin(0));
				ContentSlot->SetHorizontalAlignment(HAlign_Fill);
				ContentSlot->SetVerticalAlignment(VAlign_Fill);
			}
	for (const TCHAR* Name : {TEXT("WeaponStats"), TEXT("ArmorStats")})
		if (UWidget* Content = FindDescendantWidgetByName(this, Name))
			if (UVerticalBoxSlot* ContentSlot = Cast<UVerticalBoxSlot>(Content->Slot))
				ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	for (const TCHAR* Name : {TEXT("UnitRosterPane"), TEXT("WeaponArmorPanel")})
		if (UWidget* Content = FindDescendantWidgetByName(this, Name))
			if (UOverlaySlot* LayoutSlot = Cast<UOverlaySlot>(Content->Slot))
			{ LayoutSlot->SetHorizontalAlignment(HAlign_Center); LayoutSlot->SetVerticalAlignment(VAlign_Center); }
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (UWidget* Host = FindDescendantWidgetByName(this, *FString::Printf(TEXT("ActiveIconHost%d"), Index)))
			if (UHorizontalBoxSlot* LayoutSlot = Cast<UHorizontalBoxSlot>(Host->Slot))
			{ LayoutSlot->SetPadding(SelectionSlotPadding); LayoutSlot->SetVerticalAlignment(VAlign_Center); }
		if (UWidget* Info = FindDescendantWidgetByName(this, *FString::Printf(TEXT("ActiveInfo%d"), Index)))
			if (UHorizontalBoxSlot* LayoutSlot = Cast<UHorizontalBoxSlot>(Info->Slot))
			{ LayoutSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); LayoutSlot->SetPadding(SelectionSlotPadding); }
		if (UTextBlock* Status = Cast<UTextBlock>(FindDescendantWidgetByName(this, *FString::Printf(TEXT("ActiveStatus%d"), Index))))
		{ Status->SetAutoWrapText(true); Status->SetWrapTextAt(2 * Cell.X - SelectionSlotPadding.Left - SelectionSlotPadding.Right); }
	}
	for (const TCHAR* Name : {TEXT("UnitNameText"), TEXT("UnitClassificationText")})
		if (UTextBlock* Text = Cast<UTextBlock>(FindDescendantWidgetByName(this, Name)))
			Text->SetJustification(ETextJustify::Center);
	const TPair<const TCHAR*, const TCHAR*> Images[] = {
		{TEXT("WeaponImage"), TEXT("/Game/UI/HeadUpDisplay/WW2Unified/Commands/T_WW2_Commands_Attack.T_WW2_Commands_Attack")},
		{TEXT("ArmorImage"), TEXT("/Game/UI/HeadUpDisplay/WW2Unified/Markers/T_WW2_Markers_Armor.T_WW2_Markers_Armor")}};
	for (const auto& Entry : Images)
		if (UImage* Image = Cast<UImage>(FindDescendantWidgetByName(this, Entry.Key)))
		{
			Image->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr, Entry.Value));
			Image->SetDesiredSizeOverride(FVector2D(96));
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
		UnitPanelHeaderBounds = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("UnitPanelHeaderBounds"));
		UnitPanelHeaderBounds->AddChild(FormationList);
		ContentRoot->AddChildToVerticalBox(UnitPanelHeaderBounds);
		UnitPanelRouteBounds = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("UnitPanelRouteBounds"));
		ContentRoot->AddChildToVerticalBox(UnitPanelRouteBounds);
		UOverlay* Routes = WidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(), TEXT("UnitPanelRoutes"));
		UnitPanelRouteBounds->AddChild(Routes);

		// Native fallback uses the same named tree and data route as the authored widget.
		auto Add = [this](UClass* Class, const FName Name, UPanelWidget* Parent)
		{
			UWidget* Widget = WidgetTree->ConstructWidget<UWidget>(Class, Name);
			if (Parent) Parent->AddChild(Widget);
			return Widget;
		};
		auto Column = [&Add](const FName Name, UPanelWidget* Parent)
		{ return CastChecked<UVerticalBox>(Add(UVerticalBox::StaticClass(), Name, Parent)); };
		auto Row = [&Add](const FName Name, UPanelWidget* Parent)
		{ return CastChecked<UHorizontalBox>(Add(UHorizontalBox::StaticClass(), Name, Parent)); };
		auto Box = [&Add](const FName Name, UPanelWidget* Parent)
		{ return CastChecked<USizeBox>(Add(USizeBox::StaticClass(), Name, Parent)); };
		auto Text = [&Add](const FName Name, const TCHAR* Value, UPanelWidget* Parent)
		{
			UTextBlock* Widget = CastChecked<UTextBlock>(Add(UTextBlock::StaticClass(), Name, Parent));
			Widget->SetText(FText::FromString(Value));
			Widget->SetJustification(ETextJustify::Center);
			FSlateFontInfo Font = Widget->GetFont(); Font.Size = 24; Widget->SetFont(Font);
			return Widget;
		};
		auto Bar = [&Add](const FName Name, UPanelWidget* Parent)
		{
			UProgressBar* Widget = CastChecked<UProgressBar>(Add(UProgressBar::StaticClass(), Name, Parent));
			Widget->SetFillColorAndOpacity(FLinearColor(0.22f, 1.0f, 0.42f));
			return Widget;
		};
		UVerticalBox* Single = Column(TEXT("SingleUnitPanel"), Routes);
		SingleUnitPanel = Single;
		Text(TEXT("UnitNameText"), TEXT(""), Box(TEXT("UnitTitleBounds"), Single));
		UHorizontalBox* Middle = Row(TEXT("UnitPanelBody"), Box(TEXT("UnitMiddleBounds"), Single));
		UVerticalBox* Identity = Column(TEXT("UnitIdentityContent"), Box(TEXT("UnitIdentityBounds"), Middle));
		USizeBox* Portrait = Box(TEXT("UnitIconContainer"), Identity);
		UPanelWidget* PortraitScale = CastChecked<UScaleBox>(Add(UScaleBox::StaticClass(), TEXT("UnitPortraitScale"), Portrait));
		Add(UImage::StaticClass(), TEXT("UnitIconImage"), PortraitScale);
		UVerticalBox* Stats = Column(TEXT("InfoVerticalBox"), Identity);
		Bar(TEXT("HealthBar"), Stats); Text(TEXT("HealthValueText"), TEXT(""), Stats);
		Bar(TEXT("EnergyBar"), Stats); Bar(TEXT("ShieldBar"), Stats);
		Text(TEXT("ActivityText"), TEXT(""), Stats); Bar(TEXT("ActivityBar"), Stats);
		UOverlay* Equipment = CastChecked<UOverlay>(Add(UOverlay::StaticClass(), TEXT("UnitEquipmentRoutes"), Box(TEXT("UnitEquipmentBounds"), Middle)));
		UHorizontalBox* Combat = Row(TEXT("WeaponArmorPanel"), Equipment);
		WeaponArmorPanel = Combat;
		for (const FString Prefix : {FString(TEXT("Weapon")), FString(TEXT("Armor"))})
		{
			UVerticalBox* Card = Column(*(Prefix + TEXT("Card")), Box(*(Prefix + TEXT("CardBounds")), Combat));
			Text(*(Prefix + TEXT("Title")), Prefix == TEXT("Weapon") ? TEXT("武器") : TEXT("护甲"), Card);
			Add(UImage::StaticClass(), *(Prefix + TEXT("Image")), Card);
			Text(*(Prefix + TEXT("Stats")), TEXT(""), Card);
		}
		UVerticalBox* Research = Column(TEXT("UnitRosterPane"), Equipment);
		UnitRosterPane = Research;
		UHorizontalBox* ActiveRow = Row(TEXT("ActiveProductionRow"), Research);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UHorizontalBox* Active = Row(*FString::Printf(TEXT("ActiveProductionSlot%d"), Index),
				Box(*FString::Printf(TEXT("ActiveProductionBounds%d"), Index), ActiveRow));
			Box(*FString::Printf(TEXT("ActiveIconHost%d"), Index), Active);
			UVerticalBox* Info = Column(*FString::Printf(TEXT("ActiveInfo%d"), Index), Active);
			Text(*FString::Printf(TEXT("ActiveStatus%d"), Index), TEXT("正在研发"), Info);
			UProgressBar* Progress = Bar(*FString::Printf(TEXT("ActiveProgress%d"), Index), Info);
			(Index == 0 ? ActiveProgress0 : ActiveProgress1) = Progress;
		}
		ActivityQueueContainer = CastChecked<UGridPanel>(Add(UGridPanel::StaticClass(), TEXT("ActivityQueueContainer"), Research));
		Text(TEXT("UnitClassificationText"), TEXT(""), Box(TEXT("UnitClassificationBounds"), Single));
		IconContainer = CastChecked<UUniformGridPanel>(Add(UUniformGridPanel::StaticClass(), TEXT("IconContainer"), Routes));
		SummaryIconContainer = CastChecked<UUniformGridPanel>(Add(UUniformGridPanel::StaticClass(), TEXT("SummaryIconContainer"), Routes));

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

FVector2D URTSUnitPanelWidget::GetSelectionCellSize() const
{
	return SelectionButtonSize + FVector2D(
		SelectionSlotPadding.Left + SelectionSlotPadding.Right,
		SelectionSlotPadding.Top + SelectionSlotPadding.Bottom);
}

FVector2D URTSUnitPanelWidget::CalculateFixedPanelSize() const
{
	const int32 Rows = FMath::Max(3, MaxRows);
	const int32 Columns = FMath::Max(8, MaxColumns);
	const FVector2D CellSize = GetSelectionCellSize();
	const float HeaderHeight = FMath::Max(0.0f, PanelHeaderHeight);
	FMargin PanelContentPadding(16.0f, 4.0f, 16.0f, 4.0f);

	if (const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>())
	{
		PanelContentPadding = Settings->SelectionPanelContentPadding;
	}

	const float Width = static_cast<float>(Columns * CellSize.X) + PanelContentPadding.Left + PanelContentPadding.Right;
	const float Height = HeaderHeight + static_cast<float>(Rows * CellSize.Y) + PanelContentPadding.Top + PanelContentPadding.Bottom;
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

void URTSUnitPanelWidget::BuildSelectionGrid(
	UPanelWidget* Container, TArray<URTSUnitIconWidget*>& Slots, bool bSummary)
{
	Slots.Reset();
	if (!Container || !WidgetTree || !IconWidgetClass
		|| !IconWidgetClass->IsChildOf(URTSUnitIconWidget::StaticClass())) return;
	Container->ClearChildren();
	UGridPanel* Grid = Cast<UGridPanel>(Container);
	UUniformGridPanel* Uniform = Cast<UUniformGridPanel>(Container);
	if (!Grid && !Uniform)
	{
		UE_LOG(LogTemp, Error, TEXT("RTS selection requires a GridPanel or UniformGridPanel: %s"), *Container->GetName());
		return;
	}
	if (Grid)
	{
		Grid->RowFill.Init(1.0f, MaxRows);
		Grid->ColumnFill.Init(1.0f, MaxColumns);
	}
	if (Uniform)
	{
		Uniform->SetSlotPadding(SelectionSlotPadding);
		Uniform->SetMinDesiredSlotWidth(SelectionButtonSize.X);
		Uniform->SetMinDesiredSlotHeight(SelectionButtonSize.Y);
	}
	for (int32 CellIndex = 0; CellIndex < ItemsPerPage; ++CellIndex)
	{
		const int32 Row = CellIndex / MaxColumns;
		const int32 Column = CellIndex % MaxColumns;
		USizeBox* Cell = WidgetTree->ConstructWidget<USizeBox>();
		Cell->SetWidthOverride(SelectionButtonSize.X);
		Cell->SetHeightOverride(SelectionButtonSize.Y);
		Cell->SetClipping(EWidgetClipping::ClipToBounds);
		if (Grid)
		{
			UGridSlot* CellSlot = Grid->AddChildToGrid(Cell, Row, Column);
			CellSlot->SetPadding(SelectionSlotPadding);
			CellSlot->SetHorizontalAlignment(HAlign_Fill);
			CellSlot->SetVerticalAlignment(VAlign_Fill);
		}
		else
		{
			UUniformGridSlot* CellSlot = Uniform->AddChildToUniformGrid(Cell, Row, Column);
			CellSlot->SetHorizontalAlignment(HAlign_Fill);
			CellSlot->SetVerticalAlignment(VAlign_Fill);
		}
		// Odd column counts leave the last cell empty so icon/count pairs never wrap.
		if (bSummary && Column == MaxColumns - 1 && MaxColumns % 2 != 0) continue;
		if (bSummary && Column % 2 != 0)
		{
			UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>();
			Scale->SetStretch(EStretch::ScaleToFit);
			Scale->SetStretchDirection(EStretchDirection::DownOnly);
			Cell->AddChild(Scale);
			UTextBlock* Count = WidgetTree->ConstructWidget<UTextBlock>();
			Count->SetJustification(ETextJustify::Center);
			Count->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 1.0f, 0.72f, 1.0f)));
			Count->SetShadowColorAndOpacity(FLinearColor::Black);
			Count->SetShadowOffset(FVector2D(1.0f, 1.0f));
			FSlateFontInfo Font = Count->GetFont();
			Font.Size = FMath::Max(22, IconSlotSize / 2);
			Count->SetFont(Font);
			Count->SetVisibility(ESlateVisibility::Hidden);
			Scale->AddChild(Count);
			CountSlots.Add(Count);
		}
		else
		{
			URTSUnitIconWidget* Icon = CreateWidget<URTSUnitIconWidget>(this, IconWidgetClass);
			Cell->AddChild(Icon);
			Icon->SetVisibility(ESlateVisibility::Hidden);
			Slots.Add(Icon);
		}
	}
}

void URTSUnitPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ApplySelectionPanelLayoutSettings();

	// Designer children provide presentation templates, never grid dimensions.
	if (IconContainer && !IconWidgetClass)
	{
		for (UWidget* Child : IconContainer->GetAllChildren())
		{
			if (URTSUnitIconWidget* Template = Cast<URTSUnitIconWidget>(Child))
			{
				IconWidgetClass = Template->GetClass();
				break;
			}
		}
	}
	if (!IconWidgetClass) IconWidgetClass = UnitIconClass;
	if (!IconWidgetClass)
		IconWidgetClass = LoadClass<URTSUnitIconWidget>(nullptr,
			TEXT("/Game/UI/HeadUpDisplay/UnitDetails/Unit.Unit_C"));

	ItemsPerPage = MaxRows * MaxColumns;
	HideGridSlots();
	CountSlots.Reset();
	BuildSelectionGrid(IconContainer, IconSlots, false);
	BuildSelectionGrid(SummaryIconContainer, SummaryIconSlots, true);

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

void URTSUnitPanelWidget::OnCommandProgressChanged(AActor* ProgressProvider)
{
	if (!ProgressProvider || DisplayedProgressProvider.Get() != ProgressProvider
		|| !ProgressProvider->Implements<URTSCommandProgressProvider>()) return;
	IRTSCommandProgressProvider::Execute_GetCommandProgressItems(
		ProgressProvider, DisplayedSingleUnitData.CommandProgressItems);
	const FRTSTimedCommandInstance* Active = DisplayedSingleUnitData.CommandProgressItems.FindByPredicate(
		[](const FRTSTimedCommandInstance& Item) { return Item.State != ERTSTimedCommandState::Queued; });
	DisplayedSingleUnitData.bHasActivity = Active != nullptr;
	DisplayedSingleUnitData.ActivityLabel = Active && Active->CommandButton ? Active->CommandButton->DisplayName : FText::GetEmpty();
	DisplayedSingleUnitData.ActivityProgress = Active ? Active->GetProgress01() : 0.0f;
	DisplayedSingleUnitData.ActivityRemainingSeconds = Active ? Active->GetRemainingSeconds() : 0.0f;
	DisplayedSingleUnitData.ActivityQueueCount = DisplayedSingleUnitData.CommandProgressItems.Num();
	RefreshSingleUnitActivity(DisplayedSingleUnitData);
	ShowCommandProgressItems(DisplayedSingleUnitData);
}

void URTSUnitPanelWidget::OnControlGroupsUpdated(const FRTSControlGroupsView& View)
{
	// Control-group visibility does not change the reserved header or route size.
	ApplySelectionPanelLayoutSettings();
	bHasAssignedControlGroups = View.Groups.ContainsByPredicate(
		[](const FRTSControlGroupView& Group) { return Group.bAssigned && Group.UnitCount > 0; });
	const bool bHasContent = (SingleUnitPanel && SingleUnitPanel->GetVisibility() != ESlateVisibility::Collapsed)
		|| (IconContainer && IconContainer->GetVisibility() != ESlateVisibility::Collapsed)
		|| (SummaryIconContainer && SummaryIconContainer->GetVisibility() != ESlateVisibility::Collapsed);
	SetVisibility(bHasAssignedControlGroups || bHasContent
		? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
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
	SetContentRoute(ERTSSelectionMode::Empty);
	// Hide all visuals while retaining the same allocation in the HUD.
	SetVisibility(bHasAssignedControlGroups
		? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
}

void URTSUnitPanelWidget::SetContentRoute(ERTSSelectionMode Mode)
{
	auto SetRoute = [Mode](UWidget* Widget, ERTSSelectionMode Route)
	{
		if (Widget) Widget->SetVisibility(Mode == Route
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	};
	SetRoute(SingleUnitPanel, ERTSSelectionMode::Single);
	SetRoute(IconContainer, ERTSSelectionMode::List);
	SetRoute(SummaryIconContainer, ERTSSelectionMode::Summary);
}

void URTSUnitPanelWidget::ShowSingleContent(const FRTSUnitData& Data)
{
	HideGridSlots();
	DisplayedSingleUnitData = Data;
	SetContentRoute(ERTSSelectionMode::Single);
	RefreshSingleUnitDetail(Data);
	ShowCommandProgressItems(Data);
}

void URTSUnitPanelWidget::ShowCommandProgressItems(const FRTSUnitData& OwnerData)
{
	for (URTSCommandButtonWidget* Button : ProgressButtonSlots)
		if (Button) Button->SetVisibility(ESlateVisibility::Hidden);
	ProgressButtonSlots.Reset();
	TArray<UUserWidget*> Grids;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Grids, URTSCommanderGridWidget::StaticClass(), false);
	URTSCommanderGridWidget* CommandGrid = nullptr;
	for (UUserWidget* Widget : Grids)
		if (Widget->GetWorld() == GetWorld() && Widget->GetOwningPlayer() == GetOwningPlayer())
		{ CommandGrid = CastChecked<URTSCommanderGridWidget>(Widget); break; }
	if (CommandGrid) CommandGrid->ReleaseFinishedResearchButtons(OwnerData);
	TArray<FRTSTimedCommandInstance> Active, Waiting;
	for (const FRTSTimedCommandInstance& Source : OwnerData.CommandProgressItems)
	{
		FRTSTimedCommandInstance Item = Source;
		if (CommandGrid)
			if (URTSCommandButton* Original = CommandGrid->FindDisplayedCommandButton(Item.CommandTag))
				Item.CommandButton = Original;
		if (!Item.CommandButton || !Item.CommandButton->bIsResearch) continue;
		(Item.State == ERTSTimedCommandState::Queued ? Waiting : Active).Add(Item);
	}
	const bool bResearch = !Active.IsEmpty() || !Waiting.IsEmpty();
	if (WeaponArmorPanel) WeaponArmorPanel->SetVisibility(bResearch ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	if (UnitRosterPane) UnitRosterPane->SetVisibility(bResearch ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (ActivityQueueContainer) ActivityQueueContainer->SetVisibility(Waiting.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	if (!bResearch || !ActivityQueueContainer || !CommandGrid) return;
	if (UWidget* Row = FindDescendantWidgetByName(this, TEXT("ActiveProductionRow")))
		Row->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	constexpr int32 ActiveSlots = 2;
	constexpr int32 QueueSlots = 6;
	if (EmptyQueueFrames.Num() != QueueSlots)
	{
		ActivityQueueContainer->ClearChildren();
		EmptyQueueFrames.Reset();
		for (int32 Index = 0; Index < QueueSlots; ++Index)
		{
			USizeBox* Cell = WidgetTree->ConstructWidget<USizeBox>();
			Cell->SetWidthOverride(SelectionButtonSize.X); Cell->SetHeightOverride(SelectionButtonSize.Y);
			Cell->SetClipping(EWidgetClipping::ClipToBounds);
			if (UGridSlot* CellSlot = Cast<UGridSlot>(ActivityQueueContainer->AddChild(Cell)))
			{
				CellSlot->SetRow(0); CellSlot->SetColumn(Index); CellSlot->SetPadding(SelectionSlotPadding);
				CellSlot->SetHorizontalAlignment(HAlign_Fill); CellSlot->SetVerticalAlignment(VAlign_Fill);
			}
			URTSCommandButtonWidget* Frame = CreateWidget<URTSCommandButtonWidget>(this, CommandGrid->GetCommandButtonWidgetClass());
			if (Frame)
			{
				Frame->Init(nullptr);
				FRTSTimedCommandInstance Empty; Empty.bCanCancel = false;
				Frame->InitProgressItem(Empty, nullptr, SelectionButtonSize.X);
				Cell->AddChild(Frame);
			}
			EmptyQueueFrames.Add(Frame);
		}
	}
	for (int32 Index = 0; Index < ActiveSlots + QueueSlots; ++Index)
	{
		const bool bQueued = Index >= ActiveSlots;
		const int32 Position = bQueued ? Index - ActiveSlots : Index;
		const TArray<FRTSTimedCommandInstance>& Items = bQueued ? Waiting : Active;
		const bool bOccupied = Items.IsValidIndex(Position);
		UPanelWidget* Host = bQueued ? Cast<UPanelWidget>(ActivityQueueContainer->GetChildAt(Position))
			: Cast<UPanelWidget>(FindDescendantWidgetByName(this, *FString::Printf(TEXT("ActiveIconHost%d"), Position)));
		if (!bQueued)
		{
			if (UWidget* Bounds = FindDescendantWidgetByName(this, *FString::Printf(TEXT("ActiveProductionBounds%d"), Position)))
				Bounds->SetVisibility(bOccupied ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			if (UWidget* ActiveSlot = FindDescendantWidgetByName(this, *FString::Printf(TEXT("ActiveProductionSlot%d"), Position)))
				ActiveSlot->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		if (!Host) continue;
		if (!bOccupied)
		{
			if (bQueued && EmptyQueueFrames[Position])
			{
				if (EmptyQueueFrames[Position]->GetParent() != Host)
				{ Host->ClearChildren(); Host->AddChild(EmptyQueueFrames[Position]); }
			}
			else Host->ClearChildren();
			continue;
		}
		const FRTSTimedCommandInstance& Item = Items[Position];
		URTSCommandButtonWidget* Button = CommandGrid->AcquireResearchButton(Item, OwnerData);
		if (!Button) continue;
		if (Button->GetParent() != Host)
		{ Host->ClearChildren(); Host->AddChild(Button); }
		ProgressButtonSlots.Add(Button);
		if (!bQueued)
		{
			if (UTextBlock* Status = Cast<UTextBlock>(FindDescendantWidgetByName(this, *FString::Printf(TEXT("ActiveStatus%d"), Position))))
				Status->SetText(FText::FromString(Item.State == ERTSTimedCommandState::Paused ? TEXT("已暂停") : TEXT("正在研发")));
			UProgressBar* Progress = Position == 0 ? ActiveProgress0 : ActiveProgress1;
			if (Progress) Progress->SetPercent(Item.GetProgress01());
		}
	}
}

void URTSUnitPanelWidget::ShowGridContent(const FRTSSelectionView& View)
{
	HideGridSlots();
	DisplayedSingleUnitData = FRTSUnitData();
	SetContentRoute(View.Mode);
	const bool bSummary = View.Mode == ERTSSelectionMode::Summary;
	const TArray<URTSUnitIconWidget*>& Slots = bSummary ? SummaryIconSlots : IconSlots;
	const int32 VisibleCount = FMath::Min(View.Items.Num(), Slots.Num());
	for (int32 Index = 0; Index < VisibleCount; ++Index)
	{
		const FRTSUnitData& Data = View.Items[Index];
		URTSUnitIconWidget* Icon = Slots[Index];
		Icon->InitData(Data, true, !bSummary, !bSummary, IconSlotSize);
		Icon->SetIsActive(View.ActiveGroupKey.IsEmpty()
			|| GetSelectionWidgetUnitGroupKey(Data) == View.ActiveGroupKey);
		Icon->SetVisibility(ESlateVisibility::Visible);
		if (bSummary && CountSlots.IsValidIndex(Index))
		{
			CountSlots[Index]->SetText(FText::FromString(FString::Printf(TEXT("x%d"), Data.Count)));
			CountSlots[Index]->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

void URTSUnitPanelWidget::HideGridSlots()
{
	for (URTSCommandButtonWidget* ProgressButton : ProgressButtonSlots)
		if (ProgressButton) ProgressButton->SetVisibility(ESlateVisibility::Hidden);

	for (URTSUnitIconWidget* SlotWidget : IconSlots)
	{
		if (SlotWidget)
		{
			SlotWidget->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	for (URTSUnitIconWidget* Icon : SummaryIconSlots)
		if (Icon) Icon->SetVisibility(ESlateVisibility::Hidden);

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
	UWidget* DetailRoot = this;
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

	SetOptionalDetailText(TEXT("UnitClassificationText"), Data.OrganizationLabel.IsEmpty()
		? Data.UnitCategory : Data.OrganizationLabel + TEXT(" - ") + Data.UnitCategory);
	SetOptionalDetailText(TEXT("WeaponStats"), Data.bHasWeapon
		? FString::Printf(TEXT("基础伤害  %.0f\n射程  %.0f\n冷却  %.1fs"), Data.WeaponDamage, Data.WeaponRange, Data.WeaponPeriod)
		: TEXT("无武器"));
	SetOptionalDetailText(TEXT("ArmorStats"), FString::Printf(TEXT("普通伤害减免\n%.0f%%"), Data.ArmorReduction * 100.0f));
	SetOptionalDetailText(TEXT("HealthValueText"), Data.MaxHealth > 0.0f
		? FString::Printf(TEXT("%.0f / %.0f"), FMath::Clamp(Data.Health, 0.0f, Data.MaxHealth), Data.MaxHealth) : FString());
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
	UWidget* DetailRoot = this;
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
	if (Data.bHasActivity && Data.CommandProgressItems.IsEmpty())
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
		ActivityBar->SetVisibility(Data.bHasActivity && Data.CommandProgressItems.IsEmpty()
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
