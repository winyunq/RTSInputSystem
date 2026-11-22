// Copyright Winyunq, 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "RTSHUD.h"
#include "RTSMinimalHUD.generated.h"

class URTSSelector;

/**
 * Dependency-free reference HUD assembled entirely from RTSInputSystem native
 * widgets. It deliberately contains no game-specific missions, resources,
 * factions, units, technology trees, or FogOfWar renderer.
 * @brief 生成紧凑版 HUD 并调整视口内各面板的尺寸。
 * @details 仅在逻辑尺寸或缩放量变化时调整布局。
 * 
 * ```
 * 本地玩家 → ARTSMinimalHUD → 创建控件
 * 安全区尺寸 + 视口缩放 → NativeTick → 更新紧凑布局
 * ```
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RTS Minimal HUD Widget"))
class RTSINPUTSYSTEM_API URTSMinimalHUDWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	/**
	 * @brief 在视口尺寸或缩放发生变化时更新紧凑布局。
	 * @details 从安全区尺寸和视口缩放计算逻辑尺寸。与缓存值不同才设置面板尺寸、正反方向的折行布局及标签样式。
	 * @param MyGeometry 传给父类更新的布局几何。
	 * @param InDeltaTime 传给父类更新的帧间隔。
	 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	class UScaleBox* CompactHudScale;
	UPROPERTY(Transient)
	class USizeBox* CompactHudSize;
	FVector2D CompactLogicalSize = FVector2D::ZeroVector;
	float CompactViewportScale = 0.0f;
	float CompactLayoutScale = 0.0f;
};

/** AHUD entry point that displays URTSMinimalHUDWidget and retains marquee selection. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RTS Minimal HUD"))
class RTSINPUTSYSTEM_API ARTSMinimalHUD : public ARTSHUD
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "RTS Minimal HUD")
	TSubclassOf<URTSMinimalHUDWidget> MinimalHUDWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<URTSMinimalHUDWidget> MinimalHUDWidget;
};

/** Minimal controller that installs the selector and plugin input actions. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RTS Minimal Player Controller"))
class RTSINPUTSYSTEM_API ARTSMinimalPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARTSMinimalPlayerController();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS Input")
	TObjectPtr<URTSSelector> RTSSelector;
};

/**
 * @brief Start a test map with the minimal RTS HUD and player controller. / 在测试地图中启用最小 RTS HUD 和玩家控制器。
 * @details Enable RTS Input System and its plugin dependencies, then choose RTS Minimal Game Mode in World Settings → GameMode Override. The mode selects ARTSMinimalHUD and ARTSMinimalPlayerController. For an existing GameMode, use these classes as references when wiring your own HUD and controller. / 启用 RTS Input System 及其插件依赖，在测试地图的「世界设置 → 游戏模式覆盖」中选择 RTS Minimal Game Mode。该模式会设置 ARTSMinimalHUD 和 ARTSMinimalPlayerController。已有 GameMode 的项目可参考这两个类接入自己的 HUD 和控制器。
 * @par Quick start / 快速接入
 * Play the map, select an eligible unit, and check that the selection is reflected in the HUD. To test a unit-specific command panel, give that unit a command grid with at least one applicable command. / 进入测试地图，选择一个可选单位，确认 HUD 随选区更新。要验证单位自己的命令面板，为该单位配置包含至少一项适用命令的网格。
 * @ProductDocument
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "RTS Minimal Game Mode"))
class RTSINPUTSYSTEM_API ARTSMinimalGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARTSMinimalGameMode();
};
