#include "RTSHUD.h"
#include "RTSSelectionSubsystem.h"
#include "RTSSelectable.h"
#include "RTSInputPanelSettings.h"
#include "FuncLibs/MassBattleFuncLib.h"
#include "RTSSelector.h"
#include "Engine/Canvas.h"
#include "Interfaces/RTSCommandInterface.h"
#include "Data/RTSCommandGridAsset.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

bool ARTSHUD::ResolveSingleSelectableAtScreenPosition(
	APlayerController* PlayerController,
	const FVector2D& ScreenPosition,
	AActor*& OutActor,
	FEntityHandle& OutEntity,
	FVector& OutWorldLocation)
{
	OutActor = nullptr;
	OutEntity.Reset();
	OutWorldLocation = FVector::ZeroVector;
	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
		return false;
	}

	UWorld* World = PlayerController->GetWorld();
	if (!World)
	{
		return false;
	}

	URTSSelectionSubsystem* SelectionSubsystem = nullptr;
	if (const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		SelectionSubsystem = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>();
	}

	const FVector CameraLocation =
		PlayerController->PlayerCameraManager->GetCameraLocation();
	float ActorDistanceSq = TNumericLimits<float>::Max();
	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	if (PlayerController->DeprojectScreenPositionToWorld(
		ScreenPosition.X,
		ScreenPosition.Y,
		RayOrigin,
		RayDirection))
	{
		const FVector RayEnd = RayOrigin + RayDirection * 1000000.0f;
		FCollisionQueryParams QueryParams(
			SCENE_QUERY_STAT(RTSSingleSelectionActor),
			true);
		TArray<FHitResult> ActorHits;
		World->LineTraceMultiByChannel(
			ActorHits,
			RayOrigin,
			RayEnd,
			ECC_Visibility,
			QueryParams);
		for (const FHitResult& Hit : ActorHits)
		{
			AActor* Candidate = Hit.GetActor();
			for (int32 ParentDepth = 0;
				Candidate && ParentDepth < 4;
				++ParentDepth)
			{
				if (Candidate->FindComponentByClass<URTSSelectable>())
				{
					break;
				}
				Candidate = Candidate->GetAttachParentActor();
			}

			if (!Candidate
				|| !Candidate->FindComponentByClass<URTSSelectable>()
				|| (SelectionSubsystem
					&& !SelectionSubsystem->IsActorControllable(Candidate)))
			{
				continue;
			}

			const float CandidateDistanceSq =
				FVector::DistSquared(CameraLocation, Hit.ImpactPoint);
			if (CandidateDistanceSq < ActorDistanceSq)
			{
				OutActor = Candidate;
				OutWorldLocation = Hit.ImpactPoint;
				ActorDistanceSq = CandidateDistanceSq;
			}
		}
	}

	FEntityHandle MassCandidate;
	FVector MassLocation = FVector::ZeroVector;
	float MassDistanceSq = TNumericLimits<float>::Max();
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const float HalfSize = FMath::Clamp(
		Settings ? Settings->SelectionClickHalfSizePixels : 3.0f,
		1.0f,
		12.0f);
	const FVector2D ScreenPoints[4] =
	{
		ScreenPosition + FVector2D(-HalfSize, -HalfSize),
		ScreenPosition + FVector2D(-HalfSize, HalfSize),
		ScreenPosition + FVector2D(HalfSize, HalfSize),
		ScreenPosition + FVector2D(HalfSize, -HalfSize)
	};

	FViewTracePoints TracePoints;
	TracePoints.ViewPoint = CameraLocation;
	for (const FVector2D& Point : ScreenPoints)
	{
		FVector WorldPosition = FVector::ZeroVector;
		FVector WorldDirection = FVector::ForwardVector;
		if (PlayerController->DeprojectScreenPositionToWorld(
			Point.X,
			Point.Y,
			WorldPosition,
			WorldDirection))
		{
			TracePoints.SelectionPoints.Add(
				WorldPosition + WorldDirection * 100000.0f);
		}
	}

	if (TracePoints.SelectionPoints.Num() == 4)
	{
		bool bHitMass = false;
		TArray<FTraceResult> Results;
#if WITH_EDITOR
		FTraceDrawDebugConfig DebugConfig;
		DebugConfig.bDrawDebugShape = false;
		DebugConfig.Duration = 0.0f;
		UMassBattleFuncLib::ViewTraceForAgents(
			PlayerController,
			bHitMass,
			Results,
			1,
			TracePoints,
			false,
			FVector::ZeroVector,
			1.0f,
			ESortMode::NearToFar,
			CameraLocation,
			FEntityArray(),
			FMassBattleQuery(),
			DebugConfig);
#else
		UMassBattleFuncLib::ViewTraceForAgents(
			PlayerController,
			bHitMass,
			Results,
			1,
			TracePoints,
			false,
			FVector::ZeroVector,
			1.0f,
			ESortMode::NearToFar,
			CameraLocation);
#endif
		if (bHitMass && !Results.IsEmpty())
		{
			const FTraceResult& Result = Results[0];
			if (!SelectionSubsystem
				|| SelectionSubsystem->IsEntityControllable(Result.Entity))
			{
				MassCandidate = Result.Entity;
				MassLocation = Result.EntityLocation;
				MassDistanceSq =
					FVector::DistSquared(CameraLocation, MassLocation);
			}
		}
	}

	if (MassCandidate.IsSet() && MassDistanceSq < ActorDistanceSq)
	{
		OutActor = nullptr;
		OutEntity = MassCandidate;
		OutWorldLocation = MassLocation;
	}
	else if (OutActor)
	{
		OutEntity.Reset();
	}

	return OutActor != nullptr || OutEntity.IsSet();
}

