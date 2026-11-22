#include "UI/RTSFormationListWidget.h"

#include "RTSInputPanelSettings.h"
#include "RTSSelectionSubsystem.h"
#include "UI/RTSControlGroupButton.h"
#include "UI/RTSCommanderGridWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Spacer.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

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
		FormationEdgePadding = FMargin(ButtonPadding.Left, 0.0f, ButtonPadding.Right, 0.0f);
		const int32 SlotCount = UE_ARRAY_COUNT(FormationControlGroupDisplayOrder);
		const float StripWidth = FMath::Max(8, Settings->SelectionGridColumns) * CellWidth;
		const FVector2D ButtonSize = GridDefaults->GetButtonSize();
		FormationSlotGap = FMath::Max(0.0f, Settings->FormationListSlotGap);
		const float AvailableHeight = FMath::Max(1.0f, FMath::Min(
			static_cast<float>(Settings->FormationListSlotHeight),
			Settings->SelectionPanelHeaderHeight - FormationSlotGap));
		FormationSlotWidth = FMath::Max(1.0, FMath::Min3<double>(
			static_cast<float>(Settings->FormationListSlotWidth),
			AvailableHeight * ButtonSize.X / FMath::Max(1.0, ButtonSize.Y),
			(StripWidth - ButtonPadding.Left - ButtonPadding.Right - (SlotCount - 1) * FormationSlotGap) / SlotCount));
		FormationSlotHeight = FMath::Max(1, FMath::RoundToInt(FormationSlotWidth * ButtonSize.Y / FMath::Max(1.0, ButtonSize.X)));
	}
}

