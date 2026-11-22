// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Components/PanelSlot.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"
#include "UObject/SoftObjectPtr.h"
#include "RTSFrameBox.generated.h"

class SBorder;
class UMaterialInterface;

UCLASS()
class RTSINPUTSYSTEM_API URTSFrameBoxSlot : public UPanelSlot
{
	GENERATED_BODY()

public:
	URTSFrameBoxSlot(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|RTS Frame Slot")
	FMargin Padding;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|RTS Frame Slot")
	TEnumAsByte<EHorizontalAlignment> HorizontalAlignment;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|RTS Frame Slot")
	TEnumAsByte<EVerticalAlignment> VerticalAlignment;

	UFUNCTION(BlueprintCallable, Category = "Layout|RTS Frame Slot")
	void SetPadding(FMargin InPadding);

	UFUNCTION(BlueprintCallable, Category = "Layout|RTS Frame Slot")
	void SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment);

	UFUNCTION(BlueprintCallable, Category = "Layout|RTS Frame Slot")
	void SetVerticalAlignment(EVerticalAlignment InVerticalAlignment);

	void BuildSlot(TSharedRef<SBorder> InBorder);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TWeakPtr<SBorder> Border;

	void ApplySlotProperties();
};

/**
 * Single-slot RTS frame container.
 *
 * This is a frontend UMG control: it wraps one child with a material-driven,
 * nine-sliced frame. It owns no game state and does not create gameplay UI.
 * @brief 为 HUD 提供可配置的边框和内容容器。
 * @details 从材质与切片尺寸构造画刷，将边框、填充、内边距和对齐应用到 Slate 容器。
 * 
 * ```
 * 材质 + 切片 → Slate 画刷
 * 外框 / 内框 / 填充 + 对齐 → 内容容器
 * ```
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RTS Frame Box"))
class RTSINPUTSYSTEM_API URTSFrameBox : public UContentWidget
{
	GENERATED_BODY()

public:
	URTSFrameBox(const FObjectInitializer& ObjectInitializer);

	/** Material used by the outer metal frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Materials")
	TSoftObjectPtr<UMaterialInterface> OuterFrameMaterial;

	/** Material used by the inner accent frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Materials")
	TSoftObjectPtr<UMaterialInterface> InnerFrameMaterial;

	/** Material used by the panel fill behind the content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Materials")
	TSoftObjectPtr<UMaterialInterface> PanelFillMaterial;

	/** Source material size used for nine-slice margins. Keep this power-of-two. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Nine Slice", meta = (ClampMin = "1.0"))
	FVector2D MaterialBaseSize = FVector2D(256.0f, 256.0f);

	/** Outer frame slice in pixels. Keep this power-of-two. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Nine Slice", meta = (ClampMin = "0.0"))
	FMargin OuterSlice = FMargin(32.0f);

	/** Inner frame slice in pixels. Keep this power-of-two. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Nine Slice", meta = (ClampMin = "0.0"))
	FMargin InnerSlice = FMargin(8.0f);

	/** Fill slice in pixels, usually 0 for a fully stretched panel material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Nine Slice", meta = (ClampMin = "0.0"))
	FMargin FillSlice = FMargin(0.0f);

	/** Space between the outer frame and the inner frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Layout")
	FMargin OuterPadding = FMargin(32.0f);

	/** Space between the inner frame and the panel fill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Layout")
	FMargin InnerPadding = FMargin(8.0f);

	/** Space between the panel fill and the child content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Layout")
	FMargin ContentPadding = FMargin(4.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Layout")
	TEnumAsByte<EHorizontalAlignment> HorizontalAlignment = HAlign_Fill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Layout")
	TEnumAsByte<EVerticalAlignment> VerticalAlignment = VAlign_Fill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Appearance", meta = (sRGB = "true"))
	FLinearColor OuterTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Appearance", meta = (sRGB = "true"))
	FLinearColor InnerTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Frame|Appearance", meta = (sRGB = "true"))
	FLinearColor FillTint = FLinearColor::White;

	UFUNCTION(BlueprintCallable, Category = "RTS Frame")
	void SetFramePadding(FMargin InOuterPadding, FMargin InInnerPadding, FMargin InContentPadding);

	UFUNCTION(BlueprintCallable, Category = "RTS Frame")
	void SetContentAlignment(EHorizontalAlignment InHorizontalAlignment, EVerticalAlignment InVerticalAlignment);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual UClass* GetSlotClass() const override;
	virtual void OnSlotAdded(UPanelSlot* InSlot) override;
	virtual void OnSlotRemoved(UPanelSlot* InSlot) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

private:
	TSharedPtr<SBorder> OuterBorder;
	TSharedPtr<SBorder> InnerBorder;
	TSharedPtr<SBorder> FillBorder;

	// FSlateBrush owns the synchronously loaded material through ResourceObject.
	// These must be reflected so GC visits that nested UObject reference while
	// Slate keeps the brush alive across long gameplay sessions.
	UPROPERTY(Transient)
	FSlateBrush OuterBrush;

	UPROPERTY(Transient)
	FSlateBrush InnerBrush;

	UPROPERTY(Transient)
	FSlateBrush FillBrush;

	void RebuildBrushes();
	void ApplySlateStyle();
	void EnsureDefaultMaterials();
	FSlateBrush MakeMaterialBoxBrush(const TSoftObjectPtr<UMaterialInterface>& Material, const FMargin& SlicePixels, const FLinearColor& Tint) const;
	FMargin SlicePixelsToMargin(const FMargin& SlicePixels) const;
};