// Constructor implementation: Initializes default values.
ARTSHUD::ARTSHUD()
{
	SelectionBoxColor = FLinearColor(0.0f, 0.78f, 1.0f, 0.95f);
	SelectionBoxFillColor = FLinearColor(0.0f, 0.35f, 0.85f, 0.12f);
	SelectionBoxThickness = 2.0f;
	MinSelectionSizeSq = 1.0f; // 1 pixel threshold as requested
	bIsDrawingSelectionBox = false;
}

// Render-only callback. Selection state changes are driven by input events.
void ARTSHUD::DrawHUD()
{
	Super::DrawHUD(); // Call the base class implementation.

	if (bIsDrawingSelectionBox
		&& FVector2D::DistSquared(SelectionStart, SelectionEnd)
			> MinSelectionSizeSq)
	{
		DrawSelectionBox(SelectionStart, SelectionEnd);
	}
}

// Starts the selection process, setting the initial point and activating the selection flag.
void ARTSHUD::BeginSelection(const FVector2D& StartPoint)
{
	SelectionStart = StartPoint;
	SelectionEnd = StartPoint; // Initialize End to Start to avoid stale data
	bIsDrawingSelectionBox = true;
}

// Updates the current endpoint of the selection box.
void ARTSHUD::UpdateSelection(const FVector2D& EndPoint)
{
	SelectionEnd = EndPoint;
}

// Ends the selection process and triggers the selection logic.
void ARTSHUD::EndSelection()
{
	bIsDrawingSelectionBox = false;
	PerformSelection();
}

// Default implementation of DrawSelectionBox. Draws a rectangle on the HUD.
void ARTSHUD::DrawSelectionBox_Implementation(const FVector2D& StartPoint, const FVector2D& EndPoint)
{
	if (Canvas)
	{
		float MinX = FMath::Min(SelectionStart.X, SelectionEnd.X);
		float MinY = FMath::Min(SelectionStart.Y, SelectionEnd.Y);
		float MaxX = FMath::Max(SelectionStart.X, SelectionEnd.X);
		float MaxY = FMath::Max(SelectionStart.Y, SelectionEnd.Y);
		float Width = MaxX - MinX;
		float Height = MaxY - MinY;

		if (Width > 0 && Height > 0)
		{
			DrawRect(SelectionBoxFillColor, MinX, MinY, Width, Height);
		}

		const FVector2D TopLeft(MinX, MinY);
		const FVector2D TopRight(MaxX, MinY);
		const FVector2D BottomRight(MaxX, MaxY);
		const FVector2D BottomLeft(MinX, MaxY);

		Canvas->K2_DrawLine(TopLeft, TopRight, SelectionBoxThickness, SelectionBoxColor);
		Canvas->K2_DrawLine(TopRight, BottomRight, SelectionBoxThickness, SelectionBoxColor);
		Canvas->K2_DrawLine(BottomRight, BottomLeft, SelectionBoxThickness, SelectionBoxColor);
		Canvas->K2_DrawLine(BottomLeft, TopLeft, SelectionBoxThickness, SelectionBoxColor);

	}
}

