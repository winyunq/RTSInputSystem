#include "UI/RTSControlGroupButton.h"

#include "RTSInputPanelSettings.h"
#include "RTSSelectionSubsystem.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Texture2D.h"
#endif

URTSControlGroupButton::URTSControlGroupButton()
{
	SetClickMethod(EButtonClickMethod::MouseDown);
}

void URTSControlGroupButton::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();
	OnClicked.AddUniqueDynamic(this, &URTSControlGroupButton::HandleClicked);
	BindToSelectionSubsystem();
	RefreshControlGroupState();
}

void URTSControlGroupButton::ReleaseSlateResources(bool bReleaseChildren)
{
	UnbindFromSelectionSubsystem();
	OnClicked.RemoveDynamic(this, &URTSControlGroupButton::HandleClicked);
	Super::ReleaseSlateResources(bReleaseChildren);
}

URTSSelectionSubsystem* URTSControlGroupButton::GetSelectionSubsystem() const
{
	if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		return LocalPlayer->GetSubsystem<URTSSelectionSubsystem>();
	}
	return nullptr;
}

void URTSControlGroupButton::BindToSelectionSubsystem()
{
	if (URTSSelectionSubsystem* Selection = GetSelectionSubsystem())
	{
		Selection->OnControlGroupsChanged.AddUniqueDynamic(this, &URTSControlGroupButton::HandleControlGroupsChanged);
	}
}

void URTSControlGroupButton::UnbindFromSelectionSubsystem()
{
	if (URTSSelectionSubsystem* Selection = GetSelectionSubsystem())
	{
		Selection->OnControlGroupsChanged.RemoveDynamic(this, &URTSControlGroupButton::HandleControlGroupsChanged);
	}
}

void URTSControlGroupButton::SetControlGroupIndex(int32 InGroupIndex)
{
	ControlGroupIndex = FMath::Clamp(InGroupIndex, 0, 9);
	RefreshControlGroupState();
}

void URTSControlGroupButton::SetPresentationWidgets(UImage* InIcon, UTextBlock* InNumber, UTextBlock* InCount, UImage* InActiveFrame)
{
	GroupIcon = InIcon;
	GroupNumberText = InNumber;
	GroupCountText = InCount;
	GroupActiveFrame = InActiveFrame;
	ApplyControlGroupView(CurrentGroupView);
}

void URTSControlGroupButton::RefreshControlGroupState()
{
	if (URTSSelectionSubsystem* Selection = GetSelectionSubsystem())
	{
		HandleControlGroupsChanged(Selection->GetControlGroupsView());
		return;
	}

	FRTSControlGroupView EmptyView;
	EmptyView.GroupIndex = ControlGroupIndex;
	ApplyControlGroupView(EmptyView);
}

void URTSControlGroupButton::HandleControlGroupsChanged(const FRTSControlGroupsView& GroupsView)
{
	if (const FRTSControlGroupView* MatchingView = GroupsView.Groups.FindByPredicate([this](const FRTSControlGroupView& View)
	{
		return View.GroupIndex == ControlGroupIndex;
	}))
	{
		ApplyControlGroupView(*MatchingView);
	}
	else
	{
		FRTSControlGroupView EmptyView;
		EmptyView.GroupIndex = ControlGroupIndex;
		ApplyControlGroupView(EmptyView);
	}
}

