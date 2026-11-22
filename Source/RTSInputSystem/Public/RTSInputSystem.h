// Copyright 2024 Jesus Bracho All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include "Modules/ModuleManager.h"

/**
 * @brief 注册 RTSInputSystem 模块的原生命令标签。
 * @details 模块启动时注册移动、攻击、建造及城市命令等标签，供对应面板和发令入口使用。
 *
 * ```
 * 模块加载 → StartupModule → GameplayTagsManager
 *                              └→ 命令定义 / 命令匹配
 * ```
 */
class FRTSInputSystemModule : public IModuleInterface
{
public:
	/**
	 * @brief 启动时注册 RTS 输入层使用的原生命令标签。
	 * @details 移动、攻击、停止、坚守、巡逻、建造和城市命令的定义通过 GameplayTagsManager 注册，便于配置和命令入口以同一名称引用。
	 */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

DECLARE_LOG_CATEGORY_EXTERN(LogRTSInputSystem, Log, All);
