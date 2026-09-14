// Copyright 2026 Winyunq. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Commands/RTSTimedCommand.h"
#include "RTSCommandProgressProvider.generated.h"

/**
 * Transport-neutral bridge between gameplay systems and the RTS selection UI.
 * Training, construction, research and national focuses all publish the same
 * progress-item shape; RTSInputSystem owns their shared presentation.
 */
UINTERFACE(BlueprintType)
class RTSINPUTSYSTEM_API URTSCommandProgressProvider : public UInterface
{
	GENERATED_BODY()
};

class RTSINPUTSYSTEM_API IRTSCommandProgressProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Progress")
	void GetCommandProgressItems(
		UPARAM(ref) TArray<FRTSTimedCommandInstance>& OutItems, FName SourceId = NAME_None) const;
};
