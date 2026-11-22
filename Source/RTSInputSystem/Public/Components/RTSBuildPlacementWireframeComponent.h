// Copyright 2024 Jesus Bracho All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "RTSBuildPlacementWireframeComponent.generated.h"

/**
 * Runtime-only building ghost used during placement.
 *
 * The component deliberately renders the authored static mesh as geometry
 * wireframe, leaving the footprint and the surrounding guidance grid visible
 * as independent ground layers.
 * @brief 为放置预览生成线框绘制。
 * @details 组件携带放置线框颜色，渲染侧通过专用场景代理读取显示数据。
 * 
 * ```
 * 放置预览 + 线框颜色 → 组件 → 场景代理 → 线框绘制
 * ```
 */
UCLASS(Transient, NotBlueprintable)
class RTSINPUTSYSTEM_API URTSBuildPlacementWireframeComponent final : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	void SetPlacementWireframeColor(const FLinearColor& InColor);

protected:
	/**
	 * @brief 创建建筑放置预览的线框代理。
	 * @details 返回绑定当前组件的线框场景代理。
	 * @param NaniteMaterials 沿用静态网格代理签名的材质审查参数。
	 * @param bCreateNanite 沿用静态网格代理签名的 Nanite 选项。
	 * @return 本组件的线框场景代理。
	 */
	virtual FPrimitiveSceneProxy* CreateStaticMeshSceneProxy(
		Nanite::FMaterialAudit& NaniteMaterials,
		bool bCreateNanite) override;
};
