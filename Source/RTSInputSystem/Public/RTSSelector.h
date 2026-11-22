// Copyright 2024 Jesus Bracho All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "RTSHUD.h"
#include "RTSSelectable.h"
#include "RTSSelectionStructs.h"
#include "Data/RTSCommandButton.h"
#include "Components/ActorComponent.h"
#include "RTSSelector.generated.h"

class AActor;
class FRTSSelectorInputProcessor;
class IInputProcessor;
class ULocalPlayer;
class UMassBattleAgentConfigDataAsset;
class URTSSelectable;
class UWidget;

USTRUCT(BlueprintType)
struct RTSINPUTSYSTEM_API FRTSHashGridSelectionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RTSCamera - Hash Grid Selection")
	FGameplayTag CommandTag;

	UPROPERTY(BlueprintReadOnly, Category = "RTSCamera - Hash Grid Selection")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "RTSCamera - Hash Grid Selection")
	FIntPoint Cell = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "RTSCamera - Hash Grid Selection")
	float CellSize = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RTSCamera - Hash Grid Selection")
	FVector2D FootprintCells = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "RTSCamera - Hash Grid Selection")
	FVector GroundNormal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "RTSCamera - Hash Grid Selection")
	bool bIsValidPlacement = false;

	UPROPERTY(BlueprintReadOnly, Category = "RTSCamera - Hash Grid Selection")
	FText InvalidReason;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRTSHashGridSelectionCommitted, const FRTSHashGridSelectionResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRTSHashGridSelectionCancelled, FGameplayTag, CommandTag);

/**
 * @brief 将玩家的选区和目标输入交给本地 RTS 子系统。
 * @details 命令卡交付原按钮定义后，由选择器保存瞄准状态并解析光标、小地图或放置预览目标。
 * 
 * ```
 * 命令卡原按钮 → BeginCommandTargeting → 保存瞄准状态
 * 光标 / 小地图 / 格子预览 → 解析目标 → ExecuteCommand
 * ```
 * @ProductDocument
 */
