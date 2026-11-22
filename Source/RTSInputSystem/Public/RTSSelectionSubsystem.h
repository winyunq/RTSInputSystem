// Copyright 2024 Winy unq All Rights Reserved.

/**
 * @file RTSSelectionSubsystem.h
 * @brief 提供本地选区、活动组和命令响应索引。
 * @details HUD 从 GetSelectionView 读取显示数据。输入侧通过 ExecuteCommand 提交原按钮与已解析的目标。
 * @code{.unparsed}
 * 选中实体 → BuildSelectionView → HUD 选区视图
 *                  └→ CommandGroups → RebuildCommandIndex ← 根面板 + 当前面板
 *                                           └→ 标签 → 原按钮 / 响应组号
 * 原按钮 Execute(目标) → SubmitCommandContext → 查找响应组
 *                                   ├→ 已注册业务动作
 *                                   └→ 原生 Mass 发令
 * @endcode
 */
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
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FOnRTSUnitHealthChanged,
	const FEntityHandle& /*Entity*/, float /*CurrentHealth*/, float /*MaximumHealth*/);
DECLARE_MULTICAST_DELEGATE_FourParams(
	FOnRTSCommandProgressChanged,
	UObject* /*ProgressProvider*/, FName /*SourceId*/, FGuid /*ResolvedId*/, bool /*bCancelled*/);
DECLARE_MULTICAST_DELEGATE_FourParams(
	FOnRTSCommandFeedbackIssued,
	FGameplayTag /*CommandTag*/,
	FVector /*WorldLocation*/,
	bool /*bHasWorldLocation*/,
	bool /*bQueue*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FRTSExternalMassCommandGridResolver, UObject* /*WorldContextObject*/, const FString& /*ActiveKey*/, const FRTSSelectionView& /*SelectionView*/, class URTSCommandGridAsset*& /*OutGrid*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FRTSExternalMassInstantCommandHandler, UObject* /*WorldContextObject*/, const FGameplayTag& /*CommandTag*/, const FRTSSelectionView& /*SelectionView*/, bool& /*bHandled*/);
