// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "RTSSelectionStructs.h"
#include "MassEntityTypes.h"
#include "MassAPIStructs.h"
#include "RTSSelectionSubsystem.generated.h"

class AActor;
class URTSInputPanelSettings;
struct FRTSCommandLoadoutDefinition;

DECLARE_LOG_CATEGORY_EXTERN(LogORTSSelection, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectionChanged, const FRTSSelectionView&, SelectionView);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnControlGroupsChanged, const FRTSControlGroupsView&, ControlGroupsView);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnControlGroupFocusRequested, int32, GroupIndex, FVector, WorldCenter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCommandRefreshRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCommandNavigationRequested, class URTSCommandGridAsset*, NewGrid);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnRTSCommandProgressChanged,
	AActor* /*ProgressProvider*/);
DECLARE_MULTICAST_DELEGATE_FourParams(
	FOnRTSCommandFeedbackIssued,
	FGameplayTag /*CommandTag*/,
	FVector /*WorldLocation*/,
	bool /*bHasWorldLocation*/,
	bool /*bQueue*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FRTSExternalMassCommandGridResolver, UObject* /*WorldContextObject*/, const FString& /*ActiveKey*/, const FRTSSelectionView& /*SelectionView*/, class URTSCommandGridAsset*& /*OutGrid*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FRTSExternalMassInstantCommandHandler, UObject* /*WorldContextObject*/, const FGameplayTag& /*CommandTag*/, const FRTSSelectionView& /*SelectionView*/, bool& /*bHandled*/);
DECLARE_MULTICAST_DELEGATE_FiveParams(FRTSExternalMassLocationCommandHandler, UObject* /*WorldContextObject*/, const FGameplayTag& /*CommandTag*/, const FVector& /*Location*/, const FRTSSelectionView& /*SelectionView*/, bool& /*bHandled*/);
DECLARE_MULTICAST_DELEGATE_FiveParams(FRTSExternalMassTargetCommandHandler, UObject* /*WorldContextObject*/, const FGameplayTag& /*CommandTag*/, AActor* /*TargetActor*/, const FRTSSelectionView& /*SelectionView*/, bool& /*bHandled*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FRTSExternalMassUnitDataEnricher, UObject* /*WorldContextObject*/, const FEntityHandle& /*Entity*/, FRTSUnitData& /*Data*/);
DECLARE_MULTICAST_DELEGATE_FiveParams(
	FRTSExternalQuickSelectionResolver,
	const class URTSSelectionSubsystem* /*SelectionSubsystem*/,
	const FRTSSelectionQuery& /*Query*/,
	TArray<AActor*>& /*OutActors*/,
	TArray<FEntityHandle>& /*OutEntities*/,
	bool& /*bHandled*/);
DECLARE_MULTICAST_DELEGATE_SixParams(
	FRTSExternalBuildPlacementValidator,
	UObject* /*WorldContextObject*/,
	const FGameplayTag& /*CommandTag*/,
	const FVector& /*Location*/,
	const FVector2D& /*FootprintCells*/,
	bool& /*bIsValid*/,
	FText& /*OutInvalidReason*/);

struct FRTSControlGroupState
{
	TArray<TWeakObjectPtr<AActor>> Actors;
	TArray<FEntityHandle> Entities;
};