UCLASS(Blueprintable, BlueprintType, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class RTSINPUTSYSTEM_API URTSSelector : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSSelector();

	/** Re-evaluates hover or placement state after the camera moves beneath a stationary pointer. */
	void RefreshPointerWorldState(const FVector2D& ScreenPosition);

	// BlueprintAssignable allows binding in Blueprints
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActorsSelected, const TArray<AActor*>&, SelectedActors);
	UPROPERTY(BlueprintAssignable)
	FOnActorsSelected OnActorsSelected;

	// BlueprintReadWrite allows access and modification in Blueprints
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputMappingContext* InputMappingContext;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* BeginSelection;

	// Action for right clicking to issue a smart command
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* IssueCommandAction;

	// --- Targeting State (for Targeted Commands like Move/Attack) ---
	UPROPERTY(BlueprintReadWrite, Category = "RTSCamera - Selection")
	bool bIsTargeting = false;
	/**
	 * @brief 瞄准开始时保存的按钮定义。
	 * @details 提交目标时把同一按钮传给选区子系统，响应组由提交时的索引提供。取消瞄准时清除此引用。
	 */
	UPROPERTY(Transient)
	TObjectPtr<URTSCommandButton> TargetingCommandDefinition;
	FOnRTSHashGridSelectionCancelled OnTargetingChanged;

	UPROPERTY(BlueprintReadWrite, Category = "RTSCamera - Selection")
	FGameplayTag PendingCommandTag;

	/**
	 * @brief 按标签为当前面板命令开始瞄准。
	 * @details 调用 BeginTargetingWithType，从当前命令索引取得按钮定义。
	 * @param CommandTag 当前命令面板中的命令标签。
	 */
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	void BeginTargeting(FGameplayTag CommandTag);

	/**
	 * @brief 按标签取得原按钮并开始瞄准。
	 * @details 从本地玩家的选区索引取得命令定义，再调用 BeginCommandTargeting。目标种类取自该定义。
	 * @param CommandTag 当前命令面板中的命令标签。
	 * @param TargetType 接口保留的目标类型参数。当前实现读取原按钮的 TargetType。
	 * @code{.cpp}
	 * Selector->BeginTargetingWithType(MoveCommandTag, ERTSCommandTargetType::Location);
	 * // In the minimap click handler:
	 * if (Selector->CommitPendingTargetingAtWorldLocation(ClickedWorldLocation))
	 * {
	 *     // The command was submitted. Update the minimap indicator.
	 * }
	 * @endcode
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	void BeginTargetingWithType(FGameplayTag CommandTag, ERTSCommandTargetType TargetType);

	/**
	 * @brief 结束当前命令的目标选择。
	 * @details 放置预览通过格子取消入口收尾。普通瞄准清除目标状态和按钮引用，恢复光标并请求刷新命令显示。
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	void CancelTargeting();

	/**
	 * @brief Commits the active command using the cursor target.
	 * @details Use this after starting command targeting.
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	bool CommitPendingTargetingAtCursor();

	/**
	 * @brief 为正在瞄准的位置命令提供世界坐标。
	 * @details 接受位置或位置及实体两用命令的世界坐标。将原按钮和坐标交给选区子系统，未按排队修饰键时随后结束瞄准。
	 * @param WorldLocation 小地图等入口提供的目标世界坐标。
	 * @return 坐标可交付给选区执行入口时为 true。当前状态或坐标不符合提交条件时为 false。
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	bool CommitPendingTargetingAtWorldLocation(FVector WorldLocation);

	// Input Action handler for Right Click
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	void OnIssueCommand(const FInputActionValue& Value);

	// Function to clear selected actors, can be overridden in Blueprints
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "RTSCamera - Selection")
	void ClearSelectedActors();

	// Function to handle selected actors, can be overridden in Blueprints
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "RTSCamera - Selection")
	void HandleSelectedActors(const TArray<AActor*>& NewSelectedActors);
	
	// Function to filter selectable actors, can be overriden in Blueprints
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "RTSCamera - Selection")
	bool CanSelectActor(AActor* Actor) const;

	// BlueprintCallable to allow calling from Blueprints
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	void OnSelectionStart(const FInputActionValue& Value);

	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	void OnUpdateSelection(const FInputActionValue& Value);

	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	void OnSelectionEnd(const FInputActionValue& Value);

	UPROPERTY(BlueprintReadOnly, Category = "RTSCamera - Selection")
	TArray<URTSSelectable*> SelectedActors;

	/**
	 * @brief 在建造格选择提交时提供选择结果。
	 * @ProductDocument
	 */
	UPROPERTY(BlueprintAssignable, Category = "RTSCamera - Hash Grid Selection")
	FOnRTSHashGridSelectionCommitted OnHashGridSelectionCommitted;

	/**
	 * @brief 在建造格选择取消时提供命令标签。
	 * @ProductDocument
	 */
	UPROPERTY(BlueprintAssignable, Category = "RTSCamera - Hash Grid Selection")
	FOnRTSHashGridSelectionCancelled OnHashGridSelectionCancelled;

	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Hash Grid Selection")
	void BeginHashGridSelection(FGameplayTag CommandTag);

	/**
	 * @brief 开始按占地尺寸选择建造格。
	 * @details 传入占地格数与单格大小。
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Hash Grid Selection")
	void BeginHashGridSelectionWithFootprint(FGameplayTag CommandTag, FVector2D FootprintCells, float CellSize);

	/** C++ placement entry using the final building configuration. */
	void BeginHashGridSelectionWithFootprintAndPreview(
		FGameplayTag CommandTag,
		FVector2D FootprintCells,
		float CellSize,
		UMassBattleAgentConfigDataAsset* PreviewUnit);

	/**
	 * @brief 取消当前的建造格选择。
	 * @ProductDocument
	 */
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Hash Grid Selection")
	void CancelHashGridSelection();

	UFUNCTION(BlueprintPure, Category = "RTSCamera - Hash Grid Selection")
	bool IsHashGridSelectionActive() const { return bIsHashGridSelecting; }



	/**
	 * @brief 为原按钮定义进入目标选择模式。
	 * @details 保存按钮引用和目标类型。有放置占地或需要格子选择的命令进入放置预览，其余目标命令使用十字光标。
	 * @param Command 命令面板交付的原按钮定义。
	 * @return 开始瞄准时为 true。空按钮和即时命令返回 false。
	 */
	bool BeginCommandTargeting(URTSCommandButton* Command);

	
	
	void BeginHashGridSelectionForContext(FGameplayTag CommandTag, FVector2D FootprintCells, float CellSize, UMassBattleAgentConfigDataAsset* PreviewUnit, URTSCommandButton* Command);

	
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent);