DECLARE_MULTICAST_DELEGATE_FiveParams(FRTSExternalMassLocationCommandHandler, UObject* /*WorldContextObject*/, const FGameplayTag& /*CommandTag*/, const FVector& /*Location*/, const FRTSSelectionView& /*SelectionView*/, bool& /*bHandled*/);
DECLARE_MULTICAST_DELEGATE_FiveParams(FRTSExternalMassTargetCommandHandler, UObject* /*WorldContextObject*/, const FGameplayTag& /*CommandTag*/, const FEntityHandle& /*TargetEntity*/, const FRTSSelectionView& /*SelectionView*/, bool& /*bHandled*/);
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
 * @brief 维护本地玩家的选区、活动组与命令面板索引。
 * @details 按选区建立分组视图和响应组索引。输入侧通过 ExecuteCommand 交付按钮与目标，按钮基类再经 SubmitCommandContext 发令。HUD 读取选区视图并接收导航、进度和命令确认事件。
 * @code{.cpp}
 * #include "Engine/LocalPlayer.h"
 * #include "RTSSelectionSubsystem.h"
 *
 * if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
 * {
 *     if (auto* Selection = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>())
 *     {
 *         const FRTSSelectionView& View = Selection->GetSelectionView();
 *         // Render View in the local player's unit panel.
 *     }
 * }
 * @endcode
 * @ProductDocument
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSSelectionSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	
	FOnRTSCommandProgressChanged OnCommandProgressChanged;
	FOnRTSUnitHealthChanged OnUnitHealthChanged;

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
	void NotifyCommandProgressChanged(UObject* ProgressProvider, FName SourceId = NAME_None,
			FGuid ResolvedId = FGuid(), bool bCancelled = false);
	
	void NotifyCommandProgressChanged(UObject* ProgressProvider, const FEntityHandle& Entity,
		FName SourceId, FGuid ResolvedId = FGuid(), bool bCancelled = false);

	/** Forwards processed health values to widgets already bound to the affected entity. */
	void NotifyUnitHealthChanged(const FEntityHandle& Entity, float CurrentHealth, float MaximumHealth);

	/**
	 * @brief 按命令标识直接读取当前索引中的原命令定义。
	 * @details 使用当前面板已建立的映射，不扫描按钮或加载其他面板。
	 * @param CommandTag 当前可用命令的标识。
	 * @return 对应的原命令定义；当前索引没有此命令时为空。
	 */
	URTSCommandButton* FindCommandButton(FGameplayTag CommandTag);

    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void RequestGridNavigation(class URTSCommandGridAsset* NewGrid) { OnCommandNavigationRequested.Broadcast(NewGrid); }
	// Subsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * @brief Update the local player's Actor and Mass entity selection. / 更新本地玩家选中的 Actor 和 Mass 实体。
	 * @details Pass the units found by your selection input. Use Replace for a new selection or Add to retain the current units while adding the new ones. / 传入选择操作找到的单位。普通选择使用 Replace，追加选择使用 Add 保留原有单位。
	 * @param InActors Actor units to select. / 要选择的 Actor 单位。
	 * @param InEntities Mass entity handles to select. / 要选择的 Mass 实体句柄。
	 * @param Modifier Operation applied to the selection. Defaults to Replace. / 修改选区的方式，默认为 Replace。
	 * @code{.cpp}
	 * Selection->SetSelectedUnits(Actors, Entities, ERTSSelectionModifier::Replace);
	 * @endcode
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	void SetSelectedUnits(const TArray<AActor*>& InActors, const TArray<FEntityHandle>& InEntities, ERTSSelectionModifier Modifier = ERTSSelectionModifier::Replace);

	/**
	 * @brief Save the current selection in a numbered control group. / 将当前选区保存到指定编号的控制组。
	 * @details Use the same index when recalling the group. A hotkey handler can bind the save and recall operations separately. / 召回时使用同一编号。快捷键可分别绑定保存和召回操作。
	 * @param GroupIndex Control group index from 0 through 9. / 控制组编号，范围为 0 至 9。
	 * @param AssignmentMode How to update the existing group. Replace overwrites its membership. / 控制组的更新方式，Replace 会覆盖原成员。
	 * @code{.cpp}
	 * // Save with Ctrl+1, then recall with 1.
	 * Selection->AssignCurrentSelectionToControlGroup(1, ERTSControlGroupAssignmentMode::Replace);
	 * Selection->RecallControlGroup(1);
	 * @endcode
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	bool AssignCurrentSelectionToControlGroup(int32 GroupIndex, ERTSControlGroupAssignmentMode AssignmentMode = ERTSControlGroupAssignmentMode::Replace);

	/**
	 * @brief Recall the units saved in a numbered control group. / 召回指定控制组中保存的单位。
	 * @details An empty group leaves the selection unchanged. Use the additive flag for a separate hotkey that merges the group with the current selection. / 空组保持当前选区。可以为追加召回绑定单独的快捷键，把该组单位加入选区。
	 * @param GroupIndex Control group index from 0 through 9. / 控制组编号，范围为 0 至 9。
	 * @param bAddToSelection True to add group members to the current selection. / 为 true 时把控制组成员加入当前选区。
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	bool RecallControlGroup(int32 GroupIndex, bool bAddToSelection = false);

	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	void ClearControlGroup(int32 GroupIndex);

	/**
	 * Returns all ten slot snapshots in display order: 1-9, then 0.
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	FRTSControlGroupsView GetControlGroupsView();

	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	void RequestControlGroupsRefresh();

	/**
	 * Broadcasts a camera-focus request at a real unit inside the group's dominant dense cluster.
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Control Groups")
	bool RequestControlGroupFocus(int32 GroupIndex);

	UFUNCTION(BlueprintPure, Category = "RTS Selection|Control Groups")
	int32 GetActiveControlGroupIndex() const { return ActiveControlGroupIndex; }

	/**
	 * Selects all live, controllable Actor/Mass units matching a hierarchical category query.
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Quick Selection")
	int32 SelectUnitsByQuery(const FRTSSelectionQuery& Query, ERTSSelectionModifier Modifier = ERTSSelectionModifier::Replace);

	UFUNCTION(BlueprintCallable, Category = "RTS Selection|Quick Selection")
	int32 CountUnitsByQuery(const FRTSSelectionQuery& Query) const;

	/**
	 * @brief Clears the local player selection.
	 * @details Removes the Actor and Mass entity selections.
	 * @ProductDocument
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
	 * @brief 切换到 HUD 显示顺序中的下一组。
	 * @details 按视图中的组键顺序循环，更新活动组索引并推送新的选区和命令面板。少于两个组时保持当前活动组。
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

	/**
	 * Native extension point used by optional plugins to provide command grids for pure Mass selections.
	 * @ProductDocument
	 */
	static FRTSExternalMassCommandGridResolver& OnResolveMassCommandGrid();

	/**
	 * Native extension point for optional systems to append live activity/status data to Mass units.
	 * @ProductDocument
	 */
	static FRTSExternalMassUnitDataEnricher& OnEnrichMassUnitData();
	/**
	 * Lets a project-owned indexed registry satisfy a quick-selection query without a world scan.
	 * @ProductDocument
	 */
	static FRTSExternalQuickSelectionResolver& OnResolveQuickSelection();

	/**
	 * @ProductDocument
	 * @brief 取得旧版 Mass 无目标命令多播委托。
	 * @details 旧广播将命令标签和组视图传给订阅者。订阅者共用 bHandled，用它表示该次广播中的动作已被接管。当前提交入口通过动作表选择执行函数。
	 * @return 供旧业务注册或显式广播的多播委托。
	 */
	static FRTSExternalMassInstantCommandHandler& OnHandleMassInstantCommand();
	/**
	 * @brief 取得旧版 Mass 位置命令多播委托。
	 * @details 旧广播传递位置与组视图。订阅者共用 bHandled。当前 SubmitCommandContext 经动作表发令。
	 * @ProductDocument
	 * @return 供旧业务注册或显式广播的位置命令多播委托。
	 */
	static FRTSExternalMassLocationCommandHandler& OnHandleMassLocationCommand();
	/**
	 * @brief 取得旧版 Mass 实体目标命令多播委托。
	 * @details 旧广播传递目标实体与组视图。订阅者共用 bHandled。当前 SubmitCommandContext 经动作表发令。
	 * @ProductDocument
	 * @return 供旧业务注册或显式广播的实体目标命令多播委托。
	 */
	static FRTSExternalMassTargetCommandHandler& OnHandleMassTargetCommand();

	/**
	 * Native extension point for game-specific placement rules such as ownership
	 * auras and resource deposits. Validators may reject an otherwise valid
	 * terrain/collision result and should provide a player-facing reason.
	 * @ProductDocument
	 */
	static FRTSExternalBuildPlacementValidator& OnValidateBuildPlacement();

    /**
     * @brief 按标签触发面板中的即时命令。
     * @details 从当前命令索引查找原按钮，交给 ExecuteCommand 匹配空目标并执行。
     * @param CommandTag 当前面板中要执行的即时命令标签。
     * @ProductDocument
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void IssueCommand(FGameplayTag CommandTag);

    /**
     * @ProductDocument
     * @brief 按标签为响应组提交一个位置命令。
     * @details 读取索引首组并写入排队状态，随后调用 SubmitCommandContext。此入口供右键位置命令、小地图和指定标签的调用方使用。
     * @param CommandTag 要提交的命令标签。
     * @param Location 目标世界坐标。
     * @param bQueue 传入本次命令的排队状态。
     * @param bForceStrategicNavigation 原生移动是否强制使用战略导航尺度。
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void IssueCommandWithLocation(
		FGameplayTag CommandTag,
		FVector Location,
		bool bQueue = false,
		bool bForceStrategicNavigation = false);

    /**
     * Issues a command targeting a specific actor to all selected units.
     * @ProductDocument
     * @brief 按标签发起实体目标命令。
     * @details 转给 IssueCommandWithEntityTarget，由原按钮定义处理。
     * @param CommandTag 要提交的命令标签。
     * @param TargetEntity 目标 Mass 实体的句柄。
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    void IssueCommandWithTarget(FGameplayTag CommandTag, const FEntityHandle& TargetEntity);

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool HasSelectedActors() const { return SelectedActors.Num() > 0; }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool HasSelectedMass() const { return SelectedEntities.Num() > 0; }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool IsActorSelected(const AActor* Actor) const { return SelectedActors.Contains(Actor); }

	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	bool IsEntitySelected(const FEntityHandle& Handle) const { return SelectedEntities.Contains(Handle); }

    /**
     * 返回当前选中的 Actor 列表 (只读访问)
     * @ProductDocument
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    const TArray<AActor*>& GetSelectedActors() const { return SelectedActors; }

	/**
	 * 返回当前选中的 Mass 实体列表。
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTS Selection")
	TArray<FEntityHandle> GetSelectedEntities() const { return SelectedEntities; }

	/** Zero-copy native view for per-frame systems; Blueprint callers retain the safe copy above. */
	const TArray<FEntityHandle>& GetSelectedEntitiesView() const { return SelectedEntities; }

	/** Returns selected Mass entities that are still alive and owned by the local player. 
	 * @brief 列出当前选区中由本地玩家控制的实体。
	 * @details 遍历选中句柄，逐个调用 IsEntityControllable 并复制满足条件的句柄。
	 * @return 可控选中实体的新数组。
	 */
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
		const TArray<FEntityHandle>& GetActiveMassEntities() const;

    /** 
     * 获取当前“激活”的 Actor (即当前选中组的代表)
     * 在 Direct Callback 模式下，它作为按钮执行的主要上下文。
     */
    UFUNCTION(BlueprintCallable, Category = "RTS Selection")
    AActor* GetActiveActor() const;

	/** The same command owner carried by the active selection's unit data. */
	UObject* GetActiveCommandContext() const;

	/** Resolves the objects represented by a UI group when that UI establishes its binding. */
	TArray<FRTSUnitData> GetUnitGroupMembers(const FString& GroupKey) const;
	static void AddOrUpdateSummaryGroup(TMap<FString, FRTSUnitData>& GroupMap, const FRTSUnitData& Data);

	/**
	 * Event fired when selection changes. UI should bind to this.
	 * @ProductDocument
	 */
	UPROPERTY(BlueprintAssignable, Category = "RTS Selection")
	FOnSelectionChanged OnSelectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "RTS Selection|Control Groups")
	FOnControlGroupsChanged OnControlGroupsChanged;

	/**
	 * Camera components may bind here without making the selection subsystem depend on a camera implementation.
	 * @ProductDocument
	 */
	UPROPERTY(BlueprintAssignable, Category = "RTS Selection|Control Groups")
	FOnControlGroupFocusRequested OnControlGroupFocusRequested;

	/** Shared unit presentation lookup for command buttons and other selection-driven UI. */
	FString GetMassSubtypeDisplayName(int32 SubTypeIndex) const;
	UTexture2D* GetMassUnitPortrait(FName UnitAssetKey, int32 SubTypeIndex) const;
	UTexture2D* GetMassSubtypeUnitPanelIcon(int32 SubTypeIndex) const;
	UTexture2D* GetMassSubtypeUnitAvatar(int32 SubTypeIndex) const;


	/**
	 * @brief 按标签提交以 Mass 实体为目标的命令。
	 * @details 按标签取得原按钮，目标类型匹配时调用按钮的 Execute。
	 * @param CommandTag 要提交的命令标签。
	 * @param TargetEntity 目标 Mass 实体的句柄。
	 */
	void IssueCommandWithEntityTarget(FGameplayTag CommandTag, const FEntityHandle& TargetEntity);

	/**
	 * @brief 读取缓存的当前选区视图。
	 * @details 选区或活动组变化时更新缓存；此读取不重新收集单位。
	 * @return 当前选区的只读引用。
	 * @ProductDocument
	 */
	const FRTSSelectionView& GetSelectionView();

	
	/**
	 * @brief 按命令标签向响应组提交命令。
	 * @details 按标签取得当前索引中的分组，再依次在当前 GameState 的实际类和默认动作表中查找一个执行函数。找到函数时逐组执行。没有登记动作时，把实体句柄交给原生 Mass 发令入口。单组直接使用原数组，多组在提交前合并。
	 * @param CommandTag 按钮配置的命令标签，同时用于查找响应组与执行函数。
	 * @param QueueOverride 指定是否追加命令。空值时读取本地 Shift 键。
	 * @param Location 位置目标的世界坐标。无需位置时传空指针。
	 * @param TargetEntity 命令指向的 Mass 实体。无需实体目标时传空句柄。
	 * @param bForceStrategicNavigation 原生发令使用战略导航；关闭时由原生发令选择导航尺度。
	 * @ProductDocument
	 * @note 同一次提交只在有接受结果时发布一次确认事件。实体目标的反馈坐标取该实体位置。原生多组命令需要拼接实体数组。
	 */
	void SubmitCommandContext(FGameplayTag CommandTag, TOptional<bool> QueueOverride, const FVector* Location = nullptr, FEntityHandle TargetEntity = FEntityHandle(), bool bForceStrategicNavigation = false);

	/**
	 * @brief 选区提交命令后发给 HUD 的确认事件。
	 * @details 参数依次为首个响应组视图、命令标签、反馈坐标、坐标是否有效以及是否排队。每次被提交入口接受的命令触发一次。
	 */
	TMulticastDelegate<void(const FRTSSelectionView&, FGameplayTag, FVector, bool, bool)> OnCommandFeedbackIssued;

	




	/**
	 * @brief 建立命令标签到按钮定义及响应组号的索引。
	 * @details 以根面板与当前面板的按钮定义为候选。每条按钮命令先记录活动组，再根据其他组的类型协议或组面板加入响应组。索引有效且面板按钮序列相同时复用。
	 * @param PanelButtons 当前面板提供的原按钮定义。它们追加到根面板定义之后。
	 * @note 构建成本取决于面板命令数与其他选中组数。类型协议分支逐组检查候选命令，查找过程可以遍历已配置的子面板。
	 */
	void RebuildCommandIndex(const TArray<URTSCommandButton*>& PanelButtons);


	

	/**
	 * @brief 把已匹配的目标交给原按钮定义执行。
	 * @details 即时命令传空实体句柄。瞄准命令在玩家点中有效目标后提交。位置和实体输入分别调用原按钮的 Execute 重载。
	 * @param Command 点击或开始瞄准时持有的按钮定义。
	 * @param Location 位置输入的世界坐标。未提供位置时为空。
	 * @param Target 实体目标句柄。未提供实体时为空句柄。
	 *
	 * @par 命令卡执行流程
	 *
	 * ```mermaid
	 * flowchart TD
	 *     A["点击按钮"] --> B{"1. 需要目标？"}
	 *     B -->|"无需目标，传空句柄"| C["2. 原按钮 Execute(Target)"]
	 *     B -->|"需要目标"| D["等待玩家点选"]
	 *     D -->|"目标有效，传入目标"| C
	 *     D -->|"目标无效"| D
	 * ```
	 *
	 * @ProductDocument
	 */
	void ExecuteCommand(URTSCommandButton* Command, const FVector* Location, FEntityHandle Target);


	/**
	 * @brief 汇总一条命令各响应组中的业务来源标识。
	 * @details 按命令索引取得分组并按顺序拼接其 CommandSourceIds，供生产等接口批量使用。
	 * @param CommandTag 要读取响应组的命令标签。
	 * @return 各响应组中保存的来源标识组成的连续数组。
	 */
	TArray<FName> GetCommandSourceIds(FGameplayTag CommandTag) const;

	/**
	 * @brief 用命令的首个响应组发布一次确认反馈。
	 * @details 在当前索引中找到首个响应组后广播 OnCommandFeedbackIssued。此入口的反馈没有目标位置，排队状态为关闭。
	 * @param CommandTag 调用方确认已发出的命令标签。
	 */
	void NotifyCommandFeedback(FGameplayTag CommandTag);


	public:
		/**
		 * @brief 按组内顺序访问命令的响应单位。
		 * @details 从共享索引取得响应组，直接读取各组原有单位数组；回调返回 false 时立即停止。
		 * @param CommandTag 当前可用命令的标识。
		 * @param Visit 接收单个单位句柄；返回 true 继续，返回 false 停止。
		 */
		void VisitCommandUnits(FGameplayTag CommandTag, TFunctionRef<bool(const FEntityHandle&)> Visit) const;
		/**
		 * @brief 以不放回的随机顺序访问命令的响应单位。
		 * @details 对所有响应组的单位做等概率抽取；只保存已访问位置的稀疏置换，不复制单位数组或预先完整洗牌。回调返回 false 时立即停止。
		 * @param CommandTag 当前可用命令的标识。
		 * @param Random 本次抽取使用并推进的随机流。
		 * @param Visit 接收单个单位句柄；返回 true 继续，返回 false 停止。
		 */
		void VisitCommandUnits(FGameplayTag CommandTag, FRandomStream& Random, TFunctionRef<bool(const FEntityHandle&)> Visit) const;

	/**
	 * @brief 打开指定命令面板。
	 * @details 子菜单按钮仅保存面板标识；实际进入时才通过现有配置解析面板和创建其按钮。
	 * @param LoadoutId 要进入的配置面板标识。
	 */
	void NavigateCommandLoadout(FName LoadoutId);


	private:
		int32 ActiveCommandGroupIndex = INDEX_NONE;
	private:
		/**
		 * @brief 当前选区按组保存的命令数据。
		 * @details 每组共享一份代表单位资料、实体句柄数组和来源标识数组。
		 */
		TArray<TSharedPtr<FRTSSelectionView>> CommandGroups;
		/**
		 * @brief 命令标签到响应组号的映射。
		 * @details 组号索引 CommandGroups。面板或活动组变化时重建。
		 */
		TMap<FGameplayTag, TArray<int32>> CommandGroupIndex;
		TArray<URTSCommandButton*> IndexedButtons;
		UPROPERTY(Transient) TObjectPtr<URTSCommandGridAsset> ActiveCommandGrid;
		/**
		 * @brief 读取索引中一条命令所对应的分组视图。
		 * @details 按标签取得组号数组，依组号读取共享的分组视图。返回的数组保留组号顺序，组内实体数组由各视图共享。
		 * @param CommandTag 要读取响应组的命令标签。
		 * @return 命令的分组视图引用数组。索引缺少此标签时为空。
		 * @note 每次调用复制对应响应组的共享指针。组内句柄数组由原视图持有。
		 */
		TArray<TSharedPtr<FRTSSelectionView>, TInlineAllocator<8>> GetCommandGroups(FGameplayTag CommandTag) const;
	private:
		/**
		 * @brief 命令标签到原按钮定义的映射。
		 * @details 索引根面板和当前面板提供的按钮。按标签入口通过此映射取得对应定义。
		 */
		UPROPERTY(Transient) TMap<FGameplayTag, TObjectPtr<URTSCommandButton>> CommandDefinitions;
	private:
		
		
	private:
		UPROPERTY(Transient) FRTSSelectionView CurrentSelectionView;
	private:
		
		