void URTSControlGroupButton::ApplyControlGroupView(const FRTSControlGroupView& GroupView)
{
	if (GroupView.GroupIndex != ControlGroupIndex)
	{
		return;
	}

	CurrentGroupView = GroupView;
	// Empty groups retain their numbered slot but neither paint nor receive input.
	SetVisibility(GroupView.bAssigned && GroupView.UnitCount > 0
		? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	SetBackgroundColor(AssignedGroupColor);
	if (GroupActiveFrame)
	{
		GroupActiveFrame->SetColorAndOpacity(ActiveGroupColor);
		GroupActiveFrame->SetVisibility(GroupView.bActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	SetToolTipText(BuildTooltip(GroupView));

	if (GroupNumberText)
	{
		GroupNumberText->SetText(FText::AsNumber(ControlGroupIndex));
	}
	if (GroupCountText)
	{
		GroupCountText->SetText(GroupView.bAssigned
			? FText::Format(FText::FromString(TEXT("×{0}")), FText::AsNumber(GroupView.UnitCount))
			: FText::FromString(TEXT("—")));
	}
	if (GroupIcon)
	{
		GroupIcon->SetBrushFromTexture(GroupView.bAssigned ? GroupView.RepresentativeUnit.Icon : nullptr);
		GroupIcon->SetOpacity(GroupView.bAssigned ? 1.0f : 0.0f);
	}
	if (DefaultLabel)
	{
		DefaultLabel->SetText(FText::Format(
			FText::FromString(TEXT("{0}\n{1}")),
			FText::AsNumber(ControlGroupIndex),
			GroupView.bAssigned ? FText::AsNumber(GroupView.UnitCount) : FText::FromString(TEXT("—"))));
	}

	OnControlGroupStateChanged.Broadcast(CurrentGroupView);
}

FText URTSControlGroupButton::BuildTooltip(const FRTSControlGroupView& GroupView) const
{
	FString Tooltip = FString::Printf(
		TEXT("编队 %d%s\n左键：选择  双击：居中\nCtrl：覆盖  Shift：增删  Alt：抢占"),
		ControlGroupIndex,
		GroupView.bAssigned ? *FString::Printf(TEXT(" · %d 单位"), GroupView.UnitCount) : TEXT(" · 未设置"));

	if (GroupView.bAssigned && !GroupView.Composition.IsEmpty())
	{
		Tooltip += TEXT("\n");
		for (int32 Index = 0; Index < GroupView.Composition.Num(); ++Index)
		{
			const FRTSControlGroupComposition& Entry = GroupView.Composition[Index];
			if (Index > 0)
			{
				Tooltip += TEXT("  ·  ");
			}
			Tooltip += FString::Printf(TEXT("%s ×%d"), *Entry.UnitType.Name, Entry.Count);
		}
	}
	return FText::FromString(Tooltip);
}

void URTSControlGroupButton::HandleClicked()
{
	URTSSelectionSubsystem* Selection = GetSelectionSubsystem();
	if (!Selection)
	{
		return;
	}

	FModifierKeysState Modifiers;
	if (bUseKeyboardModifiers && FSlateApplication::IsInitialized())
	{
		Modifiers = FSlateApplication::Get().GetModifierKeys();
	}

	if (bUseKeyboardModifiers && Modifiers.IsAltDown())
	{
		Selection->AssignCurrentSelectionToControlGroup(ControlGroupIndex, ERTSControlGroupAssignmentMode::StealAndReplace);
		return;
	}
	if (bUseKeyboardModifiers && Modifiers.IsControlDown())
	{
		Selection->AssignCurrentSelectionToControlGroup(ControlGroupIndex, ERTSControlGroupAssignmentMode::Replace);
		return;
	}
	if (bUseKeyboardModifiers && Modifiers.IsShiftDown())
	{
		Selection->AssignCurrentSelectionToControlGroup(ControlGroupIndex, ERTSControlGroupAssignmentMode::ToggleMembership);
		return;
	}

	if (!Selection->RecallControlGroup(ControlGroupIndex))
	{
		return;
	}

	const double CurrentTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const double DoubleClickTime = Settings ? FMath::Max(0.1, Settings->GetPlayerSettingNumber(
		TEXT("GetControlGroupDoubleTapTimeIndex"),
		Settings->ControlGroupDoubleTapTime)) : 0.3;
	if (bCenterOnDoubleClick && CurrentTime - LastRecallClickTime <= DoubleClickTime)
	{
		Selection->RequestControlGroupFocus(ControlGroupIndex);
	}
	LastRecallClickTime = CurrentTime;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSControlGroupFeedbackTest, "Winyunq.Input.UnitPresentation.ControlGroupFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSControlGroupFeedbackTest::RunTest(const FString& Parameters)
{
	URTSControlGroupButton* Button = NewObject<URTSControlGroupButton>();
	UImage* Icon = NewObject<UImage>();
	UImage* Frame = NewObject<UImage>();
	UTextBlock* Number = NewObject<UTextBlock>();
	UTextBlock* Count = NewObject<UTextBlock>();
	FSlateBrush Brush;
	Brush.ImageSize = FVector2D(64, 64);
	Icon->SetBrush(Brush);
	Button->SetPresentationWidgets(Icon, Number, Count, Frame);
	FRTSControlGroupView View;
	View.GroupIndex = Button->ControlGroupIndex;
	View.bAssigned = true;
	View.UnitCount = 12;
	View.RepresentativeUnit.Icon = NewObject<UTexture2D>();
	Button->ApplyControlGroupView(View);
	TestEqual(TEXT("Texture resolution does not resize a formation icon"), FVector2D(Icon->GetBrush().ImageSize), FVector2D(64, 64));
	TestEqual(TEXT("Assigned group shows its number"), Number->GetText().ToString(), FString::FromInt(View.GroupIndex));
	TestEqual(TEXT("Inactive group hides the state frame"), Frame->GetVisibility(), ESlateVisibility::Collapsed);
	const FLinearColor Background = Button->GetBackgroundColor();
	View.bActive = true;
	Button->ApplyControlGroupView(View);
	TestEqual(TEXT("Active group shows the state frame"), Frame->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("Active group preserves the shared button background"), Button->GetBackgroundColor(), Background);
	View.bAssigned = false;
	View.bActive = false;
	View.UnitCount = 0;
	Button->ApplyControlGroupView(View);
	TestEqual(TEXT("Empty group keeps its numbered slot without painting"), Button->GetVisibility(), ESlateVisibility::Hidden);
	return true;
}
#endif
