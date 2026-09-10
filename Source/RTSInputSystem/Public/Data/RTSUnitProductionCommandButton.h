// Copyright 2026 Winyunq. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/RTSBuiltinCommandButton.h"
#include "UObject/SoftObjectPtr.h"
#include "RTSUnitProductionCommandButton.generated.h"

/**
 * Shared production-button contract used by cities, barracks, and factories.
 *
 * The input module owns the transport-neutral fields that the UI needs:
 * what unit is produced, quantity, duration, spacing, and the inherited
 * low/high resource costs.  Production-system subclasses may add queueing
 * behavior without defining a second button data model.
 *
 * UnitConfig intentionally uses UObject here so RTSInputSystem does not take a
 * public dependency on MassBattle merely to describe a command button.
 */
UCLASS(Abstract, Blueprintable)
class RTSINPUTSYSTEM_API URTSUnitProductionCommandButton : public URTSBuiltinCommandButton
{
	GENERATED_BODY()

public:
	URTSUnitProductionCommandButton()
	{
		TargetType = ERTSCommandTargetType::Instant;
		Role = FText::FromString(TEXT("单位生产"));
		bAllowAutoCast = true;
		bIsResearch = true;
		bRepeatableResearch = true;
	}

	/** Stable identifier copied into the concrete production definition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production")
	FName UnitId;

	/** Concrete unit configuration, normally a MassBattle AgentConfig. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production")
	TSoftObjectPtr<UObject> UnitConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production", meta = (ClampMin = "1"))
	int32 Quantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production", meta = (ClampMin = "0.01"))
	float TrainTime = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production", meta = (ClampMin = "1.0"))
	float SpawnSpacing = 160.0f;
};
