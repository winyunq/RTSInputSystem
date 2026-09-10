// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/RTSCommandButton.h"
#include "Data/RTSCommandGridAsset.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/Texture2D.h"
#include "GameplayTagContainer.h"
#include "Layout/Margin.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/SoftObjectPtr.h"
#include "RTSInputPanelSettings.generated.h"

class AActor;

USTRUCT(BlueprintType)
struct FRTSUnitAvatarDefinition
{
	GENERATED_BODY()

	/** Name shown in RTS selection UI. Array index maps to MassBattle FSubType.Index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Avatar")
	FString DisplayName;

	/** Optional portrait for the RTS avatar panel. Mass entities do not store this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Avatar")
	TSoftObjectPtr<UTexture2D> Avatar;
};

USTRUCT(BlueprintType)
struct FRTSCommandPanelSlot
{
	GENERATED_BODY()

	/** Keyboard shortcut for this command panel slot. Slots are 0-14, left-to-right, top-to-bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Command Panel")
	FName Hotkey;
};

USTRUCT(BlueprintType)
struct FRTSMassUnitCommandSlotDefinition
{
	GENERATED_BODY()

	/** Slot index in the 15-button command card. Missing indices remain empty/hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol", meta = (ClampMin = "0", ClampMax = "14"))
	int32 SlotIndex = -1;

	/** Logical command to issue when this button is clicked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FGameplayTag CommandTag;

	/** Config-friendly command tag name. Used when CommandTag is not set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FName CommandTagName;

	/** Optional command loadout opened by this button. When set, the button navigates instead of issuing CommandTag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FName SubMenuLoadoutId;

	/** Button title shown in tooltips and optional labels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FString DisplayName;

	/** Tooltip body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FString Description;

	/** Command targeting model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	ERTSCommandTargetType TargetType = ERTSCommandTargetType::Instant;

	/** Optional slot hotkey override. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FName Hotkey;

	/** Optional per-command icon. If unset, the button uses its text label until real artwork is authored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	TSoftObjectPtr<UTexture2D> Icon;

	/** If true, command availability checks may hide this button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	bool bHideIfUnavailable = false;
};

/** Reusable command card. Each button is the corresponding unit skill. */
USTRUCT(BlueprintType)
struct FRTSCommandLoadoutDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Command Loadout")
	FName LoadoutId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Command Loadout")
	TSoftObjectPtr<URTSCommandGridAsset> CommandGrid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Command Loadout")
	bool bIncludeDefaultUnitCommands = true;