#include "RTSSelectionSubsystem.h"

// Default implementation of PerformSelection. Selects actors within the selection box.
void ARTSHUD::PerformSelection_Implementation()
{
	APlayerController* PlayerController = GetOwningPlayerController();
	PerformScreenSelection(
		PlayerController,
		PlayerController
			? PlayerController->FindComponentByClass<URTSSelector>()
			: nullptr,
		SelectionStart,
		SelectionEnd,
		MinSelectionSizeSq);
}

#include "FuncLibs/MassBattleFuncLib.h"
#include "MassBattleStructs.h"

void ARTSHUD::PerformMassSelection(TArray<FEntityHandle>& OutEntities)
{
	OutEntities.Reset();
	
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->PlayerCameraManager) return;

	URTSSelectionSubsystem* SelectionSubsystem = nullptr;
	if (const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
	{
		SelectionSubsystem = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>();
	}

	// Calculate selection box bounds
	float MinX = FMath::Min(SelectionStart.X, SelectionEnd.X);
    float MinY = FMath::Min(SelectionStart.Y, SelectionEnd.Y);
    float MaxX = FMath::Max(SelectionStart.X, SelectionEnd.X);
    float MaxY = FMath::Max(SelectionStart.Y, SelectionEnd.Y);
    
    float Width = MaxX - MinX;
    float Height = MaxY - MinY;
    float DragDistSq = Width * Width + Height * Height;

    // --- 核心逻辑：统一平截头体选择 (Unified Frustum Selection) ---
    // 无论是点选还是框选，都使用 ViewTraceForAgents 进行后端处理
    bool bIsClick = (DragDistSq < MinSelectionSizeSq);
    
    if (bIsClick)
    {
        // 如果是点选，将单点向四周扩展 1 像素，形成一个微型 2x2 选区
        // 这样可以确保平截头体法线非零，且能利用 Mass 优化的过滤逻辑
        MinX -= 1.0f;
        MaxX += 1.0f;
        MinY -= 1.0f;
        MaxY += 1.0f;
    }

	// 关键：逆时针排列（左上→左下→右下→右上）确保视锥体平面法线朝内
	// 顺时针排列会使法线朝外，导致 PlaneDot 过滤掉框内所有实体
	TArray<FVector2D> ScreenPoints = {
		FVector2D(MinX, MinY),  // 左上
		FVector2D(MinX, MaxY),  // 左下
		FVector2D(MaxX, MaxY),  // 右下
		FVector2D(MaxX, MinY),  // 右上
	};

	FViewTracePoints TracePoints;
	TracePoints.ViewPoint = PC->PlayerCameraManager->GetCameraLocation();

	for (const FVector2D& ScreenPoint : ScreenPoints)
	{
		FVector WorldPos, WorldDirection;
		if (PC->DeprojectScreenPositionToWorld(ScreenPoint.X, ScreenPoint.Y, WorldPos, WorldDirection))
		{
			TracePoints.SelectionPoints.Add(WorldPos + WorldDirection * 100000.0f);
		}
	}

	if (TracePoints.SelectionPoints.Num() == 4)
	{
		bool bHit = false;
		TArray<FTraceResult> Results;
        
		int32 LocalKeepCount = bIsClick ? 1 : -1;
		ESortMode SortMode = bIsClick ? ESortMode::NearToFar : ESortMode::None;

#if WITH_EDITOR
		FTraceDrawDebugConfig DebugCfg;
		DebugCfg.bDrawDebugShape = false;
		DebugCfg.Duration = 1.0f;
		UMassBattleFuncLib::ViewTraceForAgents(this, bHit, Results, LocalKeepCount, TracePoints, false, FVector::ZeroVector, 1.0f, SortMode,
			FVector::ZeroVector, FEntityArray(), FMassBattleQuery(), DebugCfg);
#else
		UMassBattleFuncLib::ViewTraceForAgents(this, bHit, Results, LocalKeepCount, TracePoints, false, FVector::ZeroVector, 1.0f, SortMode);
#endif

		UE_LOG(LogTemp, Warning, TEXT("PerformMassSelection: bHit=%d Results=%d IsClick=%d"), bHit ? 1:0, Results.Num(), bIsClick?1:0);

		if (bHit)
		{
			for (const FTraceResult& Result : Results)
			{
				if (!SelectionSubsystem || SelectionSubsystem->IsEntityControllable(Result.Entity))
				{
					OutEntities.Add(Result.Entity);
				}
			}
		}
	}
}

