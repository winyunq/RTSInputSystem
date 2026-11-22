// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSSelectionStructs.h"
#include "RTSSelectionSubsystem.h"
#include "RTSActiveGroupWidget.generated.h"

class AMassBattleAgentRenderer;
class URTSUnitIconWidget;
class UImage;
class UNiagaraComponent;
class UNiagaraSystem;
class UPointLightComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class USoundConcurrency;

/**
 * A standalone widget that displays the currently active sub-group (Leader/Avatar).
 * Listen to RTSSelectionSubsystem directly.
 * @brief 显示活动组选中单位并呈现选区和发令反馈。
 * @details 根据选区视图选择代表单位。命令反馈回调使用同一视图选择播报者。
 * 
 * ```
 * 选区视图 → 活动组代表单位 → 资料 / 肖像
 * 命令确认 → 代表单位 → 界面事件 / 本地语音
 * ```
 */
UCLASS(BlueprintType, Blueprintable)
class RTSINPUTSYSTEM_API URTSActiveGroupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Prepare portrait resources during HUD/world loading; repeated calls reuse them. */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Portrait")
	bool PrepareLivePortraitResources();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RTS Selection|Portrait", meta = (ClampMin = "1.0", ClampMax = "1.08"))
	float PortraitFramingPadding = 1.04f;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> PortraitCaptureComponent;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> PortraitRenderTarget;

	/** One render-only Niagara slot copied from the selected unit's existing renderer. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> PortraitPreviewActor;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> PortraitPreviewComponent;

	/** One retained preview per actual Niagara system, shared by units using that system. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UNiagaraSystem>, TObjectPtr<UNiagaraComponent>> PortraitPreviewComponents;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> PortraitKeyLightComponent;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> PortraitFillLightComponent;

	/** Existing selected unit supplies renderer and animation data. Never owned or mutated. */
	FEntityHandle PortraitSourceEntity;
	TWeakObjectPtr<AMassBattleAgentRenderer> PortraitSourceRenderer;
	TWeakObjectPtr<UWorld> PortraitPreparedWorld;
	FDelegateHandle PortraitActorSpawnedHandle;

	/**
	 * @brief 以选中 Mass 单位作为实时头像的采样来源。
	 * @details 解析单位的渲染批次与实例，复用或准备头像预览组件，再用实例数据刷新预览。
	 * @param Data 当前代表单位的 Mass 句柄及展示资料。
	 * @return 实时头像已从当前单位建立时为 true。
	 */
	bool StartSelectedUnitPortrait(const FRTSUnitData& Data);
	bool EnsurePortraitCaptureResources();
	void PreparePortraitPreviewComponent(AMassBattleAgentRenderer* Renderer);
	void HandlePortraitActorSpawned(AActor* Actor);
	bool EnsurePortraitPreviewComponent(AMassBattleAgentRenderer* Renderer);
	bool UpdatePortraitPreview();
	void ApplyLivePortraitBrush();
	void DestroyPortraitPreview();
	void ClearPortraitSource();
	void StopLivePortrait();
	void CapturePortraitFrame();
	void ApplyStaticPortrait(UTexture2D* Texture);
	/**
	 * @brief 为一条确认反馈发送界面事件并选择播报音。
	 * @details 单组视图直接使用代表单位，列表视图优先读取活动组的单位。先发出 OnUnitCommandAcknowledged，再尝试命令专用音，最后尝试单位通用确认音。
	 * @param Context 收到的分组视图。
	 * @param CommandTag 已提交的命令标签。
	 * @note 目标坐标和两个状态参数来自统一反馈事件。此控件使用组视图和命令标签。
	 */
	void HandleCommandFeedback(const FRTSSelectionView& Context, FGameplayTag CommandTag, FVector, bool, bool);

	UPROPERTY(Transient)
	FRTSUnitData CommandFeedbackUnit;

	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> CommandVoiceConcurrency;

	/** Receives each player order even when the unit has no confirmation audio yet. */
	UFUNCTION(BlueprintImplementableEvent, Category = "RTS Selection|Feedback")
	void OnUnitCommandAcknowledged(FGameplayTag CommandTag, const FRTSUnitData& UnitData);
	
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
