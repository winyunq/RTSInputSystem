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
class UStaticMesh;
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

	UPROPERTY(BlueprintReadWrite, Category = "RTSCamera - Selection")
	FGameplayTag PendingCommandTag;

	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	void BeginTargeting(FGameplayTag CommandTag);

	/** Starts targeting while preserving the button's explicit target model. */
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	void BeginTargetingWithType(FGameplayTag CommandTag, ERTSCommandTargetType TargetType);

	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	void CancelTargeting();

	/** Commits the active targeted command at the current cursor position. */
	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Selection")
	bool CommitPendingTargetingAtCursor();

	/** Commits the active location-capable command at an explicit world point, such as a minimap click. */
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

	UPROPERTY(BlueprintAssignable, Category = "RTSCamera - Hash Grid Selection")
	FOnRTSHashGridSelectionCommitted OnHashGridSelectionCommitted;

	UPROPERTY(BlueprintAssignable, Category = "RTSCamera - Hash Grid Selection")
	FOnRTSHashGridSelectionCancelled OnHashGridSelectionCancelled;

	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Hash Grid Selection")
	void BeginHashGridSelection(FGameplayTag CommandTag);

	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Hash Grid Selection")
	void BeginHashGridSelectionWithFootprint(FGameplayTag CommandTag, FVector2D FootprintCells, float CellSize);

	/** C++ placement entry that also supplies the authored building ghost mesh. */
	void BeginHashGridSelectionWithFootprintAndPreview(
		FGameplayTag CommandTag,
		FVector2D FootprintCells,
		float CellSize,
		UStaticMesh* PreviewMesh);

	UFUNCTION(BlueprintCallable, Category = "RTSCamera - Hash Grid Selection")
	void CancelHashGridSelection();

	UFUNCTION(BlueprintPure, Category = "RTSCamera - Hash Grid Selection")
	bool IsHashGridSelectionActive() const { return bIsHashGridSelecting; }

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

	void HandleCommandFeedbackIssued(
		FGameplayTag CommandTag,
		FVector WorldLocation,
		bool bHasWorldLocation,
		bool bQueue);

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
		UStaticMesh* PreviewMesh);
	void UpdateHashGridSelectionPreview();
	void UpdateBuildPlacementGuidance(const FRTSHashGridSelectionResult& Result);
	void ClearBuildPlacementGuidance(bool bResetSamples);
	void DrawHashGridSelectionPreview(const FRTSHashGridSelectionResult& Result) const;
	void EndHashGridSelectionPreview();
	void CommitHashGridSelection();
	bool IsCommandQueueModifierDown() const;
	bool IssuePendingTargetingCommand(const FHitResult& Hit);
	bool ResolveSmartCommandHit(FHitResult& OutHit, bool& bOutHostileUnitTarget) const;
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