/**
 * Manages RTS selection state and formats data for the UI.
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSSelectionSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * O(1) visual-feedback event emitted once per player command.
	 * Consumers must not inspect the selected Actor/Mass arrays in response.
	 */
	FOnRTSCommandFeedbackIssued OnCommandFeedbackIssued;
	FOnRTSCommandProgressChanged OnCommandProgressChanged;

    /** 广播给 UI，请求刷新当前的指令网格（当单位内部状态改变时，如 CD 结束） */
    UPROPERTY(BlueprintAssignable, Category = "RTS Selection")
    FOnCommandRefreshRequested OnCommandRefreshRequested;

    /** 广播给 UI，请求导航到一个特定的指令网格（瞬态导航，如打开子菜单） */
    UPROPERTY(BlueprintAssignable, Category = "RTS Selection")
    FOnCommandNavigationRequested OnCommandNavigationRequested;

    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void RequestCommandRefresh();

	/** Rebuilds the current selection snapshot without changing the selection or command grid. */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void RequestSelectionRefresh();

	/** Announces a changed activity queue without rebuilding either UI panel. */
	void NotifyCommandProgressChanged(AActor* ProgressProvider);

    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void RequestGridNavigation(class URTSCommandGridAsset* NewGrid) { OnCommandNavigationRequested.Broadcast(NewGrid); }
	// Subsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void SetSelectedUnits(const TArray<AActor*>& InActors, const TArray<FEntityHandle>& InEntities, ERTSSelectionModifier Modifier = ERTSSelectionModifier::Replace);

	/** Writes the current selection into one of the persistent keyboard groups 0-9. */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	bool AssignCurrentSelectionToControlGroup(int32 GroupIndex, ERTSControlGroupAssignmentMode AssignmentMode = ERTSControlGroupAssignmentMode::Replace);

	/** Recalls a persistent group. Empty groups leave the current selection unchanged. */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	bool RecallControlGroup(int32 GroupIndex, bool bAddToSelection = false);

	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	void ClearControlGroup(int32 GroupIndex);

	/** Returns all ten slot snapshots in display order: 1-9, then 0. */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	FRTSControlGroupsView GetControlGroupsView();

	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	void RequestControlGroupsRefresh();

	/** Broadcasts a camera-focus request at a real unit inside the group's dominant dense cluster. */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	bool RequestControlGroupFocus(int32 GroupIndex);

	UFUNCTION(BlueprintPure, Category = "RTS Selection|Control Groups")
	int32 GetActiveControlGroupIndex() const { return ActiveControlGroupIndex; }

	/** Selects all live, controllable Actor/Mass units matching a hierarchical category query. */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Quick Selection")
	int32 SelectUnitsByQuery(const FRTSSelectionQuery& Query, ERTSSelectionModifier Modifier = ERTSSelectionModifier::Replace);

	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Quick Selection")
	int32 CountUnitsByQuery(const FRTSSelectionQuery& Query) const;

	/**
	 * Clears current selection.
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void ClearSelection();

    /** 
     * Fallback Command Grid used when an entity (like a soldier) does not have its own grid.
     */
    UPROPERTY(EditAnywhere, Category = "RTS Selection")
    TSoftObjectPtr<class URTSCommandGridAsset> DefaultEntityGrid;

	/**
	 * Cycles focus to the next available sub-group.
	 * (Tab functionality)
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void CycleGroup();

	/**
	 * Removes a specific unit/group from the current selection.
	 * (Shift-Click UI functionality)
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void RemoveUnit(const FRTSUnitData& UnitData);

	/**
	 * Restricts selection to ONLY units of the specified group key.
	 * (Ctrl-Click UI functionality)
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void SelectGroup(const FString& GroupKey);

	/** Native extension point used by optional plugins to provide command grids for pure Mass selections. */
	static FRTSExternalMassCommandGridResolver& OnResolveMassCommandGrid();

	/** Native extension point for optional systems to append live activity/status data to Mass units. */
	static FRTSExternalMassUnitDataEnricher& OnEnrichMassUnitData();
	/** Lets a project-owned indexed registry satisfy a quick-selection query without a world scan. */
	static FRTSExternalQuickSelectionResolver& OnResolveQuickSelection();

	/** Native extension points used by optional plugins to intercept Mass commands before default Move/Attack handling. */
	static FRTSExternalMassInstantCommandHandler& OnHandleMassInstantCommand();
	static FRTSExternalMassLocationCommandHandler& OnHandleMassLocationCommand();
	static FRTSExternalMassTargetCommandHandler& OnHandleMassTargetCommand();

	/**
	 * Native extension point for game-specific placement rules such as ownership
	 * auras and resource deposits. Validators may reject an otherwise valid
	 * terrain/collision result and should provide a player-facing reason.
	 */
	static FRTSExternalBuildPlacementValidator& OnValidateBuildPlacement();

    /**
     * Issues an instant command to all selected units.
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void IssueCommand(FGameplayTag CommandTag);

    /**
     * Issues a command targeting a specific location to all selected units.
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void IssueCommandWithLocation(FGameplayTag CommandTag, FVector Location, bool bQueue = false);

    /**
     * Issues a command targeting a specific actor to all selected units.
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void IssueCommandWithTarget(FGameplayTag CommandTag, AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool HasSelectedActors() const { return SelectedActors.Num() > 0; }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool HasSelectedMass() const { return SelectedEntities.Num() > 0; }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool IsActorSelected(const AActor* Actor) const { return SelectedActors.Contains(Actor); }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool IsEntitySelected(const FEntityHandle& Handle) const { return SelectedEntities.Contains(Handle); }

    /** 返回当前选中的 Actor 列表 (只读访问) */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    const TArray<AActor*>& GetSelectedActors() const { return SelectedActors; }

	/** 返回当前选中的 Mass 实体列表。 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	TArray<FEntityHandle> GetSelectedEntities() const { return SelectedEntities; }

	/** Zero-copy native view for per-frame systems; Blueprint callers retain the safe copy above. */
	const TArray<FEntityHandle>& GetSelectedEntitiesView() const { return SelectedEntities; }

	/** Returns selected Mass entities that are still alive and owned by the local player. */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	TArray<FEntityHandle> GetControllableSelectedEntities() const;

	/** Reads the local player's authoritative team directly from PlayerState. */
	UFUNCTION(BlueprintPure, Category = "RTS Selection|Ownership")
	int32 GetPlayerTeamIndex() const;

	UFUNCTION(BlueprintPure, Category = "RTS Selection|Ownership")
	bool IsEntityControllable(const FEntityHandle& Handle) const;

	UFUNCTION(BlueprintPure, Category = "RTS Selection|Ownership")
	bool IsActorControllable(const AActor* Actor) const;

	/** 返回当前 Tab 聚焦的分组 Key。 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	FString GetActiveGroupKey() const;

	/** 返回当前 Tab 聚焦分组内的 Mass 实体；没有聚焦分组时返回全部 Mass 选择。 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	TArray<FEntityHandle> GetActiveMassEntities() const;

    /** 
     * 获取当前“激活”的 Actor (即当前选中组的代表)
     * 在 Direct Callback 模式下，它作为按钮执行的主要上下文。
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    AActor* GetActiveActor() const;

	/**
	 * Event fired when selection changes. UI should bind to this.
	 */
	UPROPERTY(BlueprintAssignable, Category = "RTS Selection")
	FOnSelectionChanged OnSelectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "RTS Selection|Control Groups")
	FOnControlGroupsChanged OnControlGroupsChanged;

	/** Camera components may bind here without making the selection subsystem depend on a camera implementation. */
	UPROPERTY(BlueprintAssignable, Category = "RTS Selection|Control Groups")
	FOnControlGroupFocusRequested OnControlGroupFocusRequested;

	/** Shared unit presentation lookup for command buttons and other selection-driven UI. */
	FString GetMassSubtypeDisplayName(int32 SubTypeIndex) const;
	UTexture2D* GetMassUnitPortrait(FName UnitAssetKey, int32 SubTypeIndex) const;
	UTexture2D* GetMassSubtypeUnitPanelIcon(int32 SubTypeIndex) const;
	UTexture2D* GetMassSubtypeUnitAvatar(int32 SubTypeIndex) const;

