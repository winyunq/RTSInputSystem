#include "UI/RTSFormationListWidget.h"

#include "RTSInputPanelSettings.h"
#include "RTSSelectionSubsystem.h"
#include "UI/RTSControlGroupButton.h"
#include "UI/RTSCommanderGridWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Spacer.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

namespace
{
	const int32 FormationControlGroupDisplayOrder[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
}

TSharedRef<SWidget> URTSFormationListWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UHorizontalBox* Root = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("FormationSlotContainer"));
		FormationSlotContainer = Root;
		WidgetTree->RootWidget = Root;
	}

	return Super::RebuildWidget();
}

void URTSFormationListWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyFormationSettings();
}

void URTSFormationListWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ApplyFormationSettings();
	BuildSlotPool();

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
			{
				Selection->OnControlGroupsChanged.AddUniqueDynamic(this, &URTSFormationListWidget::OnControlGroupsUpdated);
				OnControlGroupsUpdated(Selection->GetControlGroupsView());
			}
		}
	}
}

void URTSFormationListWidget::NativeDestruct()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
			{
				Selection->OnControlGroupsChanged.RemoveDynamic(this, &URTSFormationListWidget::OnControlGroupsUpdated);
			}
		}
	}
	Super::NativeDestruct();
}

void URTSFormationListWidget::ApplyFormationSettings()
{
	if (const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>())
	{
		const UClass* GridClass = LoadClass<URTSCommanderGridWidget>(nullptr,
			TEXT("/Game/UI/HeadUpDisplay/ControlGird/ControlGrid.ControlGrid_C"));
		const URTSCommanderGridWidget* GridDefaults = GridClass
			? GridClass->GetDefaultObject<URTSCommanderGridWidget>()
			: GetDefault<URTSCommanderGridWidget>();
		const FMargin ButtonPadding = GridDefaults->GetSlotPadding();
		const float CellWidth = GridDefaults->GetButtonSize().X + ButtonPadding.Left + ButtonPadding.Right;
		FormationSlotWidth = FMath::Max(1, Settings->FormationListSlotWidth);
		FormationSlotHeight = FMath::Max(1, Settings->FormationListSlotHeight);
		FormationEdgePadding = FMargin(ButtonPadding.Left, 0.0f, ButtonPadding.Right, 0.0f);
		const int32 SlotCount = UE_ARRAY_COUNT(FormationControlGroupDisplayOrder);
		const float StripWidth = FMath::Max(8, Settings->SelectionGridColumns) * CellWidth;
		FormationSlotGap = FMath::Max(0.0f, (StripWidth - ButtonPadding.Left - ButtonPadding.Right
			- SlotCount * FormationSlotWidth) / (SlotCount - 1));
	}
}