	/** Sparse command-card entries. Unspecified slots stay empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Command Loadout")
	TArray<FRTSMassUnitCommandSlotDefinition> CommandSlots;

	/** Optional parent loadout. A Back button is generated automatically when this is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Command Loadout|Navigation")
	FName BackToLoadoutId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Command Loadout|Navigation", meta = (ClampMin = "0", ClampMax = "14"))
	int32 BackButtonSlotIndex = 14;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Command Loadout|Navigation")
	FString BackButtonDisplayName = TEXT("Back");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Command Loadout|Navigation")
	FString BackButtonDescription = TEXT("Return to the previous command card.");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Command Loadout|Navigation")
	FName BackButtonHotkey = FName(TEXT("B"));
};

USTRUCT(BlueprintType)
struct FRTSMassUnitTypeProtocol
{
	GENERATED_BODY()

	/** MassBattle FSubType.Index. If left at -1, the array index is used as a fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	int32 SubTypeIndex = INDEX_NONE;

	/**
	 * Optional exact MassBattle AgentConfig object path. Networked MassBattle stores
	 * this path in FNetworking.Key, so it remains unique even when legacy SubType
	 * numbers collide between countries or between units and structures.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FString UnitAssetPath;

	/** Stable type key used by UI grouping and future protocols. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FString TypeKey;

	/**
	 * Country-independent unit class implemented by this MassBattle subtype.
	 * Example: German, Japanese, and Soviet officer assets all use RTS.UnitClass.Officer,
	 * while retaining their own subtype, presentation, renderer, and combat statistics.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FGameplayTag UnitTypeTag;

	/** Explicit quick-selection categories. Empty entries receive sensible TypeKey/Role-based defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FGameplayTagContainer SelectionTags;

	/** Name shown in the selection panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FString DisplayName;

	/** Short role/class text for detail panels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FString Role;

	/** Compact icon used by selection grids and summary cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Canonical portrait used by production, progress, detail, and portrait-window UI. Falls back to Icon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	TSoftObjectPtr<UTexture2D> Portrait;

	/** Announcer cue key. Audio systems can resolve this now or later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FName AnnouncerId;

	/** Optional selected voice. Kept soft so selection does not force-load all unit audio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	TSoftObjectPtr<USoundBase> SelectionSound;

	/** Optional command acknowledgement voice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	TSoftObjectPtr<USoundBase> ConfirmationSound;

	/**
	 * Optional world-space range used by the selected-unit indicator.
	 * Zero reads MassBattle FAttack.Range and then falls back to the unit footprint.
	 * Buff/aura units can set this to their effect radius so selection communicates
	 * the gameplay range without drawing a second competing ring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol", meta = (ClampMin = "0.0", Units = "cm"))
	float SelectionRangeOverride = 0.0f;

	/** Reusable skill panel. Unit identity metadata does not create skills by itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	FName CommandLoadoutId;

	/** Legacy compatibility. New definitions should use CommandLoadoutId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	TSoftObjectPtr<URTSCommandGridAsset> CommandGrid;

	/** Legacy compatibility. New definitions should use CommandLoadoutId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	bool bUseDefaultCommandGrid = true;

	/** Legacy compatibility. New definitions should use CommandLoadoutId. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTS Mass Unit Protocol")
	TArray<FRTSMassUnitCommandSlotDefinition> CommandSlots;
};

UCLASS(Config=RTSInputSystem, DefaultConfig, BlueprintType, meta = (DisplayName = "RTS Input System"))
class RTSINPUTSYSTEM_API URTSInputPanelSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** RTS-only MassBattle avatar mapping. Array index maps directly to FSubType.Index. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Avatar")
	TArray<FRTSUnitAvatarDefinition> MassUnitAvatars;

	/** Comprehensive MassBattle FSubType.Index -> RTS UI/command/audio protocol mapping. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Mass Unit Protocol")
	TArray<FRTSMassUnitTypeProtocol> MassUnitTypeProtocols;

	/** Command panels are reusable capability sets and are intentionally separate from unit identity metadata. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Loadout")
	TArray<FRTSCommandLoadoutDefinition> CommandLoadouts;

	/** RTS command panel keyboard layout. Default is QWERT / ASDFG / ZXCVB. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Panel")
	TArray<FRTSCommandPanelSlot> CommandPanelSlots;

	/** Enables keyboard execution of visible command panel buttons. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Panel")
	bool bEnableCommandPanelHotkeys = true;

	/** Holding Shift and a targeted command hotkey for this long starts StarCraft-style rapid fire. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Panel", meta = (ClampMin = "0.0", Units = "s"))
	float CommandPanelHotkeyRepeatDelay = 1.0f / 3.0f;

	/** Interval between rapid-fire target confirmations after the hold delay. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Panel", meta = (ClampMin = "0.01", Units = "s"))
	float CommandPanelHotkeyRepeatInterval = 1.0f / 24.0f;

	/** Minimum font size applied to mission/objective rich text rows loaded from the shared style table. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Mission Panel", meta = (ClampMin = "1"))
	int32 MissionRichTextMinimumFontSize = 48;

	/** Selection counts above this value are exposed as grouped unit-type summaries instead of individual list items. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Panel", meta = (ClampMin = "1"))
	int32 SelectionSummaryThreshold = 16;

	/**
	 * Legacy compatibility toggle. Project move orders are submitted as Direct
	 * requests and the Landscape movement plugin owns navigation LOD.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Movement Navigation")
	bool bEnableAdaptiveMoveNavigation = true;

	// Legacy serialized value retained for asset/config compatibility. It is no
	// longer used to select a route; in particular, 4096uu is not a path grid.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Movement Navigation", meta = (ClampMin = "0.0"))
	float DirectMoveMaxDistance = 4096.0f;

	/** Number of rows generated for the RTS unit selection grid page. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Panel", meta = (ClampMin = "1"))
	int32 SelectionGridRows = 3;

	/** Number of columns generated for the RTS unit selection grid page. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Panel", meta = (ClampMin = "1"))
	int32 SelectionGridColumns = 8;

	/** Square pixel size used by each RTS unit portrait in the selection panel. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Panel", meta = (ClampMin = "1"))
	int32 SelectionIconSize = 128;

	/** Minimum height reserved while at least one control group is visible. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Panel", meta = (ClampMin = "0"))
	float SelectionPanelHeaderHeight = 68.0f;

	/** Padding inside the fixed UnitPanel shell around the generated selection grid. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Panel")
	FMargin SelectionPanelContentPadding = FMargin(16.0f, 4.0f, 16.0f, 4.0f);

	/** Number of persistent control-group slots shown in the UnitPanel header strip. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Formation List", meta = (ClampMin = "1"))
	int32 FormationListMaxSlots = 10;

	/** Fixed width of each control-group card; the strip distributes spare width between cards. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Formation List", meta = (ClampMin = "1"))
	int32 FormationListSlotWidth = 102;

	/** Height of each control-group card. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Formation List", meta = (ClampMin = "1"))
	int32 FormationListSlotHeight = 64;

	/** Gap below the fixed control-group strip. Horizontal gaps are derived from the panel width. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Formation List", meta = (ClampMin = "0"))
	float FormationListSlotGap = 4.0f;

	/** Enables StarCraft-style 0-9 control-group keyboard handling on RTSSelector. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Control Groups")
	bool bEnableControlGroupHotkeys = true;

	/** Maximum interval between two recalls of the same group before the camera centers on it. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Control Groups", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float ControlGroupDoubleTapTime = 0.3f;

	/** Shows the world-space placement footprint while a building command is active. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	bool bEnableHashGridSelectionPreview = true;

	/** Optional deferred decal material. Runtime grid lines remain available when this is unset. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	TSoftObjectPtr<UMaterialInterface> HashGridSelectionDecalMaterial;

	/** Logical map resolution in cells. 4096 x 16 UU produces the current 65536 UU-wide map. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	FIntPoint BuildGridMapResolution = FIntPoint(4096, 4096);

	/** World-space center of the logical build grid. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	FVector2D BuildGridWorldOrigin = FVector2D::ZeroVector;

	/** World-space size of one logical placement-grid cell. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "1.0"))
	float HashGridCellSize = 16.0f;

	/** Footprint dimensions, in hash-grid cells, displayed by the decal. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	FVector2D HashGridSelectionFootprintCells = FVector2D(64.0f, 64.0f);

	/** Rejects footprints that extend outside BuildGridMapResolution. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	bool bConstrainBuildingPlacementToMap = true;

	/** Rejects placement on surfaces steeper than this angle. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float BuildPlacementMaxSlopeDegrees = 30.0f;

	/** Enables overlap checks against world actors inside the proposed footprint. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	bool bValidateBuildPlacementCollision = true;

	/** Vertical half-height used by the placement overlap box. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "1.0"))
	float BuildPlacementCollisionHalfHeight = 300.0f;

	/** Optional debug line overlay. The circular SC2 material grid does not require it. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	bool bDrawBuildPlacementGridLines = false;

	/** Shows a local buildability guide around the occupied building footprint. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	bool bEnableBuildPlacementGuidance = true;

	/** Global circular guide radius shared by every building. Diameter is Radius * 2 + 1 cells and is independent of footprint. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "1", ClampMax = "7", UIMin = "1", UIMax = "7", DisplayName = "Build Placement Guidance Radius (Cells)", ToolTip = "Global circular guidance radius shared by every building placement command. This does not change the building footprint. Radius 7 displays a 15-cell-wide guide."))
	int32 BuildPlacementGuidanceRadiusCells = 7;

	/** Barracks build time. The current game clock maps one real second to one game day. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "0.1", UIMin = "1.0", UIMax = "60.0", DisplayName = "Barracks Construction Time (Seconds / Game Days)"))
	float BarracksConstructionTimeSeconds = 14.0f;

	/** Tank-factory build time. The current game clock maps one real second to one game day. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "0.1", UIMin = "1.0", UIMax = "120.0", DisplayName = "Tank Factory Construction Time (Seconds / Game Days)"))
	float TankFactoryConstructionTimeSeconds = 28.0f;

	/** Maximum site- or commander-centered radius used to form an officer's infantry construction pool. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Construction", meta = (ClampMin = "1", UIMin = "1", UIMax = "512", DisplayName = "Defense Infantry Search Radius (Cells)"))
	int32 DefenseConstructionInfantrySearchRadiusCells = 128;

	/** Legacy serialized setting. The parallel scheduler always assigns exactly one infantryman to each site. */
	UPROPERTY(Config, BlueprintReadOnly, Category = "RTS Construction", meta = (DeprecatedProperty, DeprecationMessage = "Each defense site now owns one worker; excess orders are distributed across the infantry pool."))
	int32 DefenseConstructionInfantryCount = 1;

