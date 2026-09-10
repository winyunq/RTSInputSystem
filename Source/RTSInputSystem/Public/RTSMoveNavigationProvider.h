#pragma once

#include "CoreMinimal.h"
#include "MassAPIStructs.h"
#include "Tasks/MassBattleBPTaskAgentsMoveTo.h"

class UWorld;
class UMassBattleAgentConfigDataAsset;

/** Command-authoritative navigation scale. Camera/minimap state is resolved by
 * the issuing client once and carried as this stable hint. */
enum class ERTSMoveNavigationScale : uint8
{
	Auto,
	Tactical,
	Strategic
};

/** One native movement batch sharing one navigation source and one goal. */
struct RTSINPUTSYSTEM_API FRTSMoveNavigationBatch
{
	TArray<FEntityHandle> Entities;
	FVector Goal = FVector::ZeroVector;
	FMBMoveNavigation Navigation;

	// Optional strategic city chain, also supplied through Navigation.CustomPaths
	// for AgentsMoveTo to advance; Goal remains the exact final destination.
	TArray<FVector> StrategicCityWaypoints;
	float StrategicCityArrivalRadiusUU = 0.0f;
	/** Static city-graph component whose ingress direction field owns this batch. */
	int32 StrategicCityComponentKey = INDEX_NONE;
	/** First city reached through the shared ingress direction field. */
	int32 StrategicEntryCityIndex = INDEX_NONE;
};

// Command-time navigation extension. Implementations build navigation data;
// Native MoveTo/ChaseAttack tasks retain ownership of targeting and completion.
class RTSINPUTSYSTEM_API IRTSMoveNavigationProvider
{
public:
	virtual ~IRTSMoveNavigationProvider() = default;

	virtual bool BuildMoveNavigationBatches(
		const TArray<FEntityHandle>& Entities,
		const FVector& RequestedGoal,
		TArray<FRTSMoveNavigationBatch>& OutBatches,
		ERTSMoveNavigationScale NavigationScale) = 0;

	/** Called after the native MoveTo or ChaseAttack task has published its owner ID. */
	virtual void OnMoveNavigationBatchActivated(
		const FRTSMoveNavigationBatch& Batch)
	{
	}

	/**
	 * Projects a complete spawn formation into the unit's legal movement domain.
	 * Providers must return one canonical anchor so every member keeps its
	 * authored formation offset. The default keeps generic RTSInput behavior.
	 */
	virtual bool ResolveInitialSpawnLocation(
		const UMassBattleAgentConfigDataAsset* AgentConfig,
		const FVector& RequestedLocation,
		float FormationRadiusUU,
		FVector& OutLocation)
	{
		OutLocation = RequestedLocation;
		return true;
	}
};

/** World-scoped provider registry shared by player, production and AI orders. */
class RTSINPUTSYSTEM_API FRTSMoveNavigationProviderRegistry
{
public:
	static void Register(
		UWorld* World,
		IRTSMoveNavigationProvider* Provider);
	static void Unregister(
		UWorld* World,
		IRTSMoveNavigationProvider* Provider);
	static bool BuildBatches(
		UObject* WorldContext,
		const TArray<FEntityHandle>& Entities,
		const FVector& RequestedGoal,
		TArray<FRTSMoveNavigationBatch>& OutBatches,
		ERTSMoveNavigationScale NavigationScale =
			ERTSMoveNavigationScale::Auto);
	static void NotifyBatchActivated(
		UObject* WorldContext,
		const FRTSMoveNavigationBatch& Batch);
	/** Binds existing terrain navigation after a native ChaseAttack task activates. */
	static void BindChaseTaskNavigation(
		UObject* WorldContext,
		const TArray<FEntityHandle>& Entities,
		FEntityHandle Target,
		uint32 ChaseTaskID);
	static bool ResolveInitialSpawnLocation(
		UObject* WorldContext,
		const UMassBattleAgentConfigDataAsset* AgentConfig,
		const FVector& RequestedLocation,
		float FormationRadiusUU,
		FVector& OutLocation);
};