void URTSFormationListWidget::BuildSlotPool()
{
	if (!FormationSlotContainer || !WidgetTree)
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSFormationListWidget: FormationSlotContainer is not bound."));
		return;
	}

	FormationSlotContainer->ClearChildren();
	ControlGroupButtons.SetNumZeroed(10);
	ControlGroupGrid = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(),
		TEXT("ControlGroupGrid"));
	if (!ControlGroupGrid)
	{
		return;
	}

	FormationSlotContainer->AddChild(ControlGroupGrid);
	if (UHorizontalBoxSlot* GridHostSlot = Cast<UHorizontalBoxSlot>(ControlGroupGrid->Slot))
	{
		GridHostSlot->SetHorizontalAlignment(HAlign_Left);
		GridHostSlot->SetVerticalAlignment(VAlign_Center);
		GridHostSlot->SetPadding(FormationEdgePadding);
	}

	if (!ControlGroupButtonClass)
	{
		ControlGroupButtonClass = URTSControlGroupButton::StaticClass();
	}

	const int32 SlotCount = UE_ARRAY_COUNT(FormationControlGroupDisplayOrder);
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		if (SlotIndex > 0)
		{
			USpacer* Gap = WidgetTree->ConstructWidget<USpacer>();
			Gap->SetSize(FVector2D(FormationSlotGap, 0.0f));
			ControlGroupGrid->AddChildToHorizontalBox(Gap);
		}
		const int32 GroupIndex = FormationControlGroupDisplayOrder[SlotIndex];
		URTSControlGroupButton* Button = WidgetTree->ConstructWidget<URTSControlGroupButton>(
			ControlGroupButtonClass,
			FName(*FString::Printf(TEXT("ControlGroup_%d"), GroupIndex)));
		if (!Button)
		{
			continue;
		}
		Button->ControlGroupIndex = GroupIndex;

		UHorizontalBox* CardRow = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(),
			FName(*FString::Printf(TEXT("ControlGroupRow_%d"), GroupIndex)));
		USizeBox* PortraitBox = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("ControlGroupPortraitBox_%d"), GroupIndex)));
		UImage* Icon = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(),
			FName(*FString::Printf(TEXT("ControlGroupIcon_%d"), GroupIndex)));
		UVerticalBox* InfoBox = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(),
			FName(*FString::Printf(TEXT("ControlGroupInfo_%d"), GroupIndex)));
		UTextBlock* NumberText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("ControlGroupNumber_%d"), GroupIndex)));
		UTextBlock* CountText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("ControlGroupCount_%d"), GroupIndex)));

		if (!CardRow || !PortraitBox || !Icon || !InfoBox || !NumberText || !CountText)
		{
			continue;
		}

		Button->AddChild(CardRow);
		const float PortraitSize = FMath::Min(static_cast<float>(FormationSlotHeight), FormationSlotWidth * 0.5f);
		PortraitBox->SetWidthOverride(PortraitSize);
		PortraitBox->SetHeightOverride(PortraitSize);
		PortraitBox->SetContent(Icon);
		if (UHorizontalBoxSlot* PortraitSlot = CardRow->AddChildToHorizontalBox(PortraitBox))
		{
			FSlateChildSize AutoSize;
			AutoSize.SizeRule = ESlateSizeRule::Automatic;
			PortraitSlot->SetSize(AutoSize);
			PortraitSlot->SetHorizontalAlignment(HAlign_Fill);
			PortraitSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (UHorizontalBoxSlot* InfoSlot = CardRow->AddChildToHorizontalBox(InfoBox))
		{
			FSlateChildSize FillSize;
			FillSize.SizeRule = ESlateSizeRule::Fill;
			FillSize.Value = 1.0f;
			InfoSlot->SetSize(FillSize);
			InfoSlot->SetHorizontalAlignment(HAlign_Fill);
			InfoSlot->SetVerticalAlignment(VAlign_Fill);
			InfoSlot->SetPadding(FMargin(6.0f, 2.0f, 4.0f, 2.0f));
		}

		NumberText->SetText(FText::AsNumber(GroupIndex));
		NumberText->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 1.0f, 0.91f, 1.0f)));
		NumberText->SetShadowColorAndOpacity(FLinearColor::Black);
		NumberText->SetShadowOffset(FVector2D(1.0f, 1.0f));
		FSlateFontInfo NumberFont = NumberText->GetFont();
		NumberFont.Size = FMath::Max(14, FormationSlotHeight / 4);
		NumberText->SetFont(NumberFont);
		if (UVerticalBoxSlot* NumberSlot = InfoBox->AddChildToVerticalBox(NumberText))
		{
			FSlateChildSize AutoSize;
			AutoSize.SizeRule = ESlateSizeRule::Automatic;
			NumberSlot->SetSize(AutoSize);
			NumberSlot->SetHorizontalAlignment(HAlign_Left);
			NumberSlot->SetVerticalAlignment(VAlign_Center);
		}

		CountText->SetJustification(ETextJustify::Center);
		CountText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		CountText->SetShadowColorAndOpacity(FLinearColor::Black);
		CountText->SetShadowOffset(FVector2D(1.0f, 1.0f));
		FSlateFontInfo CountFont = CountText->GetFont();
		CountFont.Size = FMath::Max(14, FormationSlotHeight / 4);
		CountText->SetFont(CountFont);
		if (UVerticalBoxSlot* CountSlot = InfoBox->AddChildToVerticalBox(CountText))
		{
			FSlateChildSize FillSize;
			FillSize.SizeRule = ESlateSizeRule::Fill;
			FillSize.Value = 1.0f;
			CountSlot->SetSize(FillSize);
			CountSlot->SetHorizontalAlignment(HAlign_Fill);
			CountSlot->SetVerticalAlignment(VAlign_Center);
		}

		Button->SetPresentationWidgets(Icon, NumberText, CountText);

		USizeBox* SlotBox = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("ControlGroupSlot_%d"), GroupIndex)));
		if (!SlotBox)
		{
			continue;
		}

		SlotBox->SetWidthOverride(FormationSlotWidth);
		SlotBox->SetHeightOverride(FormationSlotHeight);
		SlotBox->SetClipping(EWidgetClipping::ClipToBounds);
		SlotBox->SetContent(Button);
		if (UHorizontalBoxSlot* GridSlot = ControlGroupGrid->AddChildToHorizontalBox(SlotBox))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
		}

		ControlGroupButtons[GroupIndex] = Button;
	}

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void URTSFormationListWidget::OnControlGroupsUpdated(const FRTSControlGroupsView& View)
{
	RefreshControlGroups(View);
	OnControlGroupListChanged(View);
}

void URTSFormationListWidget::RefreshControlGroups(const FRTSControlGroupsView& View)
{
	for (URTSControlGroupButton* Button : ControlGroupButtons)
	{
		if (!Button) continue;
		const FRTSControlGroupView* GroupView = View.Groups.FindByPredicate(
			[Button](const FRTSControlGroupView& Group) { return Group.GroupIndex == Button->ControlGroupIndex; });
		if (GroupView)
		{
			Button->ApplyControlGroupView(*GroupView);
		}
		else
		{
			FRTSControlGroupView EmptyGroup;
			EmptyGroup.GroupIndex = Button->ControlGroupIndex;
			Button->ApplyControlGroupView(EmptyGroup);
		}
	}
}