	/** Number of opacity bands used to approximate a smooth outward fade. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "1", ClampMax = "6"))
	int32 BuildPlacementGuidanceFadeBands = 4;

	/** High-contrast blue guide color for anchors where the complete footprint can be built. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	FLinearColor BuildPlacementGuidanceValidColor = FLinearColor(0.05f, 0.35f, 1.0f, 0.42f);

	/** High-contrast yellow guide color for anchors that fail terrain, obstruction, or game rules. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	FLinearColor BuildPlacementGuidanceInvalidColor = FLinearColor(1.0f, 0.72f, 0.04f, 0.50f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "0.1"))
	float BuildPlacementGridLineThickness = 1.0f;

	/** Every Nth grid line is emphasized. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "1"))
	int32 BuildPlacementMajorLineEvery = 8;

	/** Safety cap for grid lines along either footprint axis. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "8"))
	int32 BuildPlacementMaxGridLinesPerAxis = 128;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	FLinearColor BuildPlacementValidColor = FLinearColor(0.05f, 0.80f, 0.10f, 0.55f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	FLinearColor BuildPlacementInvalidColor = FLinearColor(1.0f, 0.05f, 0.03f, 0.55f);

	/** Decal projection depth. Increase this if steep terrain stops receiving the preview. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "1.0"))
	float HashGridSelectionDecalDepth = 4096.0f;

	/** Half-height used when tracing snapped hash-grid locations back down to terrain. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement", meta = (ClampMin = "1000.0"))
	float HashGridSelectionTraceHalfHeight = 50000.0f;

	/** Snaps selected X/Y to HashGridCellSize before issuing the command. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Building Placement")
	bool bSnapHashGridSelectionToGrid = true;

	/** Uses the built-in high-contrast RTS software cursors. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Feedback")
	bool bEnableStrategyMouseCursor = true;

	/** Changes the cursor and previews the single selectable currently under it. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Feedback")
	bool bEnableSelectableHoverPreview = true;

	/** Half-size of the tiny screen-space pick frustum used by click and hover selection. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Feedback", meta = (ClampMin = "1.0", ClampMax = "12.0"))
	float SelectionClickHalfSizePixels = 3.0f;

	/** Custom-depth stencil used by Actor-backed hover outlines. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Feedback", meta = (ClampMin = "0", ClampMax = "255"))
	int32 HoverOutlineStencilValue = 250;

	/** Shows terrain-conforming arrival-range feedback when a move command is issued. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback")
	bool bEnableMoveCommandFeedback = true;

	/** Legacy optional decal. Arrival-range feedback no longer projects a decal onto the terrain. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback")
	TSoftObjectPtr<UMaterialInterface> CommandFeedbackDecalMaterial;

	/** Radius accepted as arrival for move/patrol destinations, in Unreal units. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback", meta = (ClampMin = "1.0", Units = "cm"))
	float MoveArrivalRange = 256.0f;

	/** Deprecated size retained for old config serialization. */
	UPROPERTY(Config, BlueprintReadOnly, Category = "RTS Command Feedback", meta = (DeprecatedProperty, DeprecationMessage = "Use MoveArrivalRange."))
	float MoveCommandFeedbackSize = 256.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback", meta = (ClampMin = "0.1"))
	float MoveCommandFeedbackDuration = 0.65f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback")
	FLinearColor MoveCommandFeedbackColor = FLinearColor(0.08f, 1.0f, 0.18f, 1.0f);

