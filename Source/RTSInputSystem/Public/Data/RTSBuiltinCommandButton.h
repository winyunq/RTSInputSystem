// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/RTSCommandButton.h"
#include "RTSBuiltinCommandButton.generated.h"

/**
 * Base class for "Code-Defined" Command Buttons.
 * Users can subclass this in C++ to define a button without creating an asset file.
 * The Default Object (CDO) of the subclass will be used as the button data.
 * 
 * Example:
 * UCLASS()
 * class URTSCmd_BuildFactory : public URTSBuiltinCommandButton
 * {
 *     virtual void PostInitProperties() override; // Set Defaults here
 * };
 * @brief 为 C++ 内置按钮补齐缺省命令标签。
 * @details 派生按钮可以配置自己的命令标签。缺省时通过类名生成标签并注册。
 * 
 * ```
 * 派生按钮类名 → 去除命名前缀 → RTS.Command.Builtin 标签
 * 已配置标签 → 直接沿用
 * ```
 */
UCLASS(Abstract, Blueprintable)
class RTSINPUTSYSTEM_API URTSBuiltinCommandButton : public URTSCommandButton
{
	GENERATED_BODY()
	
public:
    /**
     * @brief 在内置按钮属性初始化后补齐默认标签。
     * @details 按钮缺少有效标签时用类名去掉命名前缀，登记为 RTS.Command.Builtin 的子标签，再写入 CommandTag。
     */
    virtual void PostInitProperties() override;
};
