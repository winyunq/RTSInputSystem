// Copyright 2024 Winy unq All Rights Reserved.

#include "UI/RTSTooltipWidget.h"
#include "UI/RTSCommandButtonWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Engine/DataTable.h"
#include "GameFramework/PlayerController.h"

namespace
{
	const TCHAR* UnifiedTooltipRichTextStyleSetPath = TEXT("/Game/UI/HeadUpDisplay/RTSStyle/DT_RTS_UnifiedRichTextStyle.DT_RTS_UnifiedRichTextStyle");
}

TSharedRef<SWidget> URTSTooltipWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("Background"));
		Background->SetBrushColor(FLinearColor(0.008f, 0.018f, 0.028f, 0.96f));
		Background->SetPadding(FMargin(14.0f));

		UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("TooltipStack"));
		ContentBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ContentBox"));
		ContentBox->SetContent(Stack);
		Background->SetContent(ContentBox);

		UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
		Stack->AddChildToVerticalBox(Header);
		TitleText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("TitleText"));
		Header->AddChildToHorizontalBox(TitleText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HotkeyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HotkeyText"));
		Header->AddChildToHorizontalBox(HotkeyText);
		CostsBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CostsBox"));
		Stack->AddChildToVerticalBox(CostsBox);
		DurationRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("DurationRow"));
		Stack->AddChildToVerticalBox(DurationRow);
		UImage* DurationIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("DurationIcon"));
		DurationRow->AddChildToHorizontalBox(DurationIcon);
		DurationText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DurationText"));
		DurationRow->AddChildToHorizontalBox(DurationText);
		EffectRowsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EffectRowsBox"));
		Stack->AddChildToVerticalBox(EffectRowsBox);

		DescriptionText = WidgetTree->ConstructWidget<URichTextBlock>(
			URichTextBlock::StaticClass(), TEXT("DescriptionText"));
		Stack->AddChildToVerticalBox(DescriptionText);

		RequirementsText = WidgetTree->ConstructWidget<URichTextBlock>(URichTextBlock::StaticClass(), TEXT("RequirementsText"));
		Stack->AddChildToVerticalBox(RequirementsText);
		StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
		Stack->AddChildToVerticalBox(StatusText);

		WidgetTree->RootWidget = Background;
	}

	return Super::RebuildWidget();
}

void URTSTooltipWidget::NativeConstruct()
{
    Super::NativeConstruct();

	// RichTextBlock's Slate style instance exists only after construction.
	ApplyConfiguredStyle();
	SetContentWidth(TooltipWrapTextAt);
	// Keep the authored material brush; RebuildWidget supplies the native fallback.
}

void URTSTooltipWidget::ApplyConfiguredStyle()
{
    if (TitleText)
    {
        FSlateFontInfo Font = TitleText->GetFont();
        Font.Size = DefaultFontSize;
        TitleText->SetFont(Font);
        TitleText->SetColorAndOpacity(FSlateColor(TitleColor));
        TitleText->SetAutoWrapText(true);
        TitleText->SetWrapTextAt(TooltipWrapTextAt);
    }

    for (URichTextBlock* Text : {DescriptionText.Get(), RequirementsText.Get()})
    {
        if (!Text) continue;
        if (!UnifiedRichTextStyleSet)
        {
            UnifiedRichTextStyleSet = LoadObject<UDataTable>(nullptr, UnifiedTooltipRichTextStyleSetPath);
        }

        if (UnifiedRichTextStyleSet)
        {
            Text->SetTextStyleSet(UnifiedRichTextStyleSet);
        }

        FTextBlockStyle DefaultStyle = Text->GetDefaultTextStyle();
        DefaultStyle.Font.Size = DescriptionFontSize;
        DefaultStyle.SetColorAndOpacity(FSlateColor(Text == RequirementsText ? TitleColor : DescriptionColor));
        Text->SetDefaultTextStyle(DefaultStyle);
        Text->SetAutoWrapText(true);
        Text->SetWrapTextAt(TooltipWrapTextAt);
    }

    for (UTextBlock* Text : {HotkeyText.Get(), DurationText.Get(), StatusText.Get()})
    {
        if (!Text) continue;
        FSlateFontInfo Font = Text->GetFont();
        Font.Size = CostFontSize;
        Text->SetFont(Font);
        Text->SetColorAndOpacity(FSlateColor(Text == HotkeyText ? TitleColor : DescriptionColor));
        Text->SetAutoWrapText(true);
        Text->SetWrapTextAt(TooltipWrapTextAt);
    }
}