private:
	friend class FRTSSelectorInputProcessor;
#if WITH_DEV_AUTOMATION_TESTS
	friend class FWinyunqSelectionTabInputTest;
#endif

	UPROPERTY()
	APlayerController* PlayerController;

	UPROPERTY()
	ARTSHUD* HUD;

	UPROPERTY(Transient)
	TObjectPtr<class UDecalComponent> HashGridSelectionDecalComponent;

	/** Unhidden local host for placement primitives; AController owners are hidden by UE. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> BuildPlacementPreviewActor;

	UPROPERTY(Transient)
	TObjectPtr<class UInputComponent> ControlGroupInputComponent;

	TWeakObjectPtr<APlayerController> ControlGroupInputOwner;
	TWeakObjectPtr<class UUserWidget> BoundTopSelectWidget;

	FVector2D SelectionStart;
	FVector2D SelectionEnd;

	bool bIsSelecting;
	bool bSkipCurrentSelectionClick = false;
	bool bIsHashGridSelecting = false;
	ERTSCommandTargetType PendingTargetType = ERTSCommandTargetType::Location;
	FVector2D ActiveHashGridFootprintCells = FVector2D::ZeroVector;
	float ActiveHashGridCellSize = 0.0f;
	struct FBuildPlacementGuidanceSample
	{
		FVector WorldLocation = FVector::ZeroVector;
		FVector GroundNormal = FVector::UpVector;
		bool bBuildable = false;
	};
	struct FSelectedTaskRoute
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		TArray<FVector> GroundRingPoints;
		bool bAttackMove = false;
	};
	TMap<FIntPoint, FBuildPlacementGuidanceSample> BuildPlacementGuidanceSampleCache;
	TArray<uint32> MoveCommandFeedbackBatchIds;
	TArray<FSelectedTaskRoute> SelectedTaskRoutes;
	uint32 MoveCommandFeedbackSequence = 0;
	FIntPoint LastBuildPlacementGuidanceCell = FIntPoint::ZeroValue;
	FVector2D LastBuildPlacementGuidanceFootprintCells = FVector2D::ZeroVector;
	float LastBuildPlacementGuidanceCellSize = 0.0f;
	int32 LastBuildPlacementGuidanceRadiusCells = 0;
	bool bHasBuildPlacementGuidanceAnchor = false;
	FVector SelectedTaskRouteAnchor = FVector::ZeroVector;
	bool bHasSelectedTaskRouteAnchor = false;
	bool bSelectedPathPreviewVisible = false;
	TWeakObjectPtr<URTSSelectable> HoveredActorSelectable;
	FEntityHandle HoveredMassEntity;
	bool bCursorOverSelectable = false;
	bool bHoverVisualApplied = false;
	bool bHasProcessedPointerPixel = false;
	FIntPoint LastProcessedPointerPixel = FIntPoint::ZeroValue;
	int32 LastRecalledControlGroupIndex = INDEX_NONE;
	double LastControlGroupRecallTime = -1.0;
	FDelegateHandle CommandFeedbackDelegateHandle;
	FDelegateHandle ViewportWidgetAddedDelegateHandle;
	TSharedPtr<IInputProcessor> SelectionInputProcessor;

	void BindInputActions();
	void BindInputMappingContext();
	void RegisterSelectionInputProcessor();
	void UnregisterSelectionInputProcessor();
	void HandlePointerMoved();
	void UpdateSelectionAtScreenPosition(const FVector2D& ScreenPosition);
	void InstallStrategyMouseCursors();
	void RestoreStrategyMouseCursors();
	void UpdateSelectableHoverPreview(const FVector2D& ScreenPosition);
	void ClearSelectableHoverPreview(bool bRestoreDefaultCursor);
	void EnsureSelectionFxRenderer();
	void RegisterControlGroupHotkeys();
	void UnregisterControlGroupHotkeys();
	void HandleControlGroupHotkey(int32 GroupIndex);
	void TryBindTopSelectButtons();
	void UnbindTopSelectButtons();
	void BindTopSelectButton(FName WidgetName, FName HandlerName);
	void HandleViewportWidgetAdded(UWidget* Widget, ULocalPlayer* LocalPlayer);
	void SelectTopCategory(const TCHAR* TagName);
	void CollectComponentDependencyReferences();

	UFUNCTION()
	void HandleSubsystemSelectionChanged(const FRTSSelectionView& SelectionView);

	UFUNCTION()
	void HandleControlGroupFocusRequested(int32 GroupIndex, FVector WorldCenter);

	void HandleCommandFeedbackIssued(const FRTSSelectionView& Context, const FGameplayTag CommandTag, const FVector WorldLocation, const bool bHasWorldLocation, const bool bQueue);

	UFUNCTION() void SelectAllArtillery();
	UFUNCTION() void SelectAllAircraft();
	UFUNCTION() void SelectAllNaval();
	UFUNCTION() void SelectAllDefense();
	UFUNCTION() void SelectAllArmor();
	UFUNCTION() void SelectAllInfantry();
	UFUNCTION() void SelectAllGroundArmy();
	UFUNCTION() void SelectAllEngineers();
	UFUNCTION() void SelectAllCities();
	UFUNCTION() void SelectAllUniversities();
	UFUNCTION() void SelectAllResearch();
	UFUNCTION() void SelectAllGovernment();
	UFUNCTION() void SelectAllPorts();
	UFUNCTION() void SelectAllAirports();
	UFUNCTION() void SelectAllBarracks();
	UFUNCTION() void SelectAllMilitaryCamps();
	bool ShouldUseHashGridSelectionForCommand(FGameplayTag CommandTag) const;
	bool GetHashGridSelectionResult(FRTSHashGridSelectionResult& OutResult) const;
	bool ProjectHashGridSelectionLocationToGround(
		const FVector& CandidateLocation,
		FVector& OutLocation,
		FVector& OutNormal,
		AActor*& OutGroundActor) const;
	bool ValidateHashGridSelection(
		const FRTSHashGridSelectionResult& Result,
		AActor* GroundActor,
		FText& OutInvalidReason) const;
	FVector SnapHashGridSelectionLocation(const FVector& Location) const;
	FIntPoint GetHashGridCellForLocation(const FVector& Location) const;
	AActor* GetOrCreateBuildPlacementPreviewActor();
	AActor* GetBuildPlacementPreviewActor() const;
	void DestroyBuildPlacementPreviewActor();
	void BeginHashGridSelectionInternal(
		FGameplayTag CommandTag,
		FVector2D FootprintCells,
		float CellSize,
		UMassBattleAgentConfigDataAsset* PreviewUnit);
	void UpdateHashGridSelectionPreview();
	void UpdateBuildPlacementGuidance(const FRTSHashGridSelectionResult& Result);
	void ClearBuildPlacementGuidance(bool bResetSamples);
	void DrawHashGridSelectionPreview(const FRTSHashGridSelectionResult& Result) const;
	void EndHashGridSelectionPreview();
	void CommitHashGridSelection();
	bool IsCommandQueueModifierDown() const;
	/**
	 * @brief 解析光标目标并提交瞄准中的命令。
	 * @details 实体目标从光标命中解析 Mass 句柄，位置目标使用命中世界坐标。目标与瞄准类型匹配后，把原按钮定义和目标交给选区子系统。
	 * @param Hit 用于位置目标的光标碰撞结果。
	 * @return 取得可提交的目标并调用选区执行入口时为 true。目标解析失败或选区子系统缺失时为 false。
	 */
	bool IssuePendingTargetingCommand(const FHitResult& Hit);
	bool ResolveSmartCommandHit(FHitResult& OutHit, FEntityHandle& OutEntityTarget, bool& bOutHostileUnitTarget) const;
	void ShowGroundCommandFeedback(const FVector& Location, bool bAttackGround);
	void ClearMoveCommandFeedback();
	TArray<FVector> BuildGroundConformingRing(
		const FVector& Center,
		float Radius,
		int32 Segments) const;
	bool CacheSelectedTaskRouteAnchor(const FVector2D& ScreenPosition);
	void RedrawSelectedTaskRoutes();
	void ClearSelectedPathPreview();
	class UMaterialInterface* ResolveCommandFeedbackMaterial() const;
};