private:
	TArray<TWeakObjectPtr<AActor>> PendingCommandProgressProviders;
	bool bCommandProgressNotificationPending = false;

	// Raw State
	UPROPERTY()
	TArray<AActor*> SelectedActors;

	TArray<FEntityHandle> SelectedEntities;

	TMap<int32, FRTSControlGroupState> ControlGroups;
	int32 ActiveControlGroupIndex = INDEX_NONE;
	int32 PreferredActiveControlGroupIndex = INDEX_NONE;

	// Cycle State
	UPROPERTY()
	TArray<FString> AvailableGroupKeys;
	
    UPROPERTY()
    TObjectPtr<class URTSCommandGridAsset> DefaultGridNative;

	UPROPERTY()
	TMap<FName, TObjectPtr<class URTSCommandGridAsset>> MassProtocolGridCache;

	int32 CurrentGroupIndex = 0;
	bool bExposeAllSelectedMassForComposableCommand = false;
	bool bCommandRefreshPending = false;

	// Helpers
	FRTSUnitData CreateUnitDataFromActor(AActor* Actor) const;
	FRTSUnitData CreateUnitDataFromEntity(const FEntityHandle& Handle) const;
	FRTSSelectionView BuildSelectionView();
	void BroadcastSelectionViewAndGrid(const FRTSSelectionView& View);
	void AddOrUpdateSummaryGroup(TMap<FString, FRTSUnitData>& GroupMap, const FRTSUnitData& Data);
	bool ResolveMassProtocolCommandGrid(const FString& ActiveKey, class URTSCommandGridAsset*& OutGrid);
	class URTSCommandGridAsset* ResolveCommandLoadoutGrid(const URTSInputPanelSettings* Settings, const FRTSCommandLoadoutDefinition& Loadout);

	bool IsValidControlGroupIndex(int32 GroupIndex) const;
	void PruneControlGroup(FRTSControlGroupState& Group);
	void PruneAllControlGroups();
	bool DoesControlGroupMatchCurrentSelection(const FRTSControlGroupState& Group) const;
	void UpdateActiveControlGroupIndex();
	FRTSControlGroupView BuildControlGroupView(int32 GroupIndex, const FRTSControlGroupState* Group) const;
	void BroadcastControlGroupsView();
	bool GetControlGroupFocusLocation(int32 GroupIndex, FVector& OutWorldCenter);
	void CollectUnitsMatchingQuery(const FRTSSelectionQuery& Query, TArray<AActor*>& OutActors, TArray<FEntityHandle>& OutEntities) const;
	FGameplayTagContainer BuildSelectionTags(
		const FString& TypeKey,
		const FString& Role,
		const FGameplayTagContainer& ExplicitTags,
		FGameplayTag UnitTypeTag = FGameplayTag()) const;
	bool IsMassEntityIdle(const FEntityHandle& Handle) const;
};
