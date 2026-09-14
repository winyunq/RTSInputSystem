// Copyright 2026 Winyunq. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commands/RTSTimedCommand.h"
#include "UObject/Interface.h"
#include "RTSCommandProgressController.generated.h"

/**
 * Optional action endpoint for a common command-progress item.
 *
 * The selection UI knows only the stable item id. Gameplay systems remain
 * responsible for authority, refunds, queue promotion, and state refresh.
 */
UINTERFACE(BlueprintType)
class RTSINPUTSYSTEM_API URTSCommandProgressController : public UInterface
{
	GENERATED_BODY()
};

class RTSINPUTSYSTEM_API IRTSCommandProgressController
{
	GENERATED_BODY()

public:
	/** Validates and prepares a task before it enters the existing capacity queue. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Progress")
	bool PrepareCommandProgressItem(UPARAM(ref) FRTSTimedCommandInstance& Item, FName SourceId = NAME_None);
	virtual bool PrepareCommandProgressItem_Implementation(FRTSTimedCommandInstance& Item, FName SourceId) { return true; }

	/** Called once when the existing executor promotes a prepared item into a lane. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Progress")
	void StartCommandProgressItem(UPARAM(ref) FRTSTimedCommandInstance& Item);
	virtual void StartCommandProgressItem_Implementation(FRTSTimedCommandInstance& Item) {}

	/** Applies the domain result after the executor has removed the finished/cancelled task. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Progress")
	void ResolveCommandProgressItem(const FRTSTimedCommandInstance& Item, bool bCancelled);
	virtual void ResolveCommandProgressItem_Implementation(const FRTSTimedCommandInstance& Item, bool bCancelled) {}

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Progress")
	void RequestCancelCommandProgressItem(FName ItemId);
};
