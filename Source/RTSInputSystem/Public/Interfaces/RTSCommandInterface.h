// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Data/RTSCommandGridAsset.h"
#include "Commands/RTSCommandRequirements.h"
#include "RTSCommandInterface.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class URTSCommandInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for Actors or Components that possess a Command Grid.
 * Both Unit Actors and Mass Entity wrappers/traits should implement this.
 */
class RTSINPUTSYSTEM_API IRTSCommandInterface
{
	GENERATED_BODY()

public:
	
	/** 
	 * Returns the Command Grid Asset for this object.
	 * This defines the buttons shown in the 15-grid UI.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
	class URTSCommandGridAsset* GetCommandGrid();

    // Returns time remaining in seconds. 0 = Ready. -1 = Not Applicable.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    float GetCooldownRemaining(FGameplayTag CommandTag);

    // Returns true if auto-cast is active for this command.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    bool IsAutoCastEnabled(FGameplayTag CommandTag);

    // Toggles auto-cast state. 
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void ToggleAutoCast(FGameplayTag CommandTag);

    /** Read-only command result shared by the command button and tooltip. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    FRTSCommandState QueryCommandState(FGameplayTag CommandTag, FName SourceId = NAME_None);

    /** State objects read by this command; UI retains its original dependency subscriptions. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void GetCommandStateDependencies(FGameplayTag CommandTag, FName SourceId, TArray<UObject*>& OutDependencies);

    /** Supplies existing state facts; the common evaluator owns condition composition. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    bool QueryCommandRequirementFact(const FRTSRequirementNode& Node, FName SourceId, FRTSRequirementFact& OutFact);

    // Returns true if the command can be performed given current state/requirements.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    bool IsCommandAvailable(FGameplayTag CommandTag, FName SourceId = NAME_None);

    /** Optional live description from the command owner. Empty keeps the authored button tooltip. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    FText GetCommandDescription(FGameplayTag CommandTag, FName SourceId = NAME_None);

    // Executes the command with no target (Instant)
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void ExecuteCommand(FGameplayTag CommandTag);

    // Executes the command with a target location
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void ExecuteCommandWithLocation(FGameplayTag CommandTag, FVector TargetLocation);

    // Executes the command with a target actor or entity
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void ExecuteCommandWithTarget(FGameplayTag CommandTag, AActor* TargetActor);

    /** 
     * Sets a specific grid asset to be displayed for this actor. 
     * Used for sub-menus / hierarchical navigation.
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "RTS Command")
    void SetCommandGrid(class URTSCommandGridAsset* NewGrid);
};