void URTSFormationListWidget::BuildSlotPool()
{
	if (!FormationSlotContainer || !WidgetTree)
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSFormationListWidget: FormationSlotContainer is not bound."));
		return;
	}
	UClass* SharedButtonClass = LoadClass<UUserWidget>(nullptr,
		TEXT("/Game/UI/HeadUpDisplay/ControlGird/CommandButton.CommandButton_C"));
	const UWidgetTree* SharedButtonTree = SharedButtonClass
		? FindObject<UWidgetTree>(SharedButtonClass, TEXT("WidgetTree")) : nullptr;
	const UButton* SharedButton = SharedButtonTree
		? Cast<UButton>(SharedButtonTree->FindWidget(TEXT("MainButton"))) : nullptr;

	FormationSlotContainer->ClearChildren();
	ControlGroupButtons.SetNumZeroed(10);
	ControlGroupGrid = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ControlGroupGrid"));
	FormationSlotContainer->AddChild(ControlGroupGrid);
	if (UHorizontalBoxSlot* HostSlot = Cast<UHorizontalBoxSlot>(ControlGroupGrid->Slot))
	{
		HostSlot->SetHorizontalAlignment(HAlign_Left);
		HostSlot->SetVerticalAlignment(VAlign_Center);
		HostSlot->SetPadding(FormationEdgePadding);
	}
	if (!ControlGroupButtonClass) ControlGroupButtonClass = URTSControlGroupButton::StaticClass();

	for (int32 SlotIndex = 0; SlotIndex < UE_ARRAY_COUNT(FormationControlGroupDisplayOrder); ++SlotIndex)
	{
		if (SlotIndex > 0)
		{
			USpacer* Gap = WidgetTree->ConstructWidget<USpacer>();
			Gap->SetSize(FVector2D(FormationSlotGap, 0.0f));
			ControlGroupGrid->AddChildToHorizontalBox(Gap);
		}
		const int32 GroupIndex = FormationControlGroupDisplayOrder[SlotIndex];
		URTSControlGroupButton* Button = WidgetTree->ConstructWidget<URTSControlGroupButton>(
			ControlGroupButtonClass, FName(*FString::Printf(TEXT("ControlGroup_%d"), GroupIndex)));
		Button->ControlGroupIndex = GroupIndex;
		if (SharedButton)
		{
			Button->SetStyle(SharedButton->GetStyle());
			Button->AssignedGroupColor = SharedButton->GetBackgroundColor();
		}

		UOverlay* Face = WidgetTree->ConstructWidget<UOverlay>();
		Button->AddChild(Face);
		UImage* Icon = WidgetTree->ConstructWidget<UImage>();
		Icon->SetDesiredSizeOverride(FVector2D(FormationSlotWidth, FormationSlotHeight));
		UOverlaySlot* IconSlot = Face->AddChildToOverlay(Icon);
		IconSlot->SetPadding(FMargin(4.0f));
		IconSlot->SetHorizontalAlignment(HAlign_Fill);
		IconSlot->SetVerticalAlignment(VAlign_Fill);

		UTextBlock* NumberText = WidgetTree->ConstructWidget<UTextBlock>();
		UTextBlock* CountText = WidgetTree->ConstructWidget<UTextBlock>();
		for (UTextBlock* Text : {NumberText, CountText})
		{
			FSlateFontInfo Font = Text->GetFont();
			Font.Size = FMath::Max(10, FormationSlotHeight / 5);
			Text->SetFont(Font);
			Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			Text->SetShadowColorAndOpacity(FLinearColor::Black);
			Text->SetShadowOffset(FVector2D(1.0f, 1.0f));
			UOverlaySlot* TextSlot = Face->AddChildToOverlay(Text);
			TextSlot->SetPadding(FMargin(5.0f));
			TextSlot->SetHorizontalAlignment(Text == NumberText ? HAlign_Left : HAlign_Right);
			TextSlot->SetVerticalAlignment(Text == NumberText ? VAlign_Top : VAlign_Bottom);
		}
		UImage* ActiveFrame = WidgetTree->ConstructWidget<UImage>();
		ActiveFrame->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 0.0f, FLinearColor::White, 3.0f));
		UOverlaySlot* FrameSlot = Face->AddChildToOverlay(ActiveFrame);
		FrameSlot->SetHorizontalAlignment(HAlign_Fill);
		FrameSlot->SetVerticalAlignment(VAlign_Fill);
		Button->SetPresentationWidgets(Icon, NumberText, CountText, ActiveFrame);

		USizeBox* SlotBox = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), FName(*FString::Printf(TEXT("ControlGroupSlot_%d"), GroupIndex)));
		SlotBox->SetWidthOverride(FormationSlotWidth);
		SlotBox->SetHeightOverride(FormationSlotHeight);
		SlotBox->SetClipping(EWidgetClipping::ClipToBounds);
		SlotBox->SetContent(Button);
		UHorizontalBoxSlot* GridSlot = ControlGroupGrid->AddChildToHorizontalBox(SlotBox);
		GridSlot->SetHorizontalAlignment(HAlign_Fill);
		GridSlot->SetVerticalAlignment(VAlign_Fill);
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

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFormationSizingTest, "Winyunq.Input.UnitPresentation.FormationSizing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSFormationSizingTest::RunTest(const FString& Parameters)
{
	URTSFormationListWidget* Formation = NewObject<URTSFormationListWidget>();
	Formation->Initialize();
	const TSharedRef<SWidget> SlateWidget = Formation->TakeWidget();
	USizeBox* First = Cast<USizeBox>(Formation->GetWidgetFromName(TEXT("ControlGroupSlot_1")));
	if (!TestNotNull(TEXT("Formation creates its numbered slots"), First)) return false;
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	TestTrue(TEXT("Formation fits its header height"), First->GetHeightOverride() + Settings->FormationListSlotGap
		<= Settings->SelectionPanelHeaderHeight + 1.0f);
	TestTrue(TEXT("Formation portraits keep square command-card proportions"),
		FMath::IsNearlyEqual(First->GetWidthOverride(), First->GetHeightOverride(), 1.0f));
	for (int32 Index : FormationControlGroupDisplayOrder)
	{
		const USizeBox* SlotBox = Cast<USizeBox>(Formation->GetWidgetFromName(
			FName(*FString::Printf(TEXT("ControlGroupSlot_%d"), Index))));
		if (!TestNotNull(TEXT("Each numbered formation slot exists"), SlotBox)) return false;
		TestEqual(TEXT("Formation widths agree"), SlotBox->GetWidthOverride(), First->GetWidthOverride());
		TestEqual(TEXT("Formation heights agree"), SlotBox->GetHeightOverride(), First->GetHeightOverride());
	}
	return true;
}
#endif
