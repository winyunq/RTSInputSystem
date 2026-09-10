// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commands/RTSTimedCommand.h"
#include "GameplayTagContainer.h"
#include "MassEntityTypes.h"
#include "MassAPIStructs.h"
#include "UObject/SoftObjectPtr.h"
#include "RTSSelectionStructs.generated.h"

class UTexture2D;
class USoundBase;
class URTSCommandButton;
class URTSCommandGridAsset;

UENUM(BlueprintType)
enum class ERTSSelectionMode : uint8
{
	Empty   = 0 UMETA(DisplayName = "No Selection"),
	Single  = 1 UMETA(DisplayName = "Single Unit"),
	List    = 2 UMETA(DisplayName = "Unit List"),
	Summary = 3 UMETA(DisplayName = "Group Summary")
};

UENUM(BlueprintType)
enum class ERTSSelectionModifier : uint8
{
	Replace     UMETA(DisplayName = "Replace Selection"),
	Add         UMETA(DisplayName = "Add to Selection"),
	Remove      UMETA(DisplayName = "Remove from Selection")
};

/** How the current selection is written into a persistent player control group. */
UENUM(BlueprintType)
enum class ERTSControlGroupAssignmentMode : uint8
{
	Replace          UMETA(DisplayName = "Replace Group"),
	ToggleMembership UMETA(DisplayName = "Add / Remove Selection"),
	StealAndReplace  UMETA(DisplayName = "Steal From Other Groups And Replace")
};

/** World-wide quick-selection request used by TopSelect and reusable UMG buttons. */
USTRUCT(BlueprintType)
struct FRTSSelectionQuery
{
	GENERATED_BODY()

	/** Hierarchical category. For example, Army.Ground also matches Infantry, Armor, and Artillery. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Quick Selection")
	FGameplayTag RequiredSelectionTag;

	/** Hierarchical category that must not be present. Army shortcuts use Structure here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Quick Selection")
	FGameplayTag ExcludedSelectionTag;

	/** Only include units currently marked idle/free. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Quick Selection")
	bool bIdleOnly = false;

	/** Legacy compatibility only. Mass entities are the default and primary selection source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Quick Selection")
	bool bIncludeActorUnits = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Quick Selection")
	bool bIncludeMassEntities = true;
};

/**
 * Unified data structure representing a single selectable unit OR a group summary.
 */
USTRUCT(BlueprintType)
struct FRTSUnitData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FString GroupKey;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FString TypeKey;

	/** Exact source unit asset used to resolve the same portrait and 3D preview everywhere. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FString UnitAssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	int32 SubTypeIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FGameplayTag UnitTypeTag;

	/** Hierarchical categories used by TopSelect and other world-wide quick-selection controls. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FGameplayTagContainer SelectionTags;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FString Role;

	/** Presentation labels supplied by the existing unit/force data. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Combat")
	FString OrganizationLabel;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Combat")
	FString UnitCategory;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Combat")
	bool bHasWeapon = false;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Combat")
	float WeaponDamage = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Combat")
	float WeaponRange = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Combat")
	float WeaponPeriod = 0.0f;

	/** Actual normal-damage resistance (0..1), not an invented armor-point scale. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Combat")
	float ArmorReduction = 0.0f;

	/** Compact icon used by selection grids and summary cells. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	UTexture2D* Icon = nullptr;

	/** Canonical portrait used by production buttons, production progress, detail, and portrait windows. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	UTexture2D* Portrait = nullptr;

	/** Lightweight announcer cue key for UI/audio systems. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FName AnnouncerId;

	/** Optional selected voice asset. Soft so protocol data can be pushed without forcing all audio loaded. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	TSoftObjectPtr<USoundBase> SelectionSound;

	/** Optional command acknowledgement voice asset. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	TSoftObjectPtr<USoundBase> ConfirmationSound;

	/** Command grid associated with this type, when one is authored as an asset. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	TSoftObjectPtr<URTSCommandGridAsset> CommandGrid;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	int32 Count = 1; // 1 for individual unit, >1 for group summary

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float Health = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float MaxHealth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float Energy = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float MaxEnergy = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float Shield = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	float MaxShield = 0.0f;

	/** Current long-running action, such as training a unit or constructing a building. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Activity")
	bool bHasActivity = false;

	/**
	 * Authoritative presentation model for all long-running commands. Multiple
	 * active lanes and queued entries remain separate instead of being averaged.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Activity")
	TArray<FRTSTimedCommandInstance> CommandProgressItems;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Activity")
	FText ActivityLabel;

	/** Normalized completion in the 0-1 range. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Activity")
	float ActivityProgress = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Activity")
	float ActivityRemainingSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Activity")
	float ActivityDurationSeconds = 0.0f;

	/** Total active and queued orders represented by this activity. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Activity")
	int32 ActivityQueueCount = 0;

	/** True when this selection item owns one or more production lanes. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Production")
	bool bHasProductionCapacity = false;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Production")
	int32 ProductionBusyLanes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Production")
	int32 ProductionTotalLanes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection|Production")
	int32 ProductionQueuedOrders = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	bool bIsMassEntity = false;

	// Optional: Raw pointers/handles if UI needs to command them back
	// Only valid if Count == 1
	UPROPERTY()
	AActor* ActorPtr = nullptr;

	UPROPERTY()
	FEntityHandle EntityHandle;


	// Default constructor for "Empty/Unknown" state
	FRTSUnitData()
	{
		Name = TEXT("Unknown");
		GroupKey = TEXT("Unknown");
	}
};

/**
 * The snapshot sent to the UI.
 */
USTRUCT(BlueprintType)
struct FRTSSelectionView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	ERTSSelectionMode Mode = ERTSSelectionMode::Empty;


	// Used only when Mode == Single. Contains detailed info.
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FRTSUnitData SingleUnit;

	// Used when Mode == List (individual items, Count=1) or Summary (grouped items, Count>1).
	// For Single, this may contain the single unit for consumers that need the current item/style.
	// For Empty, this must be empty.
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	TArray<FRTSUnitData> Items;

	// The key of the currently active sub-group (e.g. "Marine")
	// Used for highlighting and tab-cycling
	UPROPERTY(BlueprintReadOnly, Category = "RTS Selection")
	FString ActiveGroupKey;
};

/** One unit-type row inside a persistent control group. */
USTRUCT(BlueprintType)
struct FRTSControlGroupComposition
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	FRTSUnitData UnitType;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	int32 Count = 0;
};

/** UI snapshot for one of the player's 0-9 control groups. */
USTRUCT(BlueprintType)
struct FRTSControlGroupView
{
	GENERATED_BODY()

	/** The keyboard digit represented by this group (0-9). */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	int32 GroupIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	bool bAssigned = false;

	/** True when the current selection exactly matches this group. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	int32 UnitCount = 0;

	/** Representative type used for the compact slot portrait. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	FRTSUnitData RepresentativeUnit;

	/** Per-type counts for richer StarCraft-style UMG presentation and tooltips. */
	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	TArray<FRTSControlGroupComposition> Composition;
};

/** Complete control-group strip snapshot, ordered 1-9 then 0. */
USTRUCT(BlueprintType)
struct FRTSControlGroupsView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	TArray<FRTSControlGroupView> Groups;

	UPROPERTY(BlueprintReadOnly, Category = "RTS Control Groups")
	int32 ActiveGroupIndex = INDEX_NONE;
};