private:
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
	
	/** Command UI refreshes are dispatched synchronously from the state-change event. */
	bool bCommandRefreshInProgress = false;

	// Helpers
	FRTSUnitData CreateUnitDataFromActor(AActor* Actor) const;
	FRTSUnitData CreateUnitDataFromEntity(const FEntityHandle& Handle) const;
	/**
	 * @brief 重建 HUD 选区视图和命令分组数据。
	 * @details 遍历选中项，生成 HUD 单位资料并以组键汇集 Mass 句柄及来源标识。显示项按名称和组键排序，Tab 顺序取排序结果中组键首次出现的次序。
	 * @return 更新后的选区显示视图。
	 */
	FRTSSelectionView BuildSelectionView();
	void BroadcastSelectionViewAndGrid(const FRTSSelectionView& View);
	bool ResolveMassProtocolCommandGrid(const FString& ActiveKey, class URTSCommandGridAsset*& OutGrid);
	URTSCommandGridAsset* ResolveCommandLoadoutGrid(const URTSInputPanelSettings* Settings, const FRTSCommandLoadoutDefinition& Loadout);

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

    public:
    static inline TOptional<bool> (*ConfirmConstructionPlacement)(
        UObject*, const FGameplayTag&, const FVector&,
        TArrayView<const TSharedPtr<FRTSSelectionView>>, bool) = nullptr;
};