	/** Red pulse used by attack-ground and attack-target commands. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback")
	FLinearColor AttackGroundFeedbackColor = FLinearColor(1.0f, 0.035f, 0.02f, 1.0f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback", meta = (ClampMin = "0.5", ClampMax = "12.0"))
	float MoveCommandFeedbackLineThickness = 3.0f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback", meta = (ClampMin = "12", ClampMax = "64"))
	int32 MoveCommandFeedbackSegments = 32;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxMoveCommandFeedbackPulses = 8;

	/**
	 * Reserved for a future renderer-native selected-range implementation.
	 * Deliberately defaults off: the input plugin must never scan selected units
	 * or read Mass fragments to build visual feedback.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Feedback")
	bool bEnableSelectedRangeIndicator = false;

	// Spawns one MassBattleFrame Attached Batch FX renderer for all selected-unit
	// halos. This is a one-time renderer lookup; it never scans selected entities.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Feedback")
	bool bAutoSpawnSelectionFxRenderer = true;

	/** MassBattleFrame renderer class for the shared selected-unit halo batch. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Selection Feedback")
	TSoftClassPtr<AActor> SelectionFxRendererClass = TSoftClassPtr<AActor>(
		FSoftObjectPath(TEXT(
			"/Game/Unit/Shared/FX/Selection/BP_FxRenderer_SelectedRangeRing_Batch.BP_FxRenderer_SelectedRangeRing_Batch_C")));

	/** Shows one terrain-conforming destination range per queued command. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback")
	bool bEnableSelectedPathPreview = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback")
	FLinearColor SelectedMovePathColor = FLinearColor(0.04f, 1.0f, 0.16f, 0.95f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback")
	FLinearColor SelectedAttackMovePathColor = FLinearColor(1.0f, 0.025f, 0.015f, 0.98f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback", meta = (ClampMin = "0.1", ClampMax = "12.0"))
	float SelectedPathLineThickness = 2.0f;

	/** Hard cap on queued destination ranges; never scales with selected-unit count. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "RTS Command Feedback", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxSelectedTaskRouteLines = 32;
};

/**
 * Resolves country-specific MassBattle implementations into their shared command cards.
 * A command button is the skill: callers must not add a second identity gate.
 */
namespace RTSUnitTypeProtocol
{
	/** Returns the shared settings CDO and lazily restores plugin defaults if the custom config category was not merged. */
	RTSINPUTSYSTEM_API const URTSInputPanelSettings* GetSettings();

	RTSINPUTSYSTEM_API const FRTSMassUnitTypeProtocol* FindBySubType(
		const URTSInputPanelSettings* Settings,
		int32 SubTypeIndex);

	/** Resolves exact asset identity first, then retains SubType as legacy fallback. */
	RTSINPUTSYSTEM_API const FRTSMassUnitTypeProtocol* FindByNetworkKeyOrSubType(
		const URTSInputPanelSettings* Settings,
		FName NetworkKey,
		int32 SubTypeIndex);

	RTSINPUTSYSTEM_API FName ResolveCommandLoadoutId(
		const FRTSMassUnitTypeProtocol& Protocol);

	RTSINPUTSYSTEM_API bool ExposesCommandButton(
		const URTSInputPanelSettings* Settings,
		const FRTSMassUnitTypeProtocol& Protocol,
		const FGameplayTag& CommandTag);
}
