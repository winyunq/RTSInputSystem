// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSSelectionStructs.h"
#include "RTSSelectionSubsystem.h"
#include "RTSActiveGroupWidget.generated.h"

class AActor;
class UMassBattleAgentComponent;
class UMassBattleAgentConfigDataAsset;
class UPointLightComponent;
class URTSUnitIconWidget;
class UImage;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * A standalone widget that displays the currently active sub-group (Leader/Avatar).
 * Listen to RTSSelectionSubsystem directly.
 */
UCLASS(BlueprintType, Blueprintable)
class RTSINPUTSYSTEM_API URTSActiveGroupWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	virtual void OnSelectionUpdated(const FRTSSelectionView& View);

	// Optional: If bound, we forward the data to this internal widget.
	// This allows users to wrap our logic in a text/border/etc.
	// Or users can just inherit this class in their WBP_Avatar
	UPROPERTY(meta = (BindWidgetOptional))
	URTSUnitIconWidget* GroupIcon;

	// Optional direct avatar image used by RTSAvatar.
	UPROPERTY(meta = (BindWidgetOptional))
	UImage* AvatarImage;

	/** Enables the independent SC2-style 3D portrait viewport. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RTS Selection|Portrait")
	bool bEnableLivePortrait = true;

	/** Golden-ratio portrait width for the 512-unit-tall RTSAvatar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RTS Selection|Portrait", meta = (ClampMin = "96.0", ClampMax = "512.0"))
	float PortraitPanelWidth = 316.0f;

	/** RTSAvatar height. ControlGrid remains an independent sibling widget. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RTS Selection|Portrait", meta = (ClampMin = "128.0", ClampMax = "512.0"))
	float PortraitPanelHeight = 512.0f;

	/** Vertical render-target resolution; width is derived from the panel aspect ratio. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RTS Selection|Portrait", meta = (ClampMin = "256", ClampMax = "1024"))
	int32 PortraitRenderTargetHeight = 512;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RTS Selection|Portrait", meta = (ClampMin = "10.0", ClampMax = "90.0"))
	float PortraitFieldOfView = 28.0f;

	/** Direction from a forward-facing unit toward the portrait camera. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RTS Selection|Portrait")
	FRotator PortraitViewDirection = FRotator(8.0f, 0.0f, 0.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RTS Selection|Portrait", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float PortraitFramingPadding = 1.25f;

	/** Remote stage used only by the standalone MassBattleFrame portrait entity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RTS Selection|Portrait")
	FVector PortraitPreviewLocation = FVector(1500000.0f, 1500000.0f, -500000.0f);

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> PortraitCaptureComponent;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> PortraitRenderTarget;

	/** Real MassBattleFrame entity host used by the portrait, matching the gallery render path. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> PortraitPreviewActor;

	UPROPERTY(Transient)
	TObjectPtr<UMassBattleAgentComponent> PortraitPreviewAgentComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMassBattleAgentConfigDataAsset> PortraitPreviewUnitConfig;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> PortraitKeyLightComponent;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> PortraitFillLightComponent;

	FString PortraitPreviewUnitAssetPath;

	bool StartMassPreviewPortrait(const FRTSUnitData& Data);
	bool EnsurePortraitCaptureResources();
	void ApplyLivePortraitBrush();
	void DestroyMassPreviewPortrait();
	void StopLivePortrait();
	void CapturePortraitFrame();
	void ApplyStaticPortrait(UTexture2D* Texture);
	
	// Optional: A text block for the name? (Or let GroupIcon handle it?)
	// Let's keep it simple: It mostly wraps functionality.

	/**
	 * Event fired when the active group data changes.
	 * Implement this in Blueprint to add custom logic (e.g. update 3D Avatar, play sound).
	 * @param Data The data of the active unit/group.
	 * @param bHasData True if there is a valid selection, False if empty.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "RTS Selection")
	void OnActiveGroupChanged(const FRTSUnitData& Data, bool bHasData);
};