void URTSTooltipWidget::SetContentWidth(float Width)
{
	const UBorder* Background = WidgetTree ? Cast<UBorder>(WidgetTree->FindWidget(TEXT("Background"))) : nullptr;
	const float ContentWidth = FMath::Max(1.0f, Width - (Background ? Background->GetPadding().GetTotalSpaceAlong<Orient_Horizontal>() : 0.0f));
	if (ContentBox) ContentBox->SetWidthOverride(ContentWidth);
	for (UTextBlock* Text : {TitleText.Get(), StatusText.Get()})
		if (Text) Text->SetWrapTextAt(ContentWidth);
	for (URichTextBlock* Text : {DescriptionText.Get(), RequirementsText.Get()})
		if (Text) Text->SetWrapTextAt(ContentWidth);
}

void URTSTooltipWidget::UpdateTooltip(URTSCommandButton* Data, AActor* Executor, URTSCommandButtonWidget* SourceButton)
{
	if (!Data) return;
	const FRTSCommandState& State = SourceButton ? SourceButton->GetTooltipState()
		: Data->GetCommandStateForContext(this, Executor ? Executor : GetOwningPlayer());

    SetTooltipContent(Data->GetDisplayNameForContext(this, Executor ? Executor : GetOwningPlayer()),
        State, nullptr);
	if (HotkeyText)
	{
		const FKey Key = SourceButton ? SourceButton->GetCommandHotkey() : Data->Hotkey;
		HotkeyText->SetText(Key.IsValid() ? FText::Format(FText::FromString(TEXT("[{0}]")), Key.GetDisplayName()) : FText::GetEmpty());
		HotkeyText->SetVisibility(Key.IsValid() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void URTSTooltipWidget::SetTooltipContent(const FText& InTitle, const FRTSCommandState& InState, UTexture2D* InIcon)
{
	if (HotkeyText) HotkeyText->SetVisibility(ESlateVisibility::Collapsed);
    if (TitleText)
    {
        TitleText->SetText(InTitle);
        TitleText->SetVisibility(InTitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }

    if (DescriptionText)
    {
        DescriptionText->SetText(InState.Description);
        DescriptionText->SetVisibility(InState.Description.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }

	SetCostRows(InState.Costs);
	SetEffectRows(InState.EffectRows);
	if (DurationText)
	{
		FNumberFormattingOptions Format;
		Format.SetMaximumFractionalDigits(1);
		DurationText->SetText(InState.DurationSeconds > 0.0f
			? FText::Format(FText::FromString(TEXT("{0} s")), FText::AsNumber(InState.DurationSeconds, &Format)) : FText::GetEmpty());
	}
	if (DurationRow) DurationRow->SetVisibility(InState.DurationSeconds > 0.0f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (RequirementsText)
	{
		RequirementsText->SetText(InState.RequirementDescription);
		RequirementsText->SetVisibility(InState.RequirementDescription.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (StatusText)
	{
		StatusText->SetText(InState.StatusDescription);
		StatusText->SetVisibility(InState.StatusDescription.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

    if (IconImage)
    {
        if (InIcon)
        {
            IconImage->SetBrushFromTexture(InIcon);
            IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        else
        {
            IconImage->SetVisibility(ESlateVisibility::Collapsed);
        }
    }

}

void URTSTooltipWidget::SetCostRows(const TArray<FRTSCommandCost>& Costs)
{
	if (!CostsBox) return;
	int32 RowIndex = 0;
	for (const FRTSCommandCost& Cost : Costs)
	{
		if (Cost.Amount <= 0) continue;
		UHorizontalBox* Row = Cast<UHorizontalBox>(CostsBox->GetChildAt(RowIndex));
		if (!Row)
		{
			Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			Row->AddChildToHorizontalBox(WidgetTree->ConstructWidget<UImage>());
			UTextBlock* Value = WidgetTree->ConstructWidget<UTextBlock>();
			FSlateFontInfo Font = Value->GetFont();
			Font.Size = CostFontSize;
			Font.TypefaceFontName = TEXT("Bold");
			Value->SetFont(Font);
			UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(Value);
			ValueSlot->SetPadding(FMargin(6.0f, 0.0f, 16.0f, 0.0f));
			ValueSlot->SetVerticalAlignment(VAlign_Center);
			CostsBox->AddChildToHorizontalBox(Row);
		}
		UImage* Icon = CastChecked<UImage>(Row->GetChildAt(0));
		UTexture2D* Texture = ValueIcons.FindRef(Cost.ResourceId);
		Icon->SetBrushFromTexture(Texture);
		Icon->SetDesiredSizeOverride(FVector2D(36.0f));
		Icon->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		UTextBlock* Value = CastChecked<UTextBlock>(Row->GetChildAt(1));
		Value->SetText(FText::AsNumber(Cost.Amount));
		Value->SetColorAndOpacity(FSlateColor(CostColor));
		Row->SetVisibility(ESlateVisibility::HitTestInvisible);
		++RowIndex;
	}
	for (int32 Index = RowIndex; Index < CostsBox->GetChildrenCount(); ++Index)
		CostsBox->GetChildAt(Index)->SetVisibility(ESlateVisibility::Collapsed);
	CostsBox->SetVisibility(RowIndex > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void URTSTooltipWidget::SetEffectRows(const TArray<FRTSCommandTooltipRow>& Rows)
{
	if (!EffectRowsBox) return;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		UHorizontalBox* Row = Cast<UHorizontalBox>(EffectRowsBox->GetChildAt(Index));
		if (!Row)
		{
			Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			Row->AddChildToHorizontalBox(WidgetTree->ConstructWidget<UImage>())->SetPadding(FMargin(0, 0, 8, 0));
			for (int32 Cell = 0; Cell < 2; ++Cell)
			{
				UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
				FSlateFontInfo Font = Text->GetFont();
				Font.Size = DescriptionFontSize;
				if (Cell == 1) Font.TypefaceFontName = TEXT("Bold");
				Text->SetFont(Font);
				Text->SetColorAndOpacity(FSlateColor(Cell == 1 ? CostColor : DescriptionColor));
				Text->SetAutoWrapText(Cell == 0);
				UHorizontalBoxSlot* CellSlot = Row->AddChildToHorizontalBox(Text);
				CellSlot->SetVerticalAlignment(VAlign_Center);
				if (Cell == 0) CellSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				else CellSlot->SetPadding(FMargin(16, 0, 0, 0));
			}
			EffectRowsBox->AddChildToVerticalBox(Row);
		}
		UImage* Icon = CastChecked<UImage>(Row->GetChildAt(0));
		UTexture2D* Texture = ValueIcons.FindRef(Rows[Index].IconId);
		Icon->SetBrushFromTexture(Texture);
		Icon->SetDesiredSizeOverride(FVector2D(32.0f));
		Icon->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		CastChecked<UTextBlock>(Row->GetChildAt(1))->SetText(Rows[Index].Label);
		CastChecked<UTextBlock>(Row->GetChildAt(2))->SetText(Rows[Index].Value);
		Row->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	for (int32 Index = Rows.Num(); Index < EffectRowsBox->GetChildrenCount(); ++Index)
		EffectRowsBox->GetChildAt(Index)->SetVisibility(ESlateVisibility::Collapsed);
	EffectRowsBox->SetVisibility(Rows.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}