void ARTSHUD::PerformScreenSelection(
	APlayerController* PlayerController,
	URTSSelector* SelectorComponent,
	const FVector2D& StartPoint,
	const FVector2D& EndPoint,
	float ClickThresholdSq)
{
	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
		return;
	}

	ClickThresholdSq = FMath::Max(0.0f, ClickThresholdSq);
	if (!SelectorComponent)
	{
		SelectorComponent = PlayerController->FindComponentByClass<URTSSelector>();
	}

	URTSSelectionSubsystem* SelectionSubsystem = nullptr;
	if (const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		SelectionSubsystem = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>();
	}

	auto CollectSelectableActors = [PlayerController](
		const FVector2D& RectStart,
		const FVector2D& RectEnd,
		TArray<AActor*>& OutActors)
	{
		OutActors.Reset();
		TArray<AActor*> RawActors;

		// This helper can be called from input events, where AHUD::Canvas is not
		// valid. Project through the player controller instead of calling the
		// Canvas-backed AHUD::GetActorsInSelectionRectangle API.
		if (UWorld* World = PlayerController->GetWorld())
		{
			FBox2D SelectionRectangle(ForceInit);
			SelectionRectangle += RectStart;
			SelectionRectangle += RectEnd;

			static const FVector BoundsPointMapping[8] =
			{
				FVector(1.0f, 1.0f, 1.0f), FVector(1.0f, 1.0f, -1.0f),
				FVector(1.0f, -1.0f, 1.0f), FVector(1.0f, -1.0f, -1.0f),
				FVector(-1.0f, 1.0f, 1.0f), FVector(-1.0f, 1.0f, -1.0f),
				FVector(-1.0f, -1.0f, 1.0f), FVector(-1.0f, -1.0f, -1.0f)
			};

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!Actor || !Actor->FindComponentByClass<URTSSelectable>())
				{
					continue;
				}

				const FBox ActorBounds = Actor->GetComponentsBoundingBox(false);
				if (!ActorBounds.IsValid)
				{
					continue;
				}

				const FVector Center = ActorBounds.GetCenter();
				const FVector Extent = ActorBounds.GetExtent();
				FBox2D ActorScreenBounds(ForceInit);
				for (const FVector& PointMapping : BoundsPointMapping)
				{
					FVector2D ScreenPoint;
					if (PlayerController->ProjectWorldLocationToScreen(Center + PointMapping * Extent, ScreenPoint, false))
					{
						ActorScreenBounds += ScreenPoint;
					}
				}

				if (ActorScreenBounds.bIsValid && SelectionRectangle.Intersect(ActorScreenBounds))
				{
					RawActors.Add(Actor);
				}
			}
		}

		for (AActor* Actor : RawActors)
		{
			if (Actor && Actor->FindComponentByClass<URTSSelectable>())
			{
				OutActors.AddUnique(Actor);
			}
		}
	};

	auto CollectMassEntities = [PlayerController, SelectionSubsystem, ClickThresholdSq](
		const FVector2D& RectStart,
		const FVector2D& RectEnd,
		TArray<FEntityHandle>& OutEntities)
	{
		OutEntities.Reset();
		float MinX = FMath::Min(RectStart.X, RectEnd.X);
		float MinY = FMath::Min(RectStart.Y, RectEnd.Y);
		float MaxX = FMath::Max(RectStart.X, RectEnd.X);
		float MaxY = FMath::Max(RectStart.Y, RectEnd.Y);
		const float Width = MaxX - MinX;
		const float Height = MaxY - MinY;
		const bool bIsClick = Width * Width + Height * Height < ClickThresholdSq;
		if (bIsClick)
		{
			MinX -= 1.0f;
			MinY -= 1.0f;
			MaxX += 1.0f;
			MaxY += 1.0f;
		}

		const FVector2D ScreenPoints[4] =
		{
			FVector2D(MinX, MinY),
			FVector2D(MinX, MaxY),
			FVector2D(MaxX, MaxY),
			FVector2D(MaxX, MinY)
		};

		FViewTracePoints TracePoints;
		TracePoints.ViewPoint = PlayerController->PlayerCameraManager->GetCameraLocation();
		for (const FVector2D& ScreenPoint : ScreenPoints)
		{
			FVector WorldPosition;
			FVector WorldDirection;
			if (PlayerController->DeprojectScreenPositionToWorld(
				ScreenPoint.X, ScreenPoint.Y, WorldPosition, WorldDirection))
			{
				TracePoints.SelectionPoints.Add(WorldPosition + WorldDirection * 100000.0f);
			}
		}

		if (TracePoints.SelectionPoints.Num() != 4)
		{
			return;
		}

		bool bHit = false;
		TArray<FTraceResult> Results;
		const int32 KeepCount = bIsClick ? 1 : -1;
		const ESortMode SortMode = bIsClick ? ESortMode::NearToFar : ESortMode::None;
#if WITH_EDITOR
		FTraceDrawDebugConfig DebugConfig;
		DebugConfig.bDrawDebugShape = false;
		DebugConfig.Duration = 1.0f;
		UMassBattleFuncLib::ViewTraceForAgents(
			PlayerController, bHit, Results, KeepCount, TracePoints, false, FVector::ZeroVector,
			1.0f, SortMode, FVector::ZeroVector, FEntityArray(), FMassBattleQuery(), DebugConfig);
#else
		UMassBattleFuncLib::ViewTraceForAgents(
			PlayerController, bHit, Results, KeepCount, TracePoints, false, FVector::ZeroVector,
			1.0f, SortMode);
#endif

		if (bHit)
		{
			for (const FTraceResult& Result : Results)
			{
				if (!SelectionSubsystem || SelectionSubsystem->IsEntityControllable(Result.Entity))
				{
					OutEntities.AddUnique(Result.Entity);
				}
			}
		}
	};

	ERTSSelectionModifier Modifier = ERTSSelectionModifier::Replace;
	const float DragDistanceSq = FVector2D::DistSquared(StartPoint, EndPoint);
	const bool bIsClick = DragDistanceSq <= ClickThresholdSq;
	if (PlayerController->IsInputKeyDown(EKeys::LeftShift)
		|| PlayerController->IsInputKeyDown(EKeys::RightShift))
	{
		Modifier = ERTSSelectionModifier::Add;
	}

	TArray<AActor*> FinalActorSelection;
	TArray<FEntityHandle> FinalMassSelection;
	if (bIsClick)
	{
		AActor* ClickedActor = nullptr;
		FEntityHandle ClickedEntity;
		FVector ClickedLocation = FVector::ZeroVector;
		if (ResolveSingleSelectableAtScreenPosition(
			PlayerController,
			EndPoint,
			ClickedActor,
			ClickedEntity,
			ClickedLocation))
		{
			if (ClickedActor)
			{
				FinalActorSelection.Add(ClickedActor);
			}
			else if (ClickedEntity.IsSet())
			{
				FinalMassSelection.Add(ClickedEntity);
			}
		}
	}
	else
	{
		CollectSelectableActors(StartPoint, EndPoint, FinalActorSelection);
		CollectMassEntities(StartPoint, EndPoint, FinalMassSelection);
	}

	if (Modifier == ERTSSelectionModifier::Add && SelectionSubsystem && bIsClick)
	{
		if (FinalActorSelection.Num() == 1 && FinalMassSelection.IsEmpty()
			&& SelectionSubsystem->IsActorSelected(FinalActorSelection[0]))
		{
			Modifier = ERTSSelectionModifier::Remove;
		}
		else if (FinalActorSelection.IsEmpty() && FinalMassSelection.Num() == 1
			&& SelectionSubsystem->IsEntitySelected(FinalMassSelection[0]))
		{
			Modifier = ERTSSelectionModifier::Remove;
		}
	}

	if (SelectionSubsystem)
	{
		SelectionSubsystem->SetSelectedUnits(FinalActorSelection, FinalMassSelection, Modifier);
	}

	if (SelectorComponent)
	{
		const TArray<AActor*>& VisualActors = SelectionSubsystem
			? SelectionSubsystem->GetSelectedActors()
			: FinalActorSelection;
		SelectorComponent->HandleSelectedActors(VisualActors);
	}
}
