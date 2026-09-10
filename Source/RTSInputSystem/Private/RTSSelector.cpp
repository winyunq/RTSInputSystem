// Copyright 2024 Jesus Bracho All Rights Reserved.

#include "RTSSelector.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "RTSInputPanelSettings.h"
#include "RTSSelectable.h"
#include "RTSSelectionSubsystem.h"
#include "RTSCamera.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/InputComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/RTSBuildPlacementWireframeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MassBattleAgentComponent.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "FuncLibs/MassBattleFuncLib.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/SViewport.h"
#include "Renderers/MassBattleFxRenderer.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

DECLARE_STATS_GROUP(TEXT("RTS Input Feedback"), STATGROUP_RTSInputFeedback, STATCAT_Advanced);
DECLARE_CYCLE_STAT(
	TEXT("Selected Task Line Feedback"),
	STAT_RTSSelectedTaskLineFeedback,
	STATGROUP_RTSInputFeedback);
DECLARE_CYCLE_STAT(
	TEXT("Ground Command Pulse"),
	STAT_RTSGroundCommandPulse,
	STATGROUP_RTSInputFeedback);

static TAutoConsoleVariable<int32> CVarRTSSelectedTaskLineFeedback(
	TEXT("RTS.SelectionFeedback.TaskLines"),
	1,
	TEXT("Command-event task lines. Takes effect on the next selection or command. 0=off, 1=use project settings."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarRTSGroundCommandPulse(
	TEXT("RTS.CommandFeedback.GroundPulse"),
	1,
	TEXT("Event-submitted right-click ground marker. 0=off, 1=use project settings."),
	ECVF_Default);

class FRTSSelectorInputProcessor final : public IInputProcessor
{
public:
	explicit FRTSSelectorInputProcessor(URTSSelector* InSelector)
		: Selector(InSelector)
	{
	}

	virtual void Tick(
		const float DeltaTime,
		FSlateApplication& SlateApp,
		TSharedRef<ICursor> Cursor) override
	{
		// IInputProcessor requires Tick. Intentionally no business work here:
		// selector feedback changes only in discrete input-event callbacks.
		(void)DeltaTime;
		(void)SlateApp;
		(void)Cursor;
	}

	virtual bool HandleMouseMoveEvent(
		FSlateApplication& SlateApp,
		const FPointerEvent& MouseEvent) override
	{
		(void)SlateApp;
		(void)MouseEvent;
		if (URTSSelector* SelectorComponent = Selector.Get())
		{
			SelectorComponent->HandlePointerMoved();
		}
		return false;
	}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& KeyEvent) override
	{
		if (KeyEvent.GetKey() != EKeys::Tab || KeyEvent.IsShiftDown()
			|| KeyEvent.IsControlDown() || KeyEvent.IsAltDown() || KeyEvent.IsCommandDown())
		{
			return false;
		}
		URTSSelector* SelectorComponent = Selector.Get();
		APlayerController* PC = SelectorComponent ? SelectorComponent->PlayerController : nullptr;
		ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient : nullptr;
		if (!ViewportClient || ViewportClient->IgnoreInput() || PC->IsPaused())
		{
			return false;
		}
		const TSharedPtr<FSlateUser> User = LocalPlayer->GetSlateUser();
		const TSharedPtr<SViewport> Viewport = ViewportClient->GetGameViewportWidget();
		if (!User || User->GetUserIndex() != KeyEvent.GetUserIndex()
			|| !Viewport || !User->IsWidgetInFocusPath(Viewport))
		{
			// A PIE selector must not handle keys aimed at editor windows or another local player.
			return false;
		}
		const TSharedPtr<SWidget> Focused = User->GetFocusedWidget();
		if (Focused && Focused->GetTypeAsString().Contains(TEXT("EditableText")))
		{
			// Chat and text fields keep their own Tab handling.
			return false;
		}
		if (URTSSelectionSubsystem* Selection = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>())
		{
			// Selection owns Tab before Slate turns it into focus navigation. A held key stays consumed.
			if (!KeyEvent.IsRepeat()) Selection->CycleGroup();
			return true;
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override
	{
		return TEXT("RTSSelectorInput");
	}

private:
	TWeakObjectPtr<URTSSelector> Selector;
};

namespace
{
	enum class ERTSStrategyCursorStyle : uint8
	{
		Normal,
		Selectable,
		Targeting
	};

	class SRTSStrategyCursor final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRTSStrategyCursor)
			: _Style(ERTSStrategyCursorStyle::Normal)
		{
			_Visibility = EVisibility::HitTestInvisible;
		}
			SLATE_ARGUMENT(ERTSStrategyCursorStyle, Style)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Style = InArgs._Style;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			// SViewport centers software cursor widgets on the actual cursor
			// position. The pointer tip below is therefore drawn at this
			// widget's exact center so selection rays and clicks land there.
			return FVector2D(44.0f, 44.0f);
		}

		virtual int32 OnPaint(
			const FPaintArgs& Args,
			const FGeometry& AllottedGeometry,
			const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements,
			int32 LayerId,
			const FWidgetStyle& InWidgetStyle,
			bool bParentEnabled) const override
		{
			(void)Args;
			(void)MyCullingRect;
			(void)InWidgetStyle;
			(void)bParentEnabled;

			const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
			const FVector2f Hotspot(22.0f, 22.0f);
			const FLinearColor Accent =
				Style == ERTSStrategyCursorStyle::Selectable
					? FLinearColor(0.24f, 1.0f, 0.44f, 1.0f)
					: (Style == ERTSStrategyCursorStyle::Targeting
						? FLinearColor(1.0f, 0.34f, 0.10f, 1.0f)
						: FLinearColor(0.78f, 0.90f, 0.98f, 1.0f));
			const FLinearColor EdgeColor(0.015f, 0.025f, 0.045f, 0.98f);

			// Compact asymmetric RTS pointer. The first/last point is the exact
			// input hotspot; the body grows down-right like a conventional
			// cursor without visually displacing the click point.
			const TArray<FVector2f> Arrow =
			{
				Hotspot,
				FVector2f(23.2f, 38.0f),
				FVector2f(27.6f, 34.0f),
				FVector2f(32.3f, 42.0f),
				FVector2f(36.2f, 39.8f),
				FVector2f(31.5f, 31.8f),
				FVector2f(40.0f, 30.4f),
				Hotspot
			};
			TArray<FVector2f> ShadowArrow;
			ShadowArrow.Reserve(Arrow.Num());
			for (const FVector2f& Point : Arrow)
			{
				ShadowArrow.Add(Point + FVector2f(1.25f, 1.5f));
			}

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId,
				PaintGeometry,
				MoveTemp(ShadowArrow),
				ESlateDrawEffect::None,
				FLinearColor(0.0f, 0.0f, 0.0f, 0.72f),
				true,
				7.0f);
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 1,
				PaintGeometry,
				Arrow,
				ESlateDrawEffect::None,
				EdgeColor,
				true,
				5.5f);
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 2,
				PaintGeometry,
				Arrow,
				ESlateDrawEffect::None,
				Accent,
				true,
				2.7f);

			if (Style != ERTSStrategyCursorStyle::Normal)
			{
				// Three restrained acquisition corners make the selectable
				// state readable while leaving the down-right pointer body
				// and its exact hotspot unobstructed.
				const TArray<TArray<FVector2f>> AcquisitionCorners =
				{
					{ Hotspot + FVector2f(-5.0f, -10.0f), Hotspot + FVector2f(-10.0f, -10.0f), Hotspot + FVector2f(-10.0f, -5.0f) },
					{ Hotspot + FVector2f(5.0f, -10.0f), Hotspot + FVector2f(10.0f, -10.0f), Hotspot + FVector2f(10.0f, -5.0f) },
					{ Hotspot + FVector2f(-10.0f, 5.0f), Hotspot + FVector2f(-10.0f, 10.0f), Hotspot + FVector2f(-5.0f, 10.0f) }
				};
				for (const TArray<FVector2f>& Corner : AcquisitionCorners)
				{
					FSlateDrawElement::MakeLines(
						OutDrawElements,
						LayerId + 3,
						PaintGeometry,
						Corner,
						ESlateDrawEffect::None,
						EdgeColor,
						true,
						4.5f);
					FSlateDrawElement::MakeLines(
						OutDrawElements,
						LayerId + 4,
						PaintGeometry,
						Corner,
						ESlateDrawEffect::None,
						Accent,
						true,
						1.8f);
				}
			}
			return LayerId + 4;
		}

	private:
		ERTSStrategyCursorStyle Style = ERTSStrategyCursorStyle::Normal;
	};

	const FName LegacyBuildPlacementSurfaceComponentName(TEXT("RTSHashGridSelectionSurface"));
	const FName BuildPlacementFootprintComponentName(TEXT("RTSBuildPlacementFootprintCells"));
	const FName BuildPlacementModelComponentName(TEXT("RTSBuildPlacementModel"));
	const FName BuildPlacementGuidanceDiscComponentName(TEXT("RTSBuildPlacementGuidanceDisc"));
	const FName BuildPlacementColorParameterName(TEXT("GridColor"));
	const FName BuildPlacementOpacityParameterName(TEXT("GridOpacity"));
	const FName BuildPlacementWorldToGridScaleParameterName(TEXT("WorldToGridScale"));
	constexpr int32 MaxBuildPlacementGuidanceRadiusCells = 7;
	constexpr int32 MaxBuildPlacementGuidanceFadeBands = 6;
	constexpr int32 BuildPlacementGuidanceCircleSegments = 96;
	constexpr float BuildPlacementFootprintCellFill = 0.92f;

	struct FBuildPlacementGuidanceMeshSection
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> VertexColors;
		TArray<FProcMeshTangent> Tangents;
	};

	UProceduralMeshComponent* FindBuildPlacementGuidanceDisc(AActor* PreviewActor)
	{
		return PreviewActor
			? FindObjectFast<UProceduralMeshComponent>(
				PreviewActor,
				BuildPlacementGuidanceDiscComponentName)
			: nullptr;
	}

	UProceduralMeshComponent* GetOrCreateBuildPlacementGuidanceDisc(AActor* PreviewActor)
	{
		if (UProceduralMeshComponent* Existing =
			FindBuildPlacementGuidanceDisc(PreviewActor))
		{
			return Existing;
		}

		UWorld* World = PreviewActor ? PreviewActor->GetWorld() : nullptr;
		if (!PreviewActor || !World)
		{
			return nullptr;
		}

		UProceduralMeshComponent* Guidance = NewObject<UProceduralMeshComponent>(
			PreviewActor,
			BuildPlacementGuidanceDiscComponentName,
			RF_Transient);
		if (!Guidance)
		{
			return nullptr;
		}

		PreviewActor->AddInstanceComponent(Guidance);
		Guidance->SetMobility(EComponentMobility::Movable);
		Guidance->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Guidance->SetGenerateOverlapEvents(false);
		Guidance->SetCanEverAffectNavigation(false);
		Guidance->SetCastShadow(false);
		Guidance->SetReceivesDecals(false);
		Guidance->SetTranslucentSortPriority(80);
		Guidance->SetVisibility(false, true);
		Guidance->SetHiddenInGame(false, true);
		Guidance->bUseAsyncCooking = false;

		UMaterialInterface* SurfaceMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/RTSInputSystem/Feedback/M_RTSBuildPlacementGridSurface.M_RTSBuildPlacementGridSurface"));
		if (SurfaceMaterial)
		{
			for (int32 MaterialIndex = 0;
				MaterialIndex < MaxBuildPlacementGuidanceFadeBands * 2;
				++MaterialIndex)
			{
				Guidance->SetMaterial(
					MaterialIndex,
					UMaterialInstanceDynamic::Create(SurfaceMaterial, Guidance));
			}
		}

		Guidance->RegisterComponentWithWorld(World);
		return Guidance;
	}

	TArray<FVector2D> ClipBuildPlacementGuidePolygonToAxis(
		const TArray<FVector2D>& Input,
		bool bXAxis,
		float Boundary,
		bool bKeepGreater)
	{
		TArray<FVector2D> Output;
		if (Input.IsEmpty())
		{
			return Output;
		}

		Output.Reserve(Input.Num() + 2);
		FVector2D Previous = Input.Last();
		float PreviousValue = bXAxis ? Previous.X : Previous.Y;
		bool bPreviousInside = bKeepGreater
			? PreviousValue >= Boundary
			: PreviousValue <= Boundary;

		for (const FVector2D& Current : Input)
		{
			const float CurrentValue = bXAxis ? Current.X : Current.Y;
			const bool bCurrentInside = bKeepGreater
				? CurrentValue >= Boundary
				: CurrentValue <= Boundary;

			if (bCurrentInside != bPreviousInside)
			{
				const float Denominator = CurrentValue - PreviousValue;
				const float Alpha = FMath::IsNearlyZero(Denominator)
					? 0.0f
					: FMath::Clamp(
						(Boundary - PreviousValue) / Denominator,
						0.0f,
						1.0f);
				Output.Add(FMath::Lerp(Previous, Current, Alpha));
			}
			if (bCurrentInside)
			{
				Output.Add(Current);
			}

			Previous = Current;
			PreviousValue = CurrentValue;
			bPreviousInside = bCurrentInside;
		}

		return Output;
	}

	TArray<FVector2D> ClipBuildPlacementGuideCircleToCell(
		const TArray<FVector2D>& CirclePolygon,
		float MinX,
		float MaxX,
		float MinY,
		float MaxY)
	{
		TArray<FVector2D> Polygon = ClipBuildPlacementGuidePolygonToAxis(
			CirclePolygon, true, MinX, true);
		Polygon = ClipBuildPlacementGuidePolygonToAxis(
			Polygon, true, MaxX, false);
		Polygon = ClipBuildPlacementGuidePolygonToAxis(
			Polygon, false, MinY, true);
		return ClipBuildPlacementGuidePolygonToAxis(
			Polygon, false, MaxY, false);
	}

	URTSBuildPlacementWireframeComponent* FindBuildPlacementModel(
		AActor* PreviewActor)
	{
		return PreviewActor
			? FindObjectFast<URTSBuildPlacementWireframeComponent>(
				PreviewActor,
				BuildPlacementModelComponentName)
			: nullptr;
	}

	URTSBuildPlacementWireframeComponent* GetOrCreateBuildPlacementModel(
		AActor* PreviewActor)
	{
		if (URTSBuildPlacementWireframeComponent* Existing =
			FindBuildPlacementModel(PreviewActor))
		{
			return Existing;
		}

		UWorld* World = PreviewActor ? PreviewActor->GetWorld() : nullptr;
		if (!PreviewActor || !World)
		{
			return nullptr;
		}

		URTSBuildPlacementWireframeComponent* Model =
			NewObject<URTSBuildPlacementWireframeComponent>(
			PreviewActor,
			BuildPlacementModelComponentName,
			RF_Transient);
		if (!Model)
		{
			return nullptr;
		}

		PreviewActor->AddInstanceComponent(Model);
		Model->SetMobility(EComponentMobility::Movable);
		Model->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Model->SetGenerateOverlapEvents(false);
		Model->SetCanEverAffectNavigation(false);
		Model->SetCastShadow(false);
		Model->SetReceivesDecals(false);
		Model->SetTranslucentSortPriority(140);
		Model->SetVisibility(false, true);
		Model->SetHiddenInGame(false, true);
		Model->RegisterComponentWithWorld(World);
		return Model;
	}

	void ConfigureBuildPlacementModel(AActor* PreviewActor, UStaticMesh* PreviewMesh)
	{
		if (!PreviewMesh)
		{
			if (URTSBuildPlacementWireframeComponent* Existing =
				FindBuildPlacementModel(PreviewActor))
			{
				Existing->SetStaticMesh(nullptr);
				Existing->SetVisibility(false, true);
			}
			return;
		}

		if (URTSBuildPlacementWireframeComponent* Model =
			GetOrCreateBuildPlacementModel(PreviewActor))
		{
			Model->SetStaticMesh(PreviewMesh);
			Model->SetWorldScale3D(FVector::OneVector);
			Model->SetVisibility(true, true);
		}
	}

	void UpdateBuildPlacementModel(
		AActor* PreviewActor,
		const FRTSHashGridSelectionResult& Result,
		const URTSInputPanelSettings* Settings)
	{
		URTSBuildPlacementWireframeComponent* Model =
			FindBuildPlacementModel(PreviewActor);
		if (!Model || !Model->GetStaticMesh())
		{
			return;
		}

		// Keep the authored building distinct from both grid layers. A slight
		// lift leaves the green/red ground footprint readable under the model.
		Model->SetWorldLocationAndRotation(
			Result.WorldLocation + FVector(0.0f, 0.0f, 2.5f),
			FRotator::ZeroRotator);
		const FLinearColor PreviewColor = Result.bIsValidPlacement
			? (Settings
				? Settings->BuildPlacementValidColor
				: FLinearColor(0.05f, 0.80f, 0.10f, 0.55f))
			: (Settings
				? Settings->BuildPlacementInvalidColor
				: FLinearColor(1.0f, 0.05f, 0.03f, 0.55f));
		Model->SetPlacementWireframeColor(PreviewColor);
		Model->SetVisibility(true, true);
	}

	void HideBuildPlacementModel(AActor* PreviewActor)
	{
		if (URTSBuildPlacementWireframeComponent* Model =
			FindBuildPlacementModel(PreviewActor))
		{
			Model->SetVisibility(false, true);
		}
	}

	UStaticMeshComponent* FindLegacyBuildPlacementSurface(AActor* PreviewActor)
	{
		return PreviewActor
			? FindObjectFast<UStaticMeshComponent>(
				PreviewActor,
				LegacyBuildPlacementSurfaceComponentName)
			: nullptr;
	}

	UInstancedStaticMeshComponent* FindBuildPlacementFootprint(AActor* PreviewActor)
	{
		return PreviewActor
			? FindObjectFast<UInstancedStaticMeshComponent>(
				PreviewActor,
				BuildPlacementFootprintComponentName)
			: nullptr;
	}

	UInstancedStaticMeshComponent* GetOrCreateBuildPlacementFootprint(AActor* PreviewActor)
	{
		if (UInstancedStaticMeshComponent* Existing = FindBuildPlacementFootprint(PreviewActor))
		{
			return Existing;
		}

		UWorld* World = PreviewActor ? PreviewActor->GetWorld() : nullptr;
		if (!PreviewActor || !World)
		{
			return nullptr;
		}

		UInstancedStaticMeshComponent* Footprint = NewObject<UInstancedStaticMeshComponent>(
			PreviewActor,
			BuildPlacementFootprintComponentName,
			RF_Transient);
		if (!Footprint)
		{
			return nullptr;
		}

		PreviewActor->AddInstanceComponent(Footprint);
		Footprint->SetMobility(EComponentMobility::Movable);
		Footprint->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Footprint->SetGenerateOverlapEvents(false);
		Footprint->SetCanEverAffectNavigation(false);
		Footprint->SetCastShadow(false);
		Footprint->SetReceivesDecals(false);
		Footprint->SetTranslucentSortPriority(120);
		Footprint->SetVisibility(false, true);
		Footprint->SetHiddenInGame(false, true);

		UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(
			nullptr,
			TEXT("/Engine/BasicShapes/Plane.Plane"));
		UMaterialInterface* SurfaceMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/RTSInputSystem/Feedback/M_RTSBuildPlacementGridSurface.M_RTSBuildPlacementGridSurface"));
		if (PlaneMesh)
		{
			Footprint->SetStaticMesh(PlaneMesh);
		}
		if (SurfaceMaterial)
		{
			Footprint->SetMaterial(
				0,
				UMaterialInstanceDynamic::Create(SurfaceMaterial, Footprint));
		}

		Footprint->RegisterComponentWithWorld(World);
		return Footprint;
	}

	bool UpdateBuildPlacementFootprint(
		AActor* PreviewActor,
		const FRTSHashGridSelectionResult& Result,
		const URTSInputPanelSettings* Settings)
	{
		// A live-coded editor session can still own the superseded single
		// rectangular surface. Keep it hidden; the exact occupied cells below
		// are the authoritative green/red footprint.
		if (UStaticMeshComponent* LegacySurface =
			FindLegacyBuildPlacementSurface(PreviewActor))
		{
			LegacySurface->SetVisibility(false, true);
		}

		UInstancedStaticMeshComponent* Footprint =
			GetOrCreateBuildPlacementFootprint(PreviewActor);
		if (!Footprint || !Footprint->GetStaticMesh() || !Footprint->GetMaterial(0))
		{
			return false;
		}

		const int32 CellsX = FMath::Max(1, FMath::RoundToInt(Result.FootprintCells.X));
		const int32 CellsY = FMath::Max(1, FMath::RoundToInt(Result.FootprintCells.Y));
		const int32 QuantizedCellSize = FMath::RoundToInt(Result.CellSize * 1000.0f);
		// Cache only the local cell layout; moving the cursor moves the component
		// as a whole, so a 6x6 or 8x8 footprint is not rebuilt every frame.
		const FName LayoutTag(*FString::Printf(
			TEXT("RTSFootprintLayout_%d_%d_%d"),
			CellsX,
			CellsY,
			QuantizedCellSize));

		if (!Footprint->ComponentHasTag(LayoutTag))
		{
			Footprint->ClearInstances();
			Footprint->ComponentTags.RemoveAll([](const FName& Tag)
			{
				return Tag.ToString().StartsWith(TEXT("RTSFootprintLayout_"));
			});

			TArray<FTransform> CellTransforms;
			CellTransforms.Reserve(CellsX * CellsY);
			const FVector CellScale(
				Result.CellSize * BuildPlacementFootprintCellFill / 100.0f,
				Result.CellSize * BuildPlacementFootprintCellFill / 100.0f,
				1.0f);
			for (int32 CellY = 0; CellY < CellsY; ++CellY)
			{
				for (int32 CellX = 0; CellX < CellsX; ++CellX)
				{
					const FVector CellLocation(
						(static_cast<float>(CellX) + 0.5f - CellsX * 0.5f)
							* Result.CellSize,
						(static_cast<float>(CellY) + 0.5f - CellsY * 0.5f)
							* Result.CellSize,
						0.0f);
					CellTransforms.Emplace(
						FRotator::ZeroRotator,
						CellLocation,
						CellScale);
				}
			}
			Footprint->AddInstances(CellTransforms, false, false, false);
			Footprint->ComponentTags.Add(LayoutTag);
		}

		Footprint->SetWorldLocationAndRotation(
			Result.WorldLocation + FVector(0.0f, 0.0f, 1.5f),
			FRotator::ZeroRotator);
		Footprint->SetWorldScale3D(FVector::OneVector);

		if (UMaterialInstanceDynamic* Material =
			Cast<UMaterialInstanceDynamic>(Footprint->GetMaterial(0)))
		{
			const FLinearColor Color = Result.bIsValidPlacement
				? (Settings
					? Settings->BuildPlacementValidColor
					: FLinearColor(0.05f, 0.80f, 0.10f, 0.55f))
				: (Settings
					? Settings->BuildPlacementInvalidColor
					: FLinearColor(1.0f, 0.05f, 0.03f, 0.55f));
			Material->SetVectorParameterValue(BuildPlacementColorParameterName, Color);
			Material->SetScalarParameterValue(BuildPlacementOpacityParameterName, Color.A);
			Material->SetScalarParameterValue(
				BuildPlacementWorldToGridScaleParameterName,
				1.0f / FMath::Max(1.0f, Result.CellSize));
		}

		Footprint->SetVisibility(true, true);
		return true;
	}

	void HideBuildPlacementFootprint(AActor* PreviewActor)
	{
		if (UInstancedStaticMeshComponent* Footprint = FindBuildPlacementFootprint(PreviewActor))
		{
			Footprint->SetVisibility(false, true);
		}
		if (UStaticMeshComponent* LegacySurface =
			FindLegacyBuildPlacementSurface(PreviewActor))
		{
			LegacySurface->SetVisibility(false, true);
		}
	}

	uint32 GetBuildPlacementLineBatchId(const URTSSelector* Selector)
	{
		// Batch ID zero means "untracked" to ULineBatchComponent. Keep each
		// local selector isolated so split-screen previews cannot clear each other.
		return PointerHash(Selector, 0x52545342u) | 1u;
	}

	uint32 GetSelectedPathLineBatchId(const URTSSelector* Selector)
	{
		return PointerHash(Selector, 0x52545350u) | 1u;
	}

	uint32 GetMoveCommandFeedbackLineBatchId(
		const URTSSelector* Selector,
		const uint32 Sequence)
	{
		return PointerHash(
			Selector,
			0x5254534Du ^ Sequence * 0x9E3779B9u) | 1u;
	}

	bool IsMovePathCommand(const FGameplayTag& CommandTag)
	{
		const FName TagName = CommandTag.GetTagName();
		return TagName == FName(TEXT("RTS.Command.Move"))
			|| TagName == FName(TEXT("RTS.Command.Patrol"));
	}

	bool IsAttackPathCommand(const FGameplayTag& CommandTag)
	{
		return CommandTag.GetTagName() == FName(TEXT("RTS.Command.Attack"));
	}

	bool IsSelectorComposableContextCommand(const FGameplayTag& CommandTag)
	{
		const FName TagName = CommandTag.GetTagName();
		return TagName == FName(TEXT("RTS.Command.Move"))
			|| TagName == FName(TEXT("RTS.Command.Attack"))
			|| TagName == FName(TEXT("RTS.Command.Patrol"));
	}

}

// Sets default values for this component's properties
URTSSelector::URTSSelector(): PlayerController(nullptr), HUD(nullptr), bIsSelecting(false)
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// Add defaults for input actions
	static ConstructorHelpers::FObjectFinder<UInputAction>
		BeginSelectionActionFinder(TEXT("/RTSInputSystem/Inputs/BeginSelection"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext>
		InputMappingContextFinder(TEXT("/RTSInputSystem/Inputs/RTSInputSystemInputs"));
	this->BeginSelection = BeginSelectionActionFinder.Object;
	this->InputMappingContext = InputMappingContextFinder.Object;

	static ConstructorHelpers::FObjectFinder<UInputAction>
		IssueCommandActionFinder(TEXT("/RTSInputSystem/Inputs/IssueCommand"));
	if (IssueCommandActionFinder.Succeeded())
	{
		this->IssueCommandAction = IssueCommandActionFinder.Object;
	}
}


// Called when the game starts
void URTSSelector::BeginPlay()
{
	Super::BeginPlay();

	const auto NetMode = this->GetNetMode();
	if (NetMode != NM_DedicatedServer)
	{
		this->CollectComponentDependencyReferences();
		this->RegisterSelectionInputProcessor();
		this->InstallStrategyMouseCursors();
		this->EnsureSelectionFxRenderer();
		this->BindInputMappingContext();
		this->BindInputActions();
		this->RegisterControlGroupHotkeys();
		this->TryBindTopSelectButtons();
		if (UGameViewportSubsystem* ViewportSubsystem =
			UGameViewportSubsystem::Get())
		{
			ViewportWidgetAddedDelegateHandle =
				ViewportSubsystem->OnWidgetAdded.AddUObject(
				this,
				&URTSSelector::HandleViewportWidgetAdded);
		}
		OnActorsSelected.AddDynamic(this, &URTSSelector::HandleSelectedActors);

		if (PlayerController && PlayerController->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* Selection = PlayerController->GetLocalPlayer()->GetSubsystem<URTSSelectionSubsystem>())
			{
				Selection->OnSelectionChanged.AddUniqueDynamic(this, &URTSSelector::HandleSubsystemSelectionChanged);
				Selection->OnControlGroupFocusRequested.AddUniqueDynamic(this, &URTSSelector::HandleControlGroupFocusRequested);
				CommandFeedbackDelegateHandle = Selection->OnCommandFeedbackIssued.AddUObject(
					this,
					&URTSSelector::HandleCommandFeedbackIssued);
				HandleSelectedActors(Selection->GetSelectedActors());
			}
		}
	}
}

void URTSSelector::EnsureSelectionFxRenderer()
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	UWorld* World = GetWorld();
	if (!Settings
		|| !Settings->bAutoSpawnSelectionFxRenderer
		|| Settings->SelectionFxRendererClass.IsNull()
		|| !World)
	{
		return;
	}

	UClass* RendererClass = Settings->SelectionFxRendererClass.LoadSynchronous();
	if (!RendererClass
		|| !RendererClass->IsChildOf(AMassBattleFxRenderer::StaticClass()))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("RTSSelector: SelectionFxRendererClass is missing or is not a MassBattle FX renderer."));
		return;
	}

	// This is a one-time scan over the small renderer-actor set. It never
	// traverses selected Actors, Mass entities, or Mass fragments.
	for (TActorIterator<AMassBattleFxRenderer> It(World); It; ++It)
	{
		if (It->IsA(RendererClass))
		{
			return;
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.ObjectFlags |= RF_Transient;
	AMassBattleFxRenderer* Renderer = World->SpawnActor<AMassBattleFxRenderer>(
		RendererClass,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParameters);
	if (!Renderer)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("RTSSelector: Failed to spawn the shared selected-unit Batch FX renderer."));
	}
}

void URTSSelector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ViewportWidgetAddedDelegateHandle.IsValid())
	{
		if (UGameViewportSubsystem* ViewportSubsystem =
			UGameViewportSubsystem::Get())
		{
			ViewportSubsystem->OnWidgetAdded.Remove(
				ViewportWidgetAddedDelegateHandle);
		}
		ViewportWidgetAddedDelegateHandle.Reset();
	}
	UnregisterSelectionInputProcessor();
	ClearSelectableHoverPreview(false);
	RestoreStrategyMouseCursors();

	if (PlayerController && PlayerController->GetLocalPlayer())
	{
		if (URTSSelectionSubsystem* Selection = PlayerController->GetLocalPlayer()->GetSubsystem<URTSSelectionSubsystem>())
		{
			Selection->OnSelectionChanged.RemoveDynamic(this, &URTSSelector::HandleSubsystemSelectionChanged);
			Selection->OnControlGroupFocusRequested.RemoveDynamic(this, &URTSSelector::HandleControlGroupFocusRequested);
			Selection->OnCommandFeedbackIssued.Remove(CommandFeedbackDelegateHandle);
			CommandFeedbackDelegateHandle.Reset();
		}
	}

	UnregisterControlGroupHotkeys();
	UnbindTopSelectButtons();
	ClearSelectedPathPreview();
	ClearMoveCommandFeedback();
	EndHashGridSelectionPreview();
	DestroyBuildPlacementPreviewActor();
	Super::EndPlay(EndPlayReason);
}

void URTSSelector::HandleSelectedActors_Implementation(const TArray<AActor*>& NewSelectedActors)
{
	TArray<URTSSelectable*> NewSelectableComponents;
	TSet<URTSSelectable*> NewSelectableSet;
	for (AActor* Actor : NewSelectedActors)
	{
		if (Actor && this->CanSelectActor(Actor))
		{
			if (URTSSelectable* Selectable = Actor->FindComponentByClass<URTSSelectable>())
			{
				NewSelectableComponents.AddUnique(Selectable);
				NewSelectableSet.Add(Selectable);
			}
		}
	}

	for (URTSSelectable* Selected : this->SelectedActors)
	{
		if (Selected && !NewSelectableSet.Contains(Selected))
		{
			Selected->OnDeselected();
		}
	}

	for (URTSSelectable* Selected : NewSelectableComponents)
	{
		if (Selected && !this->SelectedActors.Contains(Selected))
		{
			Selected->OnSelected();
		}
	}

	this->SelectedActors = MoveTemp(NewSelectableComponents);
}

void URTSSelector::ClearSelectedActors_Implementation()
{
	for (URTSSelectable* Selected : this->SelectedActors)
	{
		if (Selected)
		{
			Selected->OnDeselected();
		}
	}
	this->SelectedActors.Empty();
}

void URTSSelector::HandleSubsystemSelectionChanged(const FRTSSelectionView& SelectionView)
{
	(void)SelectionView;
	ClearSelectableHoverPreview(false);
	ClearSelectedPathPreview();
	if (PlayerController && PlayerController->GetLocalPlayer())
	{
		if (URTSSelectionSubsystem* Selection = PlayerController->GetLocalPlayer()->GetSubsystem<URTSSelectionSubsystem>())
		{
			HandleSelectedActors(Selection->GetSelectedActors());
		}
	}
}

void URTSSelector::HandleControlGroupFocusRequested(int32 GroupIndex, FVector WorldCenter)
{
	(void)GroupIndex;
	SelectedTaskRouteAnchor = WorldCenter;
	bHasSelectedTaskRouteAnchor = true;
	if (!PlayerController)
	{
		return;
	}

	AActor* CameraActors[] = { PlayerController->GetViewTarget(), PlayerController->GetPawn() };
	for (AActor* CameraActor : CameraActors)
	{
		if (CameraActor)
		{
			if (URTSCamera* Camera = CameraActor->FindComponentByClass<URTSCamera>())
			{
				Camera->jumpTo(WorldCenter);
				return;
			}
		}
	}
}

void URTSSelector::TryBindTopSelectButtons()
{
	if (BoundTopSelectWidget.IsValid())
	{
		return;
	}

	UClass* TopSelectClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/HeadUpDisplay/TopSelect.TopSelect_C"));
	if (!TopSelectClass)
	{
		return;
	}

	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, TopSelectClass, false);
	if (Widgets.IsEmpty() || !Widgets[0])
	{
		return;
	}

	BoundTopSelectWidget = Widgets[0];
	BindTopSelectButton(TEXT("Button_1"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllArtillery));
	BindTopSelectButton(TEXT("Button_2"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllAircraft));
	BindTopSelectButton(TEXT("Button_3"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllNaval));
	BindTopSelectButton(TEXT("Button_11"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllDefense));
	BindTopSelectButton(TEXT("Button"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllArmor));
	BindTopSelectButton(TEXT("Button_0"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllInfantry));
	BindTopSelectButton(TEXT("Button_8"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllGroundArmy));
	BindTopSelectButton(TEXT("Button_10"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllEngineers));
	BindTopSelectButton(TEXT("Button_5"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllCities));
	BindTopSelectButton(TEXT("Button_9"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllUniversities));
	BindTopSelectButton(TEXT("Button_6"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllResearch));
	BindTopSelectButton(TEXT("Button_7"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllGovernment));
	BindTopSelectButton(TEXT("Button_12"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllPorts));
	BindTopSelectButton(TEXT("Button_13"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllAirports));
	BindTopSelectButton(TEXT("Button_14"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllBarracks));
	BindTopSelectButton(TEXT("Button_15"), GET_FUNCTION_NAME_CHECKED(URTSSelector, SelectAllMilitaryCamps));
}

void URTSSelector::HandleViewportWidgetAdded(
	UWidget* Widget,
	ULocalPlayer* LocalPlayer)
{
	(void)Widget;
	if (!PlayerController
		|| (LocalPlayer && LocalPlayer != PlayerController->GetLocalPlayer()))
	{
		return;
	}
	TryBindTopSelectButtons();
}

void URTSSelector::UnbindTopSelectButtons()
{
	UUserWidget* TopSelect = BoundTopSelectWidget.Get();
	if (!TopSelect || !TopSelect->WidgetTree)
	{
		BoundTopSelectWidget.Reset();
		return;
	}

	static const FName ButtonNames[] =
	{
		TEXT("Button_1"), TEXT("Button_2"), TEXT("Button_3"), TEXT("Button_11"),
		TEXT("Button"), TEXT("Button_0"), TEXT("Button_8"), TEXT("Button_10"),
		TEXT("Button_5"), TEXT("Button_9"), TEXT("Button_6"), TEXT("Button_7"),
		TEXT("Button_12"), TEXT("Button_13"), TEXT("Button_14"), TEXT("Button_15")
	};
	for (const FName ButtonName : ButtonNames)
	{
		if (UButton* Button = Cast<UButton>(TopSelect->WidgetTree->FindWidget(ButtonName)))
		{
			Button->OnClicked.RemoveAll(this);
		}
	}
	BoundTopSelectWidget.Reset();
}

void URTSSelector::BindTopSelectButton(FName WidgetName, FName HandlerName)
{
	UUserWidget* TopSelect = BoundTopSelectWidget.Get();
	UButton* Button = TopSelect && TopSelect->WidgetTree
		? Cast<UButton>(TopSelect->WidgetTree->FindWidget(WidgetName))
		: nullptr;
	if (!Button)
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSSelector: TopSelect is missing %s"), *WidgetName.ToString());
		return;
	}

	FScriptDelegate Handler;
	Handler.BindUFunction(this, HandlerName);
	Button->OnClicked.AddUnique(Handler);
}

void URTSSelector::SelectTopCategory(const TCHAR* TagName)
{
	if (!PlayerController || !PlayerController->GetLocalPlayer())
	{
		return;
	}

	if (URTSSelectionSubsystem* Selection = PlayerController->GetLocalPlayer()->GetSubsystem<URTSSelectionSubsystem>())
	{
		FRTSSelectionQuery Query;
		Query.RequiredSelectionTag = FGameplayTag::RequestGameplayTag(FName(TagName), false);
		const FGameplayTag ArmyRootTag = FGameplayTag::RequestGameplayTag(
			FName(TEXT("RTS.Selection.Army")), false);
		if (ArmyRootTag.IsValid() && Query.RequiredSelectionTag.MatchesTag(ArmyRootTag))
		{
			Query.ExcludedSelectionTag = FGameplayTag::RequestGameplayTag(
				FName(TEXT("RTS.Selection.Structure")), false);
		}
		Query.bIncludeMassEntities = true;
		Query.bIncludeActorUnits = false;
		Selection->SelectUnitsByQuery(Query, ERTSSelectionModifier::Replace);
	}
}

void URTSSelector::SelectAllArtillery() { SelectTopCategory(TEXT("RTS.Selection.Army.Ground.Artillery")); }
void URTSSelector::SelectAllAircraft() { SelectTopCategory(TEXT("RTS.Selection.Army.Air")); }
void URTSSelector::SelectAllNaval() { SelectTopCategory(TEXT("RTS.Selection.Army.Naval")); }
void URTSSelector::SelectAllDefense() { SelectTopCategory(TEXT("RTS.Selection.Structure.Defense")); }
void URTSSelector::SelectAllArmor() { SelectTopCategory(TEXT("RTS.Selection.Army.Ground.Armor")); }
void URTSSelector::SelectAllInfantry() { SelectTopCategory(TEXT("RTS.Selection.Army.Ground.Infantry")); }
void URTSSelector::SelectAllGroundArmy() { SelectTopCategory(TEXT("RTS.Selection.Army.Ground")); }
void URTSSelector::SelectAllEngineers() { SelectTopCategory(TEXT("RTS.Selection.Army.Ground.Engineer")); }
void URTSSelector::SelectAllCities() { SelectTopCategory(TEXT("RTS.Selection.Structure.City")); }
void URTSSelector::SelectAllUniversities() { SelectTopCategory(TEXT("RTS.Selection.Structure.University")); }
void URTSSelector::SelectAllResearch() { SelectTopCategory(TEXT("RTS.Selection.Structure.Research")); }
void URTSSelector::SelectAllGovernment() { SelectTopCategory(TEXT("RTS.Selection.Structure.Government")); }
void URTSSelector::SelectAllPorts() { SelectTopCategory(TEXT("RTS.Selection.Structure.Port")); }
void URTSSelector::SelectAllAirports() { SelectTopCategory(TEXT("RTS.Selection.Structure.Airport")); }
void URTSSelector::SelectAllBarracks() { SelectTopCategory(TEXT("RTS.Selection.Structure.Barracks")); }
void URTSSelector::SelectAllMilitaryCamps() { SelectTopCategory(TEXT("RTS.Selection.Structure.MilitaryCamp")); }

void URTSSelector::RegisterSelectionInputProcessor()
{
	if (SelectionInputProcessor.IsValid()
		|| !FSlateApplication::IsInitialized())
	{
		return;
	}
	SelectionInputProcessor =
		MakeShared<FRTSSelectorInputProcessor>(this);
	FSlateApplication::Get().RegisterInputPreProcessor(
		SelectionInputProcessor);
}

void URTSSelector::UnregisterSelectionInputProcessor()
{
	if (SelectionInputProcessor.IsValid()
		&& FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(
			SelectionInputProcessor);
	}
	SelectionInputProcessor.Reset();
}

void URTSSelector::HandlePointerMoved()
{
	if (!PlayerController)
	{
		return;
	}

	FVector2D MousePosition = FVector2D::ZeroVector;
	if (!PlayerController->GetMousePosition(
		MousePosition.X,
		MousePosition.Y))
	{
		bHasProcessedPointerPixel = false;
		ClearSelectableHoverPreview(true);
		return;
	}

	const FIntPoint PointerPixel(
		FMath::RoundToInt(MousePosition.X),
		FMath::RoundToInt(MousePosition.Y));
	if (bHasProcessedPointerPixel
		&& PointerPixel == LastProcessedPointerPixel)
	{
		return;
	}
	bHasProcessedPointerPixel = true;
	LastProcessedPointerPixel = PointerPixel;

	AActor* CameraActors[] =
	{
		PlayerController->GetViewTarget(),
		PlayerController->GetPawn()
	};
	AActor* LastCameraActor = nullptr;
	bool bForwardedToCameraFrameDriver = false;
	for (AActor* CameraActor : CameraActors)
	{
		if (!CameraActor || CameraActor == LastCameraActor)
		{
			continue;
		}
		LastCameraActor = CameraActor;
		if (URTSCamera* Camera =
			CameraActor->FindComponentByClass<URTSCamera>())
		{
			Camera->HandlePointerMoved(MousePosition);
			bForwardedToCameraFrameDriver =
				bForwardedToCameraFrameDriver ||
				CameraActor == PlayerController->GetViewTarget();
		}
	}

	// Drag-box visuals follow raw pointer events. Actor/Mass hover and build
	// queries are intentionally coalesced by the camera's 24 Hz frame driver;
	// otherwise a high-polling-rate mouse can run several heavy traces per frame.
	if (bIsSelecting)
	{
		UpdateSelectionAtScreenPosition(MousePosition);
	}
	else if (!bForwardedToCameraFrameDriver)
	{
		RefreshPointerWorldState(MousePosition);
	}
}

void URTSSelector::RefreshPointerWorldState(
	const FVector2D& ScreenPosition)
{
	if (bIsSelecting)
	{
		UpdateSelectionAtScreenPosition(ScreenPosition);
	}
	else if (bIsTargeting && bIsHashGridSelecting)
	{
		UpdateHashGridSelectionPreview();
	}
	else if (!bIsTargeting)
	{
		UpdateSelectableHoverPreview(ScreenPosition);
	}
}

void URTSSelector::UpdateSelectionAtScreenPosition(
	const FVector2D& ScreenPosition)
{
	if (bSkipCurrentSelectionClick || !bIsSelecting)
	{
		return;
	}
	SelectionEnd = ScreenPosition;
	if (IsValid(HUD))
	{
		HUD->UpdateSelection(SelectionEnd);
	}
}

void URTSSelector::CollectComponentDependencyReferences()
{
	if (const auto PlayerControllerRef = UGameplayStatics::GetPlayerController(this->GetWorld(), 0))
	{
		this->PlayerController = PlayerControllerRef;
		this->HUD = Cast<ARTSHUD>(PlayerControllerRef->GetHUD());
		if (this->HUD)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RTSSelector] HUD found OK: %s"), *this->HUD->GetClass()->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RTSSelector] ARTSHUD is unavailable (current HUD: %s). Selection remains enabled without marquee rendering."),
				PlayerControllerRef->GetHUD() ? *PlayerControllerRef->GetHUD()->GetClass()->GetName() : TEXT("NULL"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("USelector is not attached to a PlayerController."));
	}
}

void URTSSelector::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (const auto InputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		InputComponent->BindAction(this->BeginSelection, ETriggerEvent::Started, this, &URTSSelector::OnSelectionStart);
		InputComponent->BindAction(this->BeginSelection, ETriggerEvent::Completed, this, &URTSSelector::OnSelectionEnd);
		InputComponent->BindAction(this->BeginSelection, ETriggerEvent::Canceled, this, &URTSSelector::OnSelectionEnd);
		if (this->IssueCommandAction)
		{
			InputComponent->BindAction(this->IssueCommandAction, ETriggerEvent::Started, this, &URTSSelector::OnIssueCommand);
		}
	}
}

void URTSSelector::BindInputActions()
{
	if (!PlayerController || !BeginSelection)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RTSSelector] Selection input was not bound because the player controller or input action is unavailable."));
		return;
	}

	if (const auto EnhancedInputComponent = Cast<UEnhancedInputComponent>(this->PlayerController->InputComponent))
	{
		EnhancedInputComponent->BindAction(
			this->BeginSelection,
			ETriggerEvent::Started,
			this,
			&URTSSelector::OnSelectionStart
		);

		EnhancedInputComponent->BindAction(
			this->BeginSelection,
			ETriggerEvent::Completed,
			this,
			&URTSSelector::OnSelectionEnd
		);

		EnhancedInputComponent->BindAction(
			this->BeginSelection,
			ETriggerEvent::Canceled,
			this,
			&URTSSelector::OnSelectionEnd
		);

		if (!this->IssueCommandAction)
		{
			// Dynamically load it in case the CDO failed to find it during editor startup
			this->IssueCommandAction = Cast<UInputAction>(StaticLoadObject(UInputAction::StaticClass(), nullptr, TEXT("/Script/EnhancedInput.InputAction'/RTSInputSystem/Inputs/IssueCommand.IssueCommand'")));
		}

		if (this->IssueCommandAction)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RTSSelector] IssueCommandAction BOUND SUCCESSFULLY!"));
			EnhancedInputComponent->BindAction(
				this->IssueCommandAction,
				ETriggerEvent::Started,
				this,
				&URTSSelector::OnIssueCommand
			);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[RTSSelector] IssueCommandAction is NULL! Ensure it exists at /RTSInputSystem/Inputs/IssueCommand"));
		}
	}
}

void URTSSelector::RegisterControlGroupHotkeys()
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	if (!PlayerController || !PlayerController->InputComponent || (Settings && !Settings->bEnableControlGroupHotkeys))
	{
		return;
	}

	UnregisterControlGroupHotkeys();
	UWorld* InputWorld = GetWorld();
	if (!InputWorld)
	{
		return;
	}

	ControlGroupInputComponent = NewObject<UInputComponent>(PlayerController);
	if (!ControlGroupInputComponent)
	{
		return;
	}

	ControlGroupInputComponent->Priority = 10;
	ControlGroupInputComponent->bBlockInput = false;
	ControlGroupInputComponent->RegisterComponentWithWorld(InputWorld);
	PlayerController->PushInputComponent(ControlGroupInputComponent);
	ControlGroupInputOwner = PlayerController;

	const FKey NumberKeys[] =
	{
		EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
		EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine
	};
	const FKey NumpadKeys[] =
	{
		EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour,
		EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine
	};

	auto AddBinding = [this](const FKey& Key, int32 GroupIndex, bool bShift, bool bControl, bool bAlt)
	{
		FInputKeyBinding Binding(FInputChord(Key, bShift, bControl, bAlt, false), IE_Pressed);
		Binding.bConsumeInput = true;
		Binding.KeyDelegate.GetDelegateForManualSet().BindLambda([WeakThis = TWeakObjectPtr<URTSSelector>(this), GroupIndex]()
		{
			if (URTSSelector* Selector = WeakThis.Get())
			{
				Selector->HandleControlGroupHotkey(GroupIndex);
			}
		});
		ControlGroupInputComponent->KeyBindings.Add(MoveTemp(Binding));
	};

	for (int32 GroupIndex = 0; GroupIndex <= 9; ++GroupIndex)
	{
		for (const FKey Key : { NumberKeys[GroupIndex], NumpadKeys[GroupIndex] })
		{
			AddBinding(Key, GroupIndex, false, false, false);
			AddBinding(Key, GroupIndex, false, true, false);
			AddBinding(Key, GroupIndex, true, false, false);
			AddBinding(Key, GroupIndex, false, false, true);
		}
	}
}

void URTSSelector::UnregisterControlGroupHotkeys()
{
	if (!ControlGroupInputComponent)
	{
		return;
	}

	if (ControlGroupInputOwner.IsValid())
	{
		ControlGroupInputOwner->PopInputComponent(ControlGroupInputComponent);
	}
	ControlGroupInputComponent->DestroyComponent();
	ControlGroupInputComponent = nullptr;
	ControlGroupInputOwner.Reset();
}

void URTSSelector::HandleControlGroupHotkey(int32 GroupIndex)
{
	if (!PlayerController || !PlayerController->GetLocalPlayer())
	{
		return;
	}

	URTSSelectionSubsystem* Selection = PlayerController->GetLocalPlayer()->GetSubsystem<URTSSelectionSubsystem>();
	if (!Selection)
	{
		return;
	}

	const bool bControlDown = PlayerController->IsInputKeyDown(EKeys::LeftControl) || PlayerController->IsInputKeyDown(EKeys::RightControl);
	const bool bShiftDown = PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift);
	const bool bAltDown = PlayerController->IsInputKeyDown(EKeys::LeftAlt) || PlayerController->IsInputKeyDown(EKeys::RightAlt);

	if (bAltDown)
	{
		Selection->AssignCurrentSelectionToControlGroup(GroupIndex, ERTSControlGroupAssignmentMode::StealAndReplace);
		LastRecalledControlGroupIndex = INDEX_NONE;
		return;
	}
	if (bControlDown)
	{
		Selection->AssignCurrentSelectionToControlGroup(GroupIndex, ERTSControlGroupAssignmentMode::Replace);
		LastRecalledControlGroupIndex = INDEX_NONE;
		return;
	}
	if (bShiftDown)
	{
		Selection->AssignCurrentSelectionToControlGroup(GroupIndex, ERTSControlGroupAssignmentMode::ToggleMembership);
		LastRecalledControlGroupIndex = INDEX_NONE;
		return;
	}

	if (!Selection->RecallControlGroup(GroupIndex))
	{
		return;
	}

	const double CurrentTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const double DoubleTapTime = Settings ? FMath::Max(0.1f, Settings->ControlGroupDoubleTapTime) : 0.3;
	if (LastRecalledControlGroupIndex == GroupIndex && CurrentTime - LastControlGroupRecallTime <= DoubleTapTime)
	{
		Selection->RequestControlGroupFocus(GroupIndex);
	}
	LastRecalledControlGroupIndex = GroupIndex;
	LastControlGroupRecallTime = CurrentTime;
}

void URTSSelector::BindInputMappingContext()
{
	if (PlayerController && PlayerController->GetLocalPlayer())
	{
		if (const auto Input = PlayerController->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			PlayerController->bShowMouseCursor = true;

			// Keep camera, UI, and project-level contexts that may already be active.
			if (!Input->HasMappingContext(this->InputMappingContext))
			{
				Input->AddMappingContext(this->InputMappingContext, 0);
			}
		}
	}
}

void URTSSelector::InstallStrategyMouseCursors()
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	ULocalPlayer* LocalPlayer = PlayerController
		? PlayerController->GetLocalPlayer()
		: nullptr;
	UGameViewportClient* ViewportClient = LocalPlayer
		? LocalPlayer->ViewportClient
		: nullptr;
	if (!Settings
		|| !Settings->bEnableStrategyMouseCursor
		|| !ViewportClient)
	{
		return;
	}

	// Platform cursors remain responsive even when the render/game frame stalls.
	ViewportClient->SetUseSoftwareCursorWidgets(false);

	PlayerController->DefaultMouseCursor = EMouseCursor::Default;
	PlayerController->CurrentMouseCursor = EMouseCursor::Default;
}

void URTSSelector::RestoreStrategyMouseCursors()
{
	ULocalPlayer* LocalPlayer = PlayerController
		? PlayerController->GetLocalPlayer()
		: nullptr;
	if (UGameViewportClient* ViewportClient =
		LocalPlayer ? LocalPlayer->ViewportClient : nullptr)
	{
		ViewportClient->SetUseSoftwareCursorWidgets(false);
		ViewportClient->SetSoftwareCursorWidget(
			EMouseCursor::Default,
			TSharedPtr<SWidget>());
		ViewportClient->SetSoftwareCursorWidget(
			EMouseCursor::GrabHand,
			TSharedPtr<SWidget>());
		ViewportClient->SetSoftwareCursorWidget(
			EMouseCursor::Crosshairs,
			TSharedPtr<SWidget>());
		ViewportClient->RebuildCursors();
	}
}

void URTSSelector::ClearSelectableHoverPreview(
	const bool bRestoreDefaultCursor)
{
	if (bHoverVisualApplied)
	{
		if (URTSSelectable* Selectable = HoveredActorSelectable.Get())
		{
			Selectable->OnHoverPreviewEnded();
		}
		else if (HoveredMassEntity.IsSet())
		{
			TArray<FEntityHandle> HoveredEntities;
			HoveredEntities.Add(HoveredMassEntity);
			UMassBattleFuncLib::DeselectAgents(
				this,
				HoveredEntities,
				ESelectState::BeingSelect);
		}
	}

	HoveredActorSelectable.Reset();
	HoveredMassEntity.Reset();
	bCursorOverSelectable = false;
	bHoverVisualApplied = false;
	if (bRestoreDefaultCursor && PlayerController && !bIsTargeting)
	{
		PlayerController->CurrentMouseCursor = EMouseCursor::Default;
	}
}

void URTSSelector::UpdateSelectableHoverPreview(
	const FVector2D& ScreenPosition)
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	if (!Settings
		|| (!Settings->bEnableSelectableHoverPreview
			&& !Settings->bEnableStrategyMouseCursor)
		|| !PlayerController
		|| bIsTargeting
		|| bIsSelecting)
	{
		ClearSelectableHoverPreview(!bIsTargeting);
		return;
	}

	AActor* HoveredActor = nullptr;
	FEntityHandle HoveredEntity;
	FVector HoveredWorldLocation = FVector::ZeroVector;
	const bool bHasSelectable =
		ARTSHUD::ResolveSingleSelectableAtScreenPosition(
			PlayerController,
			ScreenPosition,
			HoveredActor,
			HoveredEntity,
			HoveredWorldLocation);
	(void)HoveredWorldLocation;
	URTSSelectable* HoveredSelectable = HoveredActor
		? HoveredActor->FindComponentByClass<URTSSelectable>()
		: nullptr;

	const bool bSameActor =
		HoveredActorSelectable.Get() == HoveredSelectable;
	const bool bSameMass =
		HoveredMassEntity == HoveredEntity;
	if (!bHasSelectable || !bSameActor || !bSameMass)
	{
		ClearSelectableHoverPreview(false);
	}

	if (bHasSelectable
		&& !HoveredActorSelectable.IsValid()
		&& !HoveredMassEntity.IsSet())
	{
		HoveredActorSelectable = HoveredSelectable;
		HoveredMassEntity = HoveredEntity;
	}
	if (bHasSelectable
		&& Settings->bEnableSelectableHoverPreview
		&& !bHoverVisualApplied)
	{
		if (HoveredSelectable)
		{
			HoveredSelectable->OnHoverPreviewStarted(
				Settings->HoverOutlineStencilValue);
			bHoverVisualApplied = true;
		}
		else if (HoveredEntity.IsSet())
		{
			TArray<FEntityHandle> HoveredEntities;
			HoveredEntities.Add(HoveredEntity);
			UMassBattleFuncLib::SelectAgents(
				this,
				HoveredEntities,
				ESelectState::BeingSelect);
			bHoverVisualApplied = true;
		}
	}

	bCursorOverSelectable = bHasSelectable;
	PlayerController->CurrentMouseCursor = bHasSelectable
		? EMouseCursor::GrabHand
		: EMouseCursor::Default;
}

void URTSSelector::BeginTargeting(FGameplayTag CommandTag)
{
	ERTSCommandTargetType TargetType = ERTSCommandTargetType::Location;
	const FName TagName = CommandTag.GetTagName();
	if (TagName == FName(TEXT("RTS.Command.Attack"))
		|| TagName == FName(TEXT("RTS.Command.City.Transfer")))
	{
		TargetType = ERTSCommandTargetType::LocationOrTarget;
	}
	BeginTargetingWithType(CommandTag, TargetType);
}

void URTSSelector::BeginTargetingWithType(FGameplayTag CommandTag, ERTSCommandTargetType TargetType)
{
	ClearSelectableHoverPreview(false);
	if (ShouldUseHashGridSelectionForCommand(CommandTag))
	{
		BeginHashGridSelection(CommandTag);
		return;
	}

	EndHashGridSelectionPreview();
	bIsTargeting = true;
	PendingCommandTag = CommandTag;
	PendingTargetType = TargetType;
	// Optional: Change mouse cursor to crosshair here
	if (PlayerController)
	{
		PlayerController->CurrentMouseCursor = EMouseCursor::Crosshairs;
	}
}

void URTSSelector::CancelTargeting()
{
	if (bIsHashGridSelecting)
	{
		CancelHashGridSelection();
		return;
	}

	EndHashGridSelectionPreview();
	bIsTargeting = false;
	PendingCommandTag = FGameplayTag::EmptyTag;
	PendingTargetType = ERTSCommandTargetType::Location;
	if (PlayerController)
	{
		PlayerController->CurrentMouseCursor = EMouseCursor::Default;
	}
}

bool URTSSelector::IsCommandQueueModifierDown() const
{
	return PlayerController
		&& (PlayerController->IsInputKeyDown(EKeys::LeftShift)
			|| PlayerController->IsInputKeyDown(EKeys::RightShift));
}

bool URTSSelector::CommitPendingTargetingAtCursor()
{
	if (!bIsTargeting || !PlayerController)
	{
		return false;
	}

	if (bIsHashGridSelecting)
	{
		CommitHashGridSelection();
		return true;
	}

	FHitResult Hit;
	PlayerController->GetHitResultUnderCursor(ECC_Visibility, false, Hit);
	if (!IssuePendingTargetingCommand(Hit))
	{
		return false;
	}

	if (!IsCommandQueueModifierDown())
	{
		CancelTargeting();
	}
	return true;
}

bool URTSSelector::CommitPendingTargetingAtWorldLocation(FVector WorldLocation)
{
	if (!bIsTargeting
		|| !PlayerController
		|| bIsHashGridSelecting
		|| WorldLocation.ContainsNaN()
		|| (PendingTargetType != ERTSCommandTargetType::Location
			&& PendingTargetType != ERTSCommandTargetType::LocationOrTarget))
	{
		return false;
	}

	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	URTSSelectionSubsystem* SelectionSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
		: nullptr;
	if (!SelectionSubsystem)
	{
		return false;
	}

	const bool bQueueCommand = IsCommandQueueModifierDown();
	SelectionSubsystem->IssueCommandWithLocation(PendingCommandTag, WorldLocation, bQueueCommand);
	if (!bQueueCommand)
	{
		CancelTargeting();
	}
	return true;
}

bool URTSSelector::ResolveSmartCommandHit(FHitResult& OutHit, bool& bOutHostileUnitTarget) const
{
	OutHit = FHitResult();
	bOutHostileUnitTarget = false;
	if (!PlayerController || !GetWorld())
	{
		return false;
	}

	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
	if (!PlayerController->DeprojectMousePositionToWorld(RayOrigin, RayDirection))
	{
		return false;
	}

	constexpr float SmartCommandTraceDistance = 1000000.0f;
	const FVector RayEnd = RayOrigin + RayDirection * SmartCommandTraceDistance;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RTSSmartCommand), true);
	QueryParams.bReturnPhysicalMaterial = false;

	URTSSelectionSubsystem* Selection = nullptr;
	if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		Selection = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>();
	}

	const int32 PlayerTeam = Selection
		? Selection->GetPlayerTeamIndex()
		: INDEX_NONE;
	for (int32 Pass = 0; Pass < 32; ++Pass)
	{
		FHitResult Candidate;
		if (!GetWorld()->LineTraceSingleByChannel(
			Candidate,
			RayOrigin,
			RayEnd,
			ECC_Visibility,
			QueryParams))
		{
			break;
		}

		AActor* HitActor = Candidate.GetActor();
		if (!HitActor)
		{
			OutHit = Candidate;
			return true;
		}

		int32 HitTeam = INDEX_NONE;
		bool bUnitHit = false;
		if (const UMassBattleAgentComponent* MassAgent =
			HitActor->FindComponentByClass<UMassBattleAgentComponent>())
		{
			bUnitHit = true;
			HitTeam = MassAgent->TeamIndex;
		}
		else if (const URTSSelectable* Selectable =
			HitActor->FindComponentByClass<URTSSelectable>())
		{
			bUnitHit = true;
			HitTeam = Selectable->TeamIndex;
		}

		if (bUnitHit
			&& PlayerTeam != INDEX_NONE
			&& HitTeam != INDEX_NONE
			&& HitTeam != PlayerTeam)
		{
			OutHit = Candidate;
			bOutHostileUnitTarget = true;
			return true;
		}

		if (bUnitHit || (Selection && Selection->IsActorSelected(HitActor)))
		{
			// Friendly/selected units are transparent to a context command. Re-run
			// the cursor ray without this actor so the terrain or a hostile behind it
			// can still receive the order.
			QueryParams.AddIgnoredActor(HitActor);
			continue;
		}

		OutHit = Candidate;
		return true;
	}

	// Empty sky still has a meaningful strategic-map destination. This keeps a
	// context command composable even when no collision primitive occupies the cell.
	if (!FMath::IsNearlyZero(RayDirection.Z))
	{
		const float DistanceToPlane = -RayOrigin.Z / RayDirection.Z;
		if (DistanceToPlane > 0.0f && DistanceToPlane <= SmartCommandTraceDistance)
		{
			OutHit.bBlockingHit = true;
			OutHit.Location = RayOrigin + RayDirection * DistanceToPlane;
			OutHit.ImpactPoint = OutHit.Location;
			OutHit.ImpactNormal = FVector::UpVector;
			return true;
		}
	}

	return false;
}

void URTSSelector::OnIssueCommand(const FInputActionValue& Value)
{
	if (!PlayerController) return;

	FHitResult Hit;
	PlayerController->GetHitResultUnderCursor(ECC_Visibility, false, Hit);

	UE_LOG(LogTemp, Warning, TEXT("[RTSSelector] OnIssueCommand TRIGGERED! bBlockingHit: %d, Location: %s"), Hit.bBlockingHit, *Hit.Location.ToString());

	if (bIsTargeting)
	{
		if (bIsHashGridSelecting)
		{
			CancelHashGridSelection();
			return;
		}

		if (IssuePendingTargetingCommand(Hit) && !IsCommandQueueModifierDown())
		{
			CancelTargeting();
		}
		return;
	}

	bool bHostileUnitTarget = false;
	FHitResult CommandHit;
	if (ResolveSmartCommandHit(CommandHit, bHostileUnitTarget))
	{
		if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* SelectionSubsystem = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>())
			{
				const bool bQueueCommand = PlayerController->IsInputKeyDown(EKeys::LeftShift)
					|| PlayerController->IsInputKeyDown(EKeys::RightShift);
				AActor* HitActor = CommandHit.GetActor();
				const FGameplayTag AttackTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Attack"), false);
				if (bHostileUnitTarget && HitActor)
				{
					SelectionSubsystem->IssueCommandWithTarget(AttackTag, HitActor);
				}
				else
				{
					const FGameplayTag MoveTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Move"), false);
					SelectionSubsystem->IssueCommandWithLocation(MoveTag, CommandHit.Location, bQueueCommand);
				}
			}
		}
	}

	CancelTargeting(); // Right click cancels any active targeting
}

void URTSSelector::OnSelectionStart(const FInputActionValue& Value)
{
	bIsSelecting = false;
	ClearSelectableHoverPreview(false);

	if (bIsTargeting)
	{
		bSkipCurrentSelectionClick = true;
		CommitPendingTargetingAtCursor();
		return;
	}

	bSkipCurrentSelectionClick = false;
	if (!PlayerController)
	{
		return;
	}

	FVector2D MousePosition = FVector2D::ZeroVector;
	if (!PlayerController->GetMousePosition(MousePosition.X, MousePosition.Y))
	{
		return;
	}

	SelectionStart = MousePosition;
	SelectionEnd = MousePosition;
	bIsSelecting = true;
	HUD = Cast<ARTSHUD>(PlayerController->GetHUD());
	if (IsValid(HUD))
	{
		HUD->BeginSelection(MousePosition);
	}
}

bool URTSSelector::IssuePendingTargetingCommand(const FHitResult& Hit)
{
	if (!PlayerController)
	{
		return false;
	}

	FHitResult ResolvedHit = Hit;
	bool bHostileUnitTarget = false;
	const bool bComposableCommand = IsSelectorComposableContextCommand(PendingCommandTag)
		&& PendingTargetType != ERTSCommandTargetType::TargetActor;
	if (bComposableCommand)
	{
		if (!ResolveSmartCommandHit(ResolvedHit, bHostileUnitTarget))
		{
			return false;
		}
	}
	else if (!ResolvedHit.bBlockingHit)
	{
		return false;
	}

	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	URTSSelectionSubsystem* SelectionSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
		: nullptr;
	if (!SelectionSubsystem)
	{
		return false;
	}

	AActor* HitActor = ResolvedHit.GetActor();
	const bool bHasMassTarget = HitActor && HitActor->FindComponentByClass<UMassBattleAgentComponent>();
	if (PendingTargetType == ERTSCommandTargetType::TargetActor)
	{
		if (!HitActor)
		{
			return false;
		}
		SelectionSubsystem->IssueCommandWithTarget(PendingCommandTag, HitActor);
		return true;
	}

	if (PendingTargetType == ERTSCommandTargetType::LocationOrTarget
		&& (bHostileUnitTarget || (!bComposableCommand && bHasMassTarget)))
	{
		SelectionSubsystem->IssueCommandWithTarget(PendingCommandTag, HitActor);
		return true;
	}

	if (PendingTargetType == ERTSCommandTargetType::Instant)
	{
		SelectionSubsystem->IssueCommand(PendingCommandTag);
		return true;
	}

	const bool bQueueCommand = IsCommandQueueModifierDown();
	SelectionSubsystem->IssueCommandWithLocation(PendingCommandTag, ResolvedHit.Location, bQueueCommand);
	return true;
}

void URTSSelector::OnUpdateSelection(const FInputActionValue& Value)
{
	(void)Value;
	if (bSkipCurrentSelectionClick || !bIsSelecting || !PlayerController) return;
	FVector2D MousePosition = SelectionEnd;
	if (!PlayerController->GetMousePosition(MousePosition.X, MousePosition.Y))
	{
		return;
	}
	UpdateSelectionAtScreenPosition(MousePosition);
}

void URTSSelector::OnSelectionEnd(const FInputActionValue& Value)
{
	if (bSkipCurrentSelectionClick)
	{
		bIsSelecting = false;
		return;
	}
	if (!bIsSelecting || !PlayerController)
	{
		return;
	}

	bIsSelecting = false;
	FVector2D MousePosition = SelectionEnd;
	if (PlayerController->GetMousePosition(MousePosition.X, MousePosition.Y))
	{
		SelectionEnd = MousePosition;
	}

	if (IsValid(HUD))
	{
		HUD->UpdateSelection(SelectionEnd);
		HUD->EndSelection();
	}
	else
	{
		ARTSHUD::PerformScreenSelection(PlayerController, this, SelectionStart, SelectionEnd);
	}

}

bool URTSSelector::CanSelectActor_Implementation(AActor *Actor) const {
	return true;
}

UMaterialInterface* URTSSelector::ResolveCommandFeedbackMaterial() const
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	UMaterialInterface* Material = Settings ? Settings->CommandFeedbackDecalMaterial.LoadSynchronous() : nullptr;
	if (!Material && Settings)
	{
		Material = Settings->HashGridSelectionDecalMaterial.LoadSynchronous();
	}
	return Material;
}

void URTSSelector::ShowGroundCommandFeedback(
	const FVector& Location,
	const bool bAttackGround)
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	UWorld* World = GetWorld();
	if (!Settings || !Settings->bEnableMoveCommandFeedback || !World)
	{
		return;
	}

	if (CVarRTSGroundCommandPulse.GetValueOnGameThread() != 0)
	{
		ULineBatchComponent* LineBatcher =
			World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent);
		if (!LineBatcher)
		{
			return;
		}
		const int32 MaxPulses = FMath::Clamp(
			Settings->MaxMoveCommandFeedbackPulses,
			1,
			32);
		while (MoveCommandFeedbackBatchIds.Num() >= MaxPulses)
		{
			LineBatcher->ClearBatch(MoveCommandFeedbackBatchIds[0]);
			MoveCommandFeedbackBatchIds.RemoveAt(
				0, 1, EAllowShrinking::No);
		}

		const uint32 BatchId = GetMoveCommandFeedbackLineBatchId(
			this,
			++MoveCommandFeedbackSequence);
		MoveCommandFeedbackBatchIds.Add(BatchId);
		const TArray<FVector> GroundRingPoints = BuildGroundConformingRing(
			Location,
			FMath::Max(1.0f, Settings->MoveArrivalRange),
			FMath::Clamp(Settings->MoveCommandFeedbackSegments, 12, 64));
		FLinearColor Color = bAttackGround
			? Settings->AttackGroundFeedbackColor
			: Settings->MoveCommandFeedbackColor;
		const float Duration = FMath::Max(
			0.1f,
			Settings->MoveCommandFeedbackDuration);
		const float PulseThickness = FMath::Clamp(
			Settings->MoveCommandFeedbackLineThickness,
			0.5f,
			12.0f);
		TArray<FBatchedLine> Lines;
		Lines.Reserve(GroundRingPoints.Num() + 2);
		for (int32 SegmentIndex = 1;
			SegmentIndex < GroundRingPoints.Num();
			++SegmentIndex)
		{
			Lines.Emplace(
				GroundRingPoints[SegmentIndex - 1],
				GroundRingPoints[SegmentIndex],
				Color,
				Duration,
				PulseThickness,
				1,
				BatchId);
		}

		if (bAttackGround)
		{
			const float CrossRadius =
				FMath::Max(1.0f, Settings->MoveArrivalRange) * 0.55f;
			const FVector Center =
				Location + FVector(0.0f, 0.0f, 6.0f);
			const FVector CrossOffsetA(CrossRadius, CrossRadius, 0.0f);
			const FVector CrossOffsetB(CrossRadius, -CrossRadius, 0.0f);
			Lines.Emplace(
				Center - CrossOffsetA,
				Center + CrossOffsetA,
				Color,
				Duration,
				PulseThickness,
				1,
				BatchId);
			Lines.Emplace(
				Center - CrossOffsetB,
				Center + CrossOffsetB,
				Color,
				Duration,
				PulseThickness,
				1,
				BatchId);
		}
		if (!Lines.IsEmpty())
		{
			LineBatcher->DrawLines(Lines);
		}
	}
}

void URTSSelector::ClearMoveCommandFeedback()
{
	if (UWorld* World = GetWorld())
	{
		if (ULineBatchComponent* LineBatcher =
			World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
		{
			for (const uint32 BatchId : MoveCommandFeedbackBatchIds)
			{
				LineBatcher->ClearBatch(BatchId);
			}
		}
	}
	MoveCommandFeedbackBatchIds.Reset();
}

TArray<FVector> URTSSelector::BuildGroundConformingRing(
	const FVector& Center,
	const float Radius,
	const int32 Segments) const
{
	TArray<FVector> Points;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Points;
	}

	const int32 SafeSegments = FMath::Clamp(Segments, 12, 64);
	const float SafeRadius = FMath::Max(1.0f, Radius);
	Points.Reserve(SafeSegments + 1);
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(RTSArrivalRangeGround),
		false);
	if (AActor* Owner = GetOwner())
	{
		QueryParams.AddIgnoredActor(Owner);
	}

	constexpr float TraceHalfHeight = 4096.0f;
	for (int32 SegmentIndex = 0;
		SegmentIndex <= SafeSegments;
		++SegmentIndex)
	{
		const float Angle =
			2.0f * PI * static_cast<float>(SegmentIndex) / SafeSegments;
		const FVector Candidate = Center + FVector(
			FMath::Cos(Angle) * SafeRadius,
			FMath::Sin(Angle) * SafeRadius,
			0.0f);
		FHitResult GroundHit;
		if (World->LineTraceSingleByChannel(
			GroundHit,
			Candidate + FVector(0.0f, 0.0f, TraceHalfHeight),
			Candidate - FVector(0.0f, 0.0f, TraceHalfHeight),
			ECC_Visibility,
			QueryParams))
		{
			Points.Add(GroundHit.ImpactPoint + GroundHit.ImpactNormal * 6.0f);
		}
		else
		{
			Points.Add(Candidate + FVector(0.0f, 0.0f, 6.0f));
		}
	}
	return Points;
}

bool URTSSelector::CacheSelectedTaskRouteAnchor(const FVector2D& ScreenPosition)
{
	if (!PlayerController)
	{
		bHasSelectedTaskRouteAnchor = false;
		return false;
	}

	FHitResult Hit;
	if (PlayerController->GetHitResultAtScreenPosition(
		ScreenPosition,
		ECC_Visibility,
		false,
		Hit)
		&& Hit.bBlockingHit)
	{
		SelectedTaskRouteAnchor = Hit.Location;
		bHasSelectedTaskRouteAnchor = true;
		return true;
	}

	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ZeroVector;
	if (PlayerController->DeprojectScreenPositionToWorld(
		ScreenPosition.X,
		ScreenPosition.Y,
		RayOrigin,
		RayDirection)
		&& !FMath::IsNearlyZero(RayDirection.Z))
	{
		const float DistanceToGround = -RayOrigin.Z / RayDirection.Z;
		if (DistanceToGround > 0.0f)
		{
			SelectedTaskRouteAnchor =
				RayOrigin + RayDirection * DistanceToGround;
			bHasSelectedTaskRouteAnchor = true;
			return true;
		}
	}

	bHasSelectedTaskRouteAnchor = false;
	return false;
}

void URTSSelector::HandleCommandFeedbackIssued(
	const FGameplayTag CommandTag,
	const FVector WorldLocation,
	const bool bHasWorldLocation,
	const bool bQueue)
{
	const FName TagName = CommandTag.GetTagName();
	const bool bClearsTaskRoute =
		TagName == FName(TEXT("RTS.Command.Stop"))
		|| TagName == FName(TEXT("RTS.Command.Hold"));
	if (bClearsTaskRoute)
	{
		SelectedTaskRoutes.Reset();
		RedrawSelectedTaskRoutes();
		return;
	}

	const bool bMovePath = IsMovePathCommand(CommandTag);
	const bool bAttackPath = IsAttackPathCommand(CommandTag);
	if (!bHasWorldLocation || (!bMovePath && !bAttackPath))
	{
		return;
	}

	ShowGroundCommandFeedback(WorldLocation, bAttackPath);

	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	if (!Settings
		|| !Settings->bEnableSelectedPathPreview
		|| CVarRTSSelectedTaskLineFeedback.GetValueOnGameThread() == 0)
	{
		SelectedTaskRoutes.Reset();
		RedrawSelectedTaskRoutes();
		return;
	}

	const FVector RouteStart =
		bQueue && !SelectedTaskRoutes.IsEmpty()
			? SelectedTaskRoutes.Last().End
			: WorldLocation;
	if (!bQueue)
	{
		SelectedTaskRoutes.Reset();
	}

	FSelectedTaskRoute& Route =
		SelectedTaskRoutes.AddDefaulted_GetRef();
	Route.Start = RouteStart;
	Route.End = WorldLocation;
	Route.GroundRingPoints = BuildGroundConformingRing(
		WorldLocation,
		FMath::Max(1.0f, Settings->MoveArrivalRange),
		FMath::Clamp(Settings->MoveCommandFeedbackSegments, 12, 64));
	Route.bAttackMove = bAttackPath;

	const int32 MaxLines = FMath::Clamp(
		Settings->MaxSelectedTaskRouteLines,
		1,
		32);
	if (SelectedTaskRoutes.Num() > MaxLines)
	{
		SelectedTaskRoutes.RemoveAt(
			0,
			SelectedTaskRoutes.Num() - MaxLines,
			EAllowShrinking::No);
	}

	RedrawSelectedTaskRoutes();
}

void URTSSelector::RedrawSelectedTaskRoutes()
{
	SCOPE_CYCLE_COUNTER(STAT_RTSSelectedTaskLineFeedback);

	UWorld* World = GetWorld();
	ULineBatchComponent* LineBatcher = World
		? World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent)
		: nullptr;
	if (!LineBatcher)
	{
		bSelectedPathPreviewVisible = false;
		return;
	}

	const uint32 BatchId = GetSelectedPathLineBatchId(this);
	LineBatcher->ClearBatch(BatchId);

	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	if (!Settings
		|| !Settings->bEnableSelectedPathPreview
		|| CVarRTSSelectedTaskLineFeedback.GetValueOnGameThread() == 0
		|| SelectedTaskRoutes.IsEmpty())
	{
		bSelectedPathPreviewVisible = false;
		return;
	}

	const int32 MaxLines = FMath::Clamp(
		Settings->MaxSelectedTaskRouteLines,
		1,
		32);
	const int32 FirstRouteIndex =
		FMath::Max(0, SelectedTaskRoutes.Num() - MaxLines);
	const float Thickness = FMath::Clamp(
		Settings->SelectedPathLineThickness,
		0.1f,
		12.0f);

	TArray<FBatchedLine> Lines;
	Lines.Reserve(
		(SelectedTaskRoutes.Num() - FirstRouteIndex)
		* FMath::Clamp(Settings->MoveCommandFeedbackSegments, 12, 64));
	for (int32 RouteIndex = FirstRouteIndex;
		RouteIndex < SelectedTaskRoutes.Num();
		++RouteIndex)
	{
		const FSelectedTaskRoute& Route = SelectedTaskRoutes[RouteIndex];
		FLinearColor Color = Route.bAttackMove
			? Settings->SelectedAttackMovePathColor
			: Settings->SelectedMovePathColor;
		const float QueueAlpha =
			SelectedTaskRoutes.Num() <= 1
				? 1.0f
				: FMath::Lerp(
					0.52f,
					1.0f,
					static_cast<float>(RouteIndex - FirstRouteIndex + 1)
						/ (SelectedTaskRoutes.Num() - FirstRouteIndex));
		Color.A *= QueueAlpha;
		for (int32 PointIndex = 1;
			PointIndex < Route.GroundRingPoints.Num();
			++PointIndex)
		{
			Lines.Emplace(
				Route.GroundRingPoints[PointIndex - 1],
				Route.GroundRingPoints[PointIndex],
				Color,
				-1.0f,
				Thickness,
				1,
				BatchId);
		}
	}

	if (!Lines.IsEmpty())
	{
		LineBatcher->DrawLines(Lines);
	}
	bSelectedPathPreviewVisible = !Lines.IsEmpty();
}

void URTSSelector::ClearSelectedPathPreview()
{
	SelectedTaskRoutes.Reset();
	SelectedTaskRouteAnchor = FVector::ZeroVector;
	bHasSelectedTaskRouteAnchor = false;

	if (UWorld* World = GetWorld())
	{
		if (ULineBatchComponent* LineBatcher =
			World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
		{
			LineBatcher->ClearBatch(GetSelectedPathLineBatchId(this));
		}
	}

	bSelectedPathPreviewVisible = false;
}

void URTSSelector::BeginHashGridSelection(FGameplayTag CommandTag)
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const FVector2D FootprintCells = Settings ? Settings->HashGridSelectionFootprintCells : FVector2D(64.0f, 64.0f);
	const float CellSize = Settings ? Settings->HashGridCellSize : 16.0f;
	BeginHashGridSelectionInternal(CommandTag, FootprintCells, CellSize, nullptr);
}

void URTSSelector::BeginHashGridSelectionWithFootprint(FGameplayTag CommandTag, FVector2D FootprintCells, float CellSize)
{
	BeginHashGridSelectionInternal(CommandTag, FootprintCells, CellSize, nullptr);
}

void URTSSelector::BeginHashGridSelectionWithFootprintAndPreview(
	FGameplayTag CommandTag,
	FVector2D FootprintCells,
	float CellSize,
	UStaticMesh* PreviewMesh)
{
	BeginHashGridSelectionInternal(
		CommandTag,
		FootprintCells,
		CellSize,
		PreviewMesh);
}

AActor* URTSSelector::GetBuildPlacementPreviewActor() const
{
	return IsValid(BuildPlacementPreviewActor) ? BuildPlacementPreviewActor.Get() : nullptr;
}

AActor* URTSSelector::GetOrCreateBuildPlacementPreviewActor()
{
	if (AActor* Existing = GetBuildPlacementPreviewActor())
	{
		return Existing;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = PlayerController;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	BuildPlacementPreviewActor = World->SpawnActor<AActor>(
		AActor::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	if (AActor* PreviewActor = GetBuildPlacementPreviewActor())
	{
		// Controllers are hidden actors in UE, so rendering primitives directly on
		// the selector's owner makes an otherwise valid placement preview invisible.
		PreviewActor->SetActorHiddenInGame(false);
		PreviewActor->SetActorEnableCollision(false);
		PreviewActor->SetActorTickEnabled(false);
		PreviewActor->SetReplicates(false);
		return PreviewActor;
	}

	return nullptr;
}

void URTSSelector::DestroyBuildPlacementPreviewActor()
{
	HashGridSelectionDecalComponent = nullptr;
	if (AActor* PreviewActor = GetBuildPlacementPreviewActor())
	{
		if (!PreviewActor->IsActorBeingDestroyed())
		{
			PreviewActor->Destroy();
		}
	}
	BuildPlacementPreviewActor = nullptr;
}

void URTSSelector::CancelHashGridSelection()
{
	if (!bIsHashGridSelecting)
	{
		return;
	}

	const FGameplayTag CancelledCommandTag = PendingCommandTag;
	bIsHashGridSelecting = false;
	EndHashGridSelectionPreview();
	bIsTargeting = false;
	PendingCommandTag = FGameplayTag::EmptyTag;
	PendingTargetType = ERTSCommandTargetType::Location;
	ActiveHashGridFootprintCells = FVector2D::ZeroVector;
	ActiveHashGridCellSize = 0.0f;

	if (PlayerController)
	{
		PlayerController->CurrentMouseCursor = EMouseCursor::Default;
	}

	OnHashGridSelectionCancelled.Broadcast(CancelledCommandTag);
}

bool URTSSelector::ShouldUseHashGridSelectionForCommand(FGameplayTag CommandTag) const
{
	if (!CommandTag.IsValid())
	{
		return false;
	}
	const FName TagName = CommandTag.GetTagName();
	return TagName.ToString().StartsWith(TEXT("RTS.Command.Build."))
		|| TagName == FName(TEXT("RTS.Command.City.EstablishCapital"));
}

FVector URTSSelector::SnapHashGridSelectionLocation(const FVector& Location) const
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const float CellSize = ActiveHashGridCellSize > KINDA_SMALL_NUMBER
		? ActiveHashGridCellSize
		: (Settings ? Settings->HashGridCellSize : 16.0f);

	const bool bShouldSnap = !Settings || Settings->bSnapHashGridSelectionToGrid;
	if (!bShouldSnap || CellSize <= KINDA_SMALL_NUMBER)
	{
		return Location;
	}

	const FVector2D GridOrigin = Settings ? Settings->BuildGridWorldOrigin : FVector2D::ZeroVector;
	const FIntPoint GridResolution = Settings ? Settings->BuildGridMapResolution : FIntPoint(4096, 4096);
	const FVector2D GridMin(
		GridOrigin.X - static_cast<double>(FMath::Max(1, GridResolution.X)) * CellSize * 0.5,
		GridOrigin.Y - static_cast<double>(FMath::Max(1, GridResolution.Y)) * CellSize * 0.5);
	const int32 FootprintX = FMath::Max(1, FMath::RoundToInt(ActiveHashGridFootprintCells.X));
	const int32 FootprintY = FMath::Max(1, FMath::RoundToInt(ActiveHashGridFootprintCells.Y));

	auto SnapAxis = [CellSize](double Coordinate, double AxisMin, int32 FootprintCells)
	{
		const double RawStartCell = (Coordinate - AxisMin) / CellSize - FootprintCells * 0.5;
		const int32 StartCell = FMath::RoundToInt(RawStartCell);
		return AxisMin + (StartCell + FootprintCells * 0.5) * CellSize;
	};

	FVector Snapped = Location;
	Snapped.X = SnapAxis(Location.X, GridMin.X, FootprintX);
	Snapped.Y = SnapAxis(Location.Y, GridMin.Y, FootprintY);
	return Snapped;
}

FIntPoint URTSSelector::GetHashGridCellForLocation(const FVector& Location) const
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const float CellSize = ActiveHashGridCellSize > KINDA_SMALL_NUMBER
		? ActiveHashGridCellSize
		: (Settings ? Settings->HashGridCellSize : 16.0f);

	if (CellSize <= KINDA_SMALL_NUMBER)
	{
		return FIntPoint::ZeroValue;
	}

	const FVector2D GridOrigin = Settings ? Settings->BuildGridWorldOrigin : FVector2D::ZeroVector;
	const FIntPoint GridResolution = Settings ? Settings->BuildGridMapResolution : FIntPoint(4096, 4096);
	const FVector2D GridMin(
		GridOrigin.X - static_cast<double>(FMath::Max(1, GridResolution.X)) * CellSize * 0.5,
		GridOrigin.Y - static_cast<double>(FMath::Max(1, GridResolution.Y)) * CellSize * 0.5);
	const int32 FootprintX = FMath::Max(1, FMath::RoundToInt(ActiveHashGridFootprintCells.X));
	const int32 FootprintY = FMath::Max(1, FMath::RoundToInt(ActiveHashGridFootprintCells.Y));
	return FIntPoint(
		FMath::RoundToInt((Location.X - GridMin.X) / CellSize - FootprintX * 0.5),
		FMath::RoundToInt((Location.Y - GridMin.Y) / CellSize - FootprintY * 0.5));
}

bool URTSSelector::ProjectHashGridSelectionLocationToGround(
	const FVector& CandidateLocation,
	FVector& OutLocation,
	FVector& OutNormal,
	AActor*& OutGroundActor) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const float TraceHalfHeight = Settings ? Settings->HashGridSelectionTraceHalfHeight : 50000.0f;
	const FVector TraceStart(CandidateLocation.X, CandidateLocation.Y, CandidateLocation.Z + TraceHalfHeight);
	const FVector TraceEnd(CandidateLocation.X, CandidateLocation.Y, CandidateLocation.Z - TraceHalfHeight);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RTSHashGridSelectionTrace), true);
	if (PlayerController)
	{
		if (APawn* Pawn = PlayerController->GetPawn())
		{
			QueryParams.AddIgnoredActor(Pawn);
		}
	}

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		OutLocation = Hit.Location;
		OutNormal = Hit.ImpactNormal.GetSafeNormal();
		OutGroundActor = Hit.GetActor();
		return true;
	}

	OutLocation = CandidateLocation;
	OutNormal = FVector::UpVector;
	OutGroundActor = nullptr;
	return false;
}

bool URTSSelector::ValidateHashGridSelection(
	const FRTSHashGridSelectionResult& Result,
	AActor* GroundActor,
	FText& OutInvalidReason) const
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const FIntPoint Resolution = Settings ? Settings->BuildGridMapResolution : FIntPoint(4096, 4096);
	const int32 FootprintX = FMath::Max(1, FMath::RoundToInt(Result.FootprintCells.X));
	const int32 FootprintY = FMath::Max(1, FMath::RoundToInt(Result.FootprintCells.Y));
	if ((!Settings || Settings->bConstrainBuildingPlacementToMap)
		&& (Result.Cell.X < 0 || Result.Cell.Y < 0
			|| Result.Cell.X + FootprintX > FMath::Max(1, Resolution.X)
			|| Result.Cell.Y + FootprintY > FMath::Max(1, Resolution.Y)))
	{
		OutInvalidReason = FText::FromString(TEXT("建筑占地超出地图边界"));
		return false;
	}

	const float MaxSlope = Settings ? Settings->BuildPlacementMaxSlopeDegrees : 30.0f;
	const float MinimumNormalZ = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(MaxSlope, 0.0f, 89.0f)));
	if (Result.GroundNormal.Z < MinimumNormalZ)
	{
		OutInvalidReason = FText::FromString(TEXT("地面坡度过大"));
		return false;
	}

	if (!Settings || Settings->bValidateBuildPlacementCollision)
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			OutInvalidReason = FText::FromString(TEXT("无法访问当前世界"));
			return false;
		}

		const float HalfHeight = Settings ? FMath::Max(1.0f, Settings->BuildPlacementCollisionHalfHeight) : 300.0f;
		const FVector HalfExtent(
			FMath::Max(1.0f, FootprintX * Result.CellSize * 0.5f - 1.0f),
			FMath::Max(1.0f, FootprintY * Result.CellSize * 0.5f - 1.0f),
			HalfHeight);
		const FVector Center = Result.WorldLocation + FVector(0.0f, 0.0f, HalfHeight + 2.0f);

		FCollisionObjectQueryParams ObjectTypes;
		ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
		ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
		ObjectTypes.AddObjectTypesToQuery(ECC_PhysicsBody);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RTSBuildPlacementOverlap), false);
		if (GroundActor)
		{
			QueryParams.AddIgnoredActor(GroundActor);
		}
		if (PlayerController)
		{
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				QueryParams.AddIgnoredActor(Pawn);
			}
			if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
			{
				if (URTSSelectionSubsystem* Selection = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>())
				{
					QueryParams.AddIgnoredActors(Selection->GetSelectedActors());
				}
			}
		}

		TArray<FOverlapResult> Overlaps;
		if (World->OverlapMultiByObjectType(
			Overlaps,
			Center,
			FQuat::Identity,
			ObjectTypes,
			FCollisionShape::MakeBox(HalfExtent),
			QueryParams))
		{
			OutInvalidReason = FText::FromString(TEXT("建筑占地内存在阻挡物"));
			return false;
		}
	}

	bool bPassesGameRules = true;
	FText GameRuleFailure;
	URTSSelectionSubsystem::OnValidateBuildPlacement().Broadcast(
		PlayerController ? static_cast<UObject*>(PlayerController) : const_cast<URTSSelector*>(this),
		Result.CommandTag,
		Result.WorldLocation,
		Result.FootprintCells,
		bPassesGameRules,
		GameRuleFailure);
	if (!bPassesGameRules)
	{
		OutInvalidReason = GameRuleFailure.IsEmpty()
			? FText::FromString(TEXT("当前位置不满足建筑规则"))
			: GameRuleFailure;
		return false;
	}

	return true;
}

bool URTSSelector::GetHashGridSelectionResult(FRTSHashGridSelectionResult& OutResult) const
{
	if (!PlayerController)
	{
		return false;
	}

	FHitResult Hit;
	PlayerController->GetHitResultUnderCursor(ECC_Visibility, false, Hit);
	if (!Hit.bBlockingHit)
	{
		return false;
	}

	FVector WorldLocation = FVector::ZeroVector;
	FVector GroundNormal = FVector::UpVector;
	AActor* GroundActor = nullptr;
	if (!ProjectHashGridSelectionLocationToGround(
		SnapHashGridSelectionLocation(Hit.Location), WorldLocation, GroundNormal, GroundActor))
	{
		return false;
	}

	OutResult.CommandTag = PendingCommandTag;
	OutResult.WorldLocation = WorldLocation;
	OutResult.Cell = GetHashGridCellForLocation(WorldLocation);
	OutResult.CellSize = ActiveHashGridCellSize;
	OutResult.FootprintCells = ActiveHashGridFootprintCells;
	OutResult.GroundNormal = GroundNormal;
	OutResult.bIsValidPlacement = ValidateHashGridSelection(OutResult, GroundActor, OutResult.InvalidReason);
	return true;
}

void URTSSelector::BeginHashGridSelectionInternal(
	FGameplayTag CommandTag,
	FVector2D FootprintCells,
	float CellSize,
	UStaticMesh* PreviewMesh)
{
	ClearSelectableHoverPreview(false);
	EndHashGridSelectionPreview();

	bIsTargeting = true;
	bIsHashGridSelecting = true;
	PendingCommandTag = CommandTag;
	PendingTargetType = ERTSCommandTargetType::Location;
	ActiveHashGridFootprintCells = FVector2D(
		FMath::Max(1.0f, FootprintCells.X),
		FMath::Max(1.0f, FootprintCells.Y)
	);
	ActiveHashGridCellSize = FMath::Max(1.0f, CellSize);

	if (PlayerController)
	{
		PlayerController->CurrentMouseCursor = EMouseCursor::Crosshairs;
	}

	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const int32 GuidanceRadiusCells = FMath::Clamp(
		Settings ? Settings->BuildPlacementGuidanceRadiusCells : MaxBuildPlacementGuidanceRadiusCells,
		1,
		MaxBuildPlacementGuidanceRadiusCells);
	if (Settings && !Settings->bEnableHashGridSelectionPreview)
	{
		return;
	}
	AActor* PreviewActor = GetOrCreateBuildPlacementPreviewActor();
	if (!PreviewActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSSelector: Could not create the visible build-placement preview host."));
		return;
	}
	ConfigureBuildPlacementModel(PreviewActor, PreviewMesh);

	if (!HashGridSelectionDecalComponent)
	{
		HashGridSelectionDecalComponent = NewObject<UDecalComponent>(
			PreviewActor,
			TEXT("RTSHashGridSelectionDecal"),
			RF_Transient);
		if (HashGridSelectionDecalComponent)
		{
			PreviewActor->AddInstanceComponent(HashGridSelectionDecalComponent);
			HashGridSelectionDecalComponent->RegisterComponentWithWorld(GetWorld());
			HashGridSelectionDecalComponent->SetWorldRotation(FRotator(-90.0f, 0.0f, 0.0f));
			HashGridSelectionDecalComponent->SetVisibility(false);
			HashGridSelectionDecalComponent->FadeScreenSize = 0.0f;
		}
	}

	if (!HashGridSelectionDecalComponent)
	{
		return;
	}

	if (!HashGridSelectionDecalComponent->GetDecalMaterial())
	{
		UMaterialInterface* DecalMaterial = Settings ? Settings->HashGridSelectionDecalMaterial.LoadSynchronous() : nullptr;
		if (!DecalMaterial)
		{
			DecalMaterial = Cast<UMaterialInterface>(StaticLoadObject(
				UMaterialInterface::StaticClass(),
				nullptr,
				TEXT("/RTSInputSystem/Feedback/M_RTSBuildPlacementGrid.M_RTSBuildPlacementGrid")
			));
		}

		if (DecalMaterial)
		{
			HashGridSelectionDecalComponent->SetDecalMaterial(DecalMaterial);
			HashGridSelectionDecalComponent->CreateDynamicMaterialInstance();
		}
		else if (Settings && !Settings->HashGridSelectionDecalMaterial.IsNull())
		{
			UE_LOG(LogTemp, Warning, TEXT("RTSSelector: Could not load configured hash-grid selection decal material."));
		}
	}

	const float DecalDepth = Settings ? Settings->HashGridSelectionDecalDepth : 4096.0f;
	HashGridSelectionDecalComponent->DecalSize = FVector(
		FMath::Max(1.0f, DecalDepth),
		FMath::Max(1.0f, ActiveHashGridFootprintCells.X * ActiveHashGridCellSize),
		FMath::Max(1.0f, ActiveHashGridFootprintCells.Y * ActiveHashGridCellSize)
	);
	GetOrCreateBuildPlacementFootprint(PreviewActor);
	UE_LOG(LogTemp, Display, TEXT("RTSSelector: Visible build-placement preview started for %s (footprint %dx%d, guide radius %d cells at %.1f UU; footprint valid=%s invalid=%s; guide valid=%s invalid=%s)."),
		*CommandTag.ToString(),
		FMath::RoundToInt(ActiveHashGridFootprintCells.X),
		FMath::RoundToInt(ActiveHashGridFootprintCells.Y),
		GuidanceRadiusCells,
		ActiveHashGridCellSize,
		Settings ? *Settings->BuildPlacementValidColor.ToString() : TEXT("fallback-green"),
		Settings ? *Settings->BuildPlacementInvalidColor.ToString() : TEXT("fallback-red"),
		Settings ? *Settings->BuildPlacementGuidanceValidColor.ToString() : TEXT("fallback-blue"),
		Settings ? *Settings->BuildPlacementGuidanceInvalidColor.ToString() : TEXT("fallback-yellow"));

	UpdateHashGridSelectionPreview();
}

void URTSSelector::UpdateHashGridSelectionPreview()
{
	FRTSHashGridSelectionResult Result;
	if (GetHashGridSelectionResult(Result))
	{
		DrawHashGridSelectionPreview(Result);
		const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
		AActor* PreviewActor = GetOrCreateBuildPlacementPreviewActor();
		const bool bHasCellFootprint = UpdateBuildPlacementFootprint(
			PreviewActor,
			Result,
			Settings);
		UpdateBuildPlacementModel(
			GetBuildPlacementPreviewActor(),
			Result,
			Settings);
		UpdateBuildPlacementGuidance(Result);
		if (HashGridSelectionDecalComponent)
		{
			HashGridSelectionDecalComponent->SetWorldLocation(Result.WorldLocation + FVector(0.0f, 0.0f, 8.0f));
			HashGridSelectionDecalComponent->SetWorldRotation(FRotator(-90.0f, 0.0f, 0.0f));
			if (UMaterialInstanceDynamic* DecalMaterial =
				Cast<UMaterialInstanceDynamic>(HashGridSelectionDecalComponent->GetDecalMaterial()))
			{
				const FLinearColor Color = Result.bIsValidPlacement
					? (Settings
						? Settings->BuildPlacementValidColor
						: FLinearColor(0.05f, 0.80f, 0.10f, 0.55f))
					: (Settings
						? Settings->BuildPlacementInvalidColor
						: FLinearColor(1.0f, 0.05f, 0.03f, 0.55f));
				DecalMaterial->SetVectorParameterValue(
					BuildPlacementColorParameterName,
					Color);
				DecalMaterial->SetScalarParameterValue(
					BuildPlacementOpacityParameterName,
					Color.A);
			}
			// The projected decal is a fallback for a missing preview mesh/material.
			// Showing it below the per-cell layer would visually merge all occupied
			// cells back into the ambiguous single rectangle this preview replaces.
			HashGridSelectionDecalComponent->SetVisibility(
				!bHasCellFootprint
				&& HashGridSelectionDecalComponent->GetDecalMaterial() != nullptr);
		}
	}
	else
	{
		if (UWorld* World = GetWorld())
		{
			if (ULineBatchComponent* LineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
			{
				LineBatcher->ClearBatch(GetBuildPlacementLineBatchId(this));
			}
		}
		if (HashGridSelectionDecalComponent)
		{
			HashGridSelectionDecalComponent->SetVisibility(false);
		}
		HideBuildPlacementFootprint(GetBuildPlacementPreviewActor());
		HideBuildPlacementModel(GetBuildPlacementPreviewActor());
		ClearBuildPlacementGuidance(false);
	}
}

void URTSSelector::UpdateBuildPlacementGuidance(const FRTSHashGridSelectionResult& Result)
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	if (!Settings || !Settings->bEnableHashGridSelectionPreview
		|| !Settings->bEnableBuildPlacementGuidance)
	{
		ClearBuildPlacementGuidance(false);
		return;
	}
	AActor* PreviewActor = GetOrCreateBuildPlacementPreviewActor();
	if (!PreviewActor)
	{
		ClearBuildPlacementGuidance(false);
		return;
	}

	const int32 FootprintX = FMath::Max(1, FMath::RoundToInt(Result.FootprintCells.X));
	const int32 FootprintY = FMath::Max(1, FMath::RoundToInt(Result.FootprintCells.Y));
	const int32 RadiusCells = FMath::Clamp(
		Settings->BuildPlacementGuidanceRadiusCells,
		1,
		MaxBuildPlacementGuidanceRadiusCells);
	const int32 FadeBands = FMath::Clamp(
		Settings->BuildPlacementGuidanceFadeBands,
		1,
		MaxBuildPlacementGuidanceFadeBands);
	const bool bAnchorChanged = !bHasBuildPlacementGuidanceAnchor
		|| LastBuildPlacementGuidanceCell != Result.Cell
		|| !LastBuildPlacementGuidanceFootprintCells.Equals(Result.FootprintCells, KINDA_SMALL_NUMBER)
		|| !FMath::IsNearlyEqual(LastBuildPlacementGuidanceCellSize, Result.CellSize)
		|| LastBuildPlacementGuidanceRadiusCells != RadiusCells;
	if (!bAnchorChanged)
	{
		return;
	}

	ClearBuildPlacementGuidance(false);
	if (BuildPlacementGuidanceSampleCache.Num() > 16384)
	{
		BuildPlacementGuidanceSampleCache.Reset();
	}

	UProceduralMeshComponent* Guidance =
		GetOrCreateBuildPlacementGuidanceDisc(PreviewActor);
	if (!Guidance)
	{
		ClearBuildPlacementGuidance(false);
		return;
	}

	TArray<FBuildPlacementGuidanceMeshSection> MeshSections;
	MeshSections.SetNum(MaxBuildPlacementGuidanceFadeBands * 2);

	const FVector2D GridOrigin = Settings->BuildGridWorldOrigin;
	const FIntPoint GridResolution = Settings->BuildGridMapResolution;
	const FVector2D GridMin(
		GridOrigin.X - static_cast<double>(FMath::Max(1, GridResolution.X)) * Result.CellSize * 0.5,
		GridOrigin.Y - static_cast<double>(FMath::Max(1, GridResolution.Y)) * Result.CellSize * 0.5);
	const float GuideCenterCellX = static_cast<float>(Result.Cell.X) + (FootprintX - 1) * 0.5f;
	const float GuideCenterCellY = static_cast<float>(Result.Cell.Y) + (FootprintY - 1) * 0.5f;
	const float CircleRadiusCells = static_cast<float>(RadiusCells) + 0.5f;
	const float CircleRadiusWorld = CircleRadiusCells * Result.CellSize;
	const int32 MinCellX = FMath::FloorToInt(
		GuideCenterCellX - CircleRadiusCells - 1.0f);
	const int32 MaxCellX = FMath::CeilToInt(
		GuideCenterCellX + CircleRadiusCells + 1.0f);
	const int32 MinCellY = FMath::FloorToInt(
		GuideCenterCellY - CircleRadiusCells - 1.0f);
	const int32 MaxCellY = FMath::CeilToInt(
		GuideCenterCellY + CircleRadiusCells + 1.0f);

	// Build one continuous 96-sided circle first, then intersect it with each
	// logical grid cell. The outer edge is therefore a real circular cut, while
	// every interior cell still carries its own blue/yellow buildability state.
	TArray<FVector2D> CirclePolygon;
	CirclePolygon.Reserve(BuildPlacementGuidanceCircleSegments);
	for (int32 SegmentIndex = 0;
		SegmentIndex < BuildPlacementGuidanceCircleSegments;
		++SegmentIndex)
	{
		const float Angle = -2.0f * PI
			* static_cast<float>(SegmentIndex)
			/ static_cast<float>(BuildPlacementGuidanceCircleSegments);
		CirclePolygon.Emplace(
			FMath::Cos(Angle) * CircleRadiusWorld,
			FMath::Sin(Angle) * CircleRadiusWorld);
	}

	for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
	{
		for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
		{
			const bool bInsideFootprint = CellX >= Result.Cell.X
				&& CellX < Result.Cell.X + FootprintX
				&& CellY >= Result.Cell.Y
				&& CellY < Result.Cell.Y + FootprintY;
			if (bInsideFootprint)
			{
				continue;
			}

			const FVector2D LocalCellCenter(
				GridMin.X
					+ (static_cast<double>(CellX) + 0.5) * Result.CellSize
					- Result.WorldLocation.X,
				GridMin.Y
					+ (static_cast<double>(CellY) + 0.5) * Result.CellSize
					- Result.WorldLocation.Y);
			const float HalfCellSize = Result.CellSize * 0.5f;
			TArray<FVector2D> ClippedCell = ClipBuildPlacementGuideCircleToCell(
				CirclePolygon,
				LocalCellCenter.X - HalfCellSize,
				LocalCellCenter.X + HalfCellSize,
				LocalCellCenter.Y - HalfCellSize,
				LocalCellCenter.Y + HalfCellSize);
			if (ClippedCell.Num() < 3)
			{
				continue;
			}

			// Remove duplicate vertices created when a circle vertex lies exactly
			// on a grid edge, avoiding zero-area triangles at the circular cut.
			TArray<FVector2D> CleanCell;
			CleanCell.Reserve(ClippedCell.Num());
			for (const FVector2D& Point : ClippedCell)
			{
				if (CleanCell.IsEmpty()
					|| !CleanCell.Last().Equals(Point, KINDA_SMALL_NUMBER))
				{
					CleanCell.Add(Point);
				}
			}
			if (CleanCell.Num() > 2
				&& CleanCell[0].Equals(CleanCell.Last(), KINDA_SMALL_NUMBER))
			{
				CleanCell.Pop(EAllowShrinking::No);
			}
			if (CleanCell.Num() < 3)
			{
				continue;
			}

			const FIntPoint Cell(CellX, CellY);
			FBuildPlacementGuidanceSample* CachedSample = BuildPlacementGuidanceSampleCache.Find(Cell);
			if (!CachedSample)
			{
				FBuildPlacementGuidanceSample Sample;
				const FVector CandidateLocation(
					GridMin.X + (static_cast<double>(Cell.X) + FootprintX * 0.5) * Result.CellSize,
					GridMin.Y + (static_cast<double>(Cell.Y) + FootprintY * 0.5) * Result.CellSize,
					Result.WorldLocation.Z);
				AActor* GroundActor = nullptr;
				FVector CandidateGroundLocation = CandidateLocation;
				const bool bHasGround = ProjectHashGridSelectionLocationToGround(
					CandidateLocation,
					CandidateGroundLocation,
					Sample.GroundNormal,
					GroundActor);
				if (!bHasGround)
				{
					CandidateGroundLocation = CandidateLocation;
					Sample.GroundNormal = FVector::UpVector;
				}
				Sample.WorldLocation = FVector(
					GridMin.X + (static_cast<double>(Cell.X) + 0.5) * Result.CellSize,
					GridMin.Y + (static_cast<double>(Cell.Y) + 0.5) * Result.CellSize,
					CandidateGroundLocation.Z);

				FRTSHashGridSelectionResult CellResult;
				CellResult.CommandTag = Result.CommandTag;
				CellResult.WorldLocation = CandidateGroundLocation;
				CellResult.Cell = Cell;
				CellResult.CellSize = Result.CellSize;
				CellResult.FootprintCells = Result.FootprintCells;
				CellResult.GroundNormal = Sample.GroundNormal;
				FText InvalidReason;
				Sample.bBuildable = bHasGround
					&& ValidateHashGridSelection(CellResult, GroundActor, InvalidReason);
				CachedSample = &BuildPlacementGuidanceSampleCache.Add(Cell, MoveTemp(Sample));
			}

			const float DistanceCells =
				LocalCellCenter.Size() / FMath::Max(1.0f, Result.CellSize);
			const float NormalizedDistance = FMath::Clamp(
				DistanceCells / CircleRadiusCells,
				0.0f,
				1.0f);
			const int32 FadeBand = FMath::Clamp(
				FMath::FloorToInt(NormalizedDistance * FadeBands),
				0,
				FadeBands - 1);

			const int32 SectionIndex =
				(CachedSample->bBuildable
					? 0
					: MaxBuildPlacementGuidanceFadeBands)
				+ FadeBand;
			FBuildPlacementGuidanceMeshSection& Section =
				MeshSections[SectionIndex];
			const int32 BaseVertex = Section.Vertices.Num();
			const float LocalZ =
				CachedSample->WorldLocation.Z - Result.WorldLocation.Z + 3.0f;

			for (const FVector2D& Point : CleanCell)
			{
				Section.Vertices.Emplace(Point.X, Point.Y, LocalZ);
				Section.Normals.Add(FVector::UpVector);
				Section.UVs.Add(Point / FMath::Max(1.0f, Result.CellSize));
				Section.VertexColors.Add(FLinearColor::White);
				Section.Tangents.Emplace(1.0f, 0.0f, 0.0f);
			}
			for (int32 TriangleIndex = 1;
				TriangleIndex + 1 < CleanCell.Num();
				++TriangleIndex)
			{
				Section.Triangles.Add(BaseVertex);
				Section.Triangles.Add(BaseVertex + TriangleIndex);
				Section.Triangles.Add(BaseVertex + TriangleIndex + 1);
			}
		}
	}

	Guidance->ClearAllMeshSections();
	Guidance->SetWorldLocationAndRotation(
		Result.WorldLocation,
		FRotator::ZeroRotator);
	Guidance->SetWorldScale3D(FVector::OneVector);

	bool bHasVisibleGeometry = false;
	for (int32 SectionIndex = 0;
		SectionIndex < MeshSections.Num();
		++SectionIndex)
	{
		const bool bBuildable =
			SectionIndex < MaxBuildPlacementGuidanceFadeBands;
		const int32 FadeBand = SectionIndex
			% MaxBuildPlacementGuidanceFadeBands;
		const FLinearColor BaseColor = bBuildable
			? Settings->BuildPlacementGuidanceValidColor
			: Settings->BuildPlacementGuidanceInvalidColor;
		const float BandCenter =
			(static_cast<float>(FadeBand) + 0.5f) / FadeBands;
		const float SmoothFade = 1.0f
			- BandCenter * BandCenter * (3.0f - 2.0f * BandCenter);
		if (UMaterialInstanceDynamic* Material =
			Cast<UMaterialInstanceDynamic>(Guidance->GetMaterial(SectionIndex)))
		{
			Material->SetVectorParameterValue(
				BuildPlacementColorParameterName,
				BaseColor);
			Material->SetScalarParameterValue(
				BuildPlacementOpacityParameterName,
				BaseColor.A * SmoothFade);
			Material->SetScalarParameterValue(
				BuildPlacementWorldToGridScaleParameterName,
				1.0f / FMath::Max(1.0f, Result.CellSize));
		}

		FBuildPlacementGuidanceMeshSection& Section =
			MeshSections[SectionIndex];
		if (FadeBand >= FadeBands || Section.Vertices.IsEmpty())
		{
			continue;
		}
		Guidance->CreateMeshSection_LinearColor(
			SectionIndex,
			Section.Vertices,
			Section.Triangles,
			Section.Normals,
			Section.UVs,
			Section.VertexColors,
			Section.Tangents,
			false);
		bHasVisibleGeometry = true;
	}
	Guidance->SetVisibility(bHasVisibleGeometry, true);

	LastBuildPlacementGuidanceCell = Result.Cell;
	LastBuildPlacementGuidanceFootprintCells = Result.FootprintCells;
	LastBuildPlacementGuidanceCellSize = Result.CellSize;
	LastBuildPlacementGuidanceRadiusCells = RadiusCells;
	bHasBuildPlacementGuidanceAnchor = true;
}

void URTSSelector::ClearBuildPlacementGuidance(bool bResetSamples)
{
	AActor* PreviewActor = GetBuildPlacementPreviewActor();
	if (UWorld* World = GetWorld())
	{
		if (ULineBatchComponent* LineBatcher =
			World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
		{
			const uint32 LegacyGuidanceBatchId =
				PointerHash(this, 0x52545347u) | 1u;
			LineBatcher->ClearBatch(LegacyGuidanceBatchId);
		}
	}
	if (UProceduralMeshComponent* Guidance =
		FindBuildPlacementGuidanceDisc(PreviewActor))
	{
		Guidance->ClearAllMeshSections();
		Guidance->SetVisibility(false, true);
	}
	bHasBuildPlacementGuidanceAnchor = false;
	if (bResetSamples)
	{
		BuildPlacementGuidanceSampleCache.Reset();
		LastBuildPlacementGuidanceFootprintCells = FVector2D::ZeroVector;
		LastBuildPlacementGuidanceCellSize = 0.0f;
		LastBuildPlacementGuidanceRadiusCells = 0;
	}
}

void URTSSelector::DrawHashGridSelectionPreview(const FRTSHashGridSelectionResult& Result) const
{
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	UWorld* World = GetWorld();
	ULineBatchComponent* LineBatcher = World
		? World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent)
		: nullptr;
	if (LineBatcher)
	{
		LineBatcher->ClearBatch(GetBuildPlacementLineBatchId(this));
	}
	if (!Settings || !Settings->bEnableHashGridSelectionPreview
		|| !Settings->bDrawBuildPlacementGridLines || !LineBatcher)
	{
		return;
	}

	const int32 CellsX = FMath::Max(1, FMath::RoundToInt(Result.FootprintCells.X));
	const int32 CellsY = FMath::Max(1, FMath::RoundToInt(Result.FootprintCells.Y));
	const int32 MaxLines = FMath::Max(8, Settings->BuildPlacementMaxGridLinesPerAxis);
	const int32 StepX = FMath::Max(1, FMath::CeilToInt(static_cast<float>(CellsX) / MaxLines));
	const int32 StepY = FMath::Max(1, FMath::CeilToInt(static_cast<float>(CellsY) / MaxLines));
	const int32 MajorEvery = FMath::Max(1, Settings->BuildPlacementMajorLineEvery);
	const float Thickness = FMath::Max(0.1f, Settings->BuildPlacementGridLineThickness);
	const FLinearColor LinearColor = Result.bIsValidPlacement
		? Settings->BuildPlacementValidColor
		: Settings->BuildPlacementInvalidColor;
	const FLinearColor MinorColor = LinearColor;
	const FLinearColor MajorColor = FLinearColor::LerpUsingHSV(LinearColor, FLinearColor::White, 0.35f);
	const uint32 BatchId = GetBuildPlacementLineBatchId(this);
	TArray<FBatchedLine> Lines;
	Lines.Reserve(FMath::Min(CellsX + 1, MaxLines + 2) + FMath::Min(CellsY + 1, MaxLines + 2));

	const float HalfWidth = CellsX * Result.CellSize * 0.5f;
	const float HalfHeight = CellsY * Result.CellSize * 0.5f;
	const float Z = Result.WorldLocation.Z + 4.0f;
	const float MinX = Result.WorldLocation.X - HalfWidth;
	const float MaxX = Result.WorldLocation.X + HalfWidth;
	const float MinY = Result.WorldLocation.Y - HalfHeight;
	const float MaxY = Result.WorldLocation.Y + HalfHeight;

	auto DrawVertical = [&](int32 Index)
	{
		const float X = MinX + Index * Result.CellSize;
		const bool bMajor = Index == 0 || Index == CellsX || Index % MajorEvery == 0;
		Lines.Emplace(
			FVector(X, MinY, Z), FVector(X, MaxY, Z),
			bMajor ? MajorColor : MinorColor, -1.0f,
			bMajor ? Thickness * 2.0f : Thickness, 0, BatchId);
	};
	auto DrawHorizontal = [&](int32 Index)
	{
		const float Y = MinY + Index * Result.CellSize;
		const bool bMajor = Index == 0 || Index == CellsY || Index % MajorEvery == 0;
		Lines.Emplace(
			FVector(MinX, Y, Z), FVector(MaxX, Y, Z),
			bMajor ? MajorColor : MinorColor, -1.0f,
			bMajor ? Thickness * 2.0f : Thickness, 0, BatchId);
	};

	for (int32 Index = 0; Index <= CellsX; Index += StepX)
	{
		DrawVertical(Index);
	}
	if (CellsX % StepX != 0)
	{
		DrawVertical(CellsX);
	}
	for (int32 Index = 0; Index <= CellsY; Index += StepY)
	{
		DrawHorizontal(Index);
	}
	if (CellsY % StepY != 0)
	{
		DrawHorizontal(CellsY);
	}

	LineBatcher->DrawLines(Lines);
}

void URTSSelector::EndHashGridSelectionPreview()
{
	if (UWorld* World = GetWorld())
	{
		if (ULineBatchComponent* LineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
		{
			LineBatcher->ClearBatch(GetBuildPlacementLineBatchId(this));
		}
	}
	if (HashGridSelectionDecalComponent)
	{
		HashGridSelectionDecalComponent->SetVisibility(false);
	}
	HideBuildPlacementFootprint(GetBuildPlacementPreviewActor());
	HideBuildPlacementModel(GetBuildPlacementPreviewActor());
	ClearBuildPlacementGuidance(true);
}

void URTSSelector::CommitHashGridSelection()
{
	FRTSHashGridSelectionResult Result;
	const bool bHasResult = GetHashGridSelectionResult(Result);
	const FGameplayTag CommandToIssue = PendingCommandTag;
	if (!bHasResult || !Result.bIsValidPlacement)
	{
		if (bHasResult && !Result.InvalidReason.IsEmpty())
		{
			const FString PlayerMessage = FString::Printf(
				TEXT("无法建造：%s"),
				*Result.InvalidReason.ToString());
			UE_LOG(LogTemp, Display, TEXT("RTS build placement rejected: %s"), *Result.InvalidReason.ToString());
			if (PlayerController)
			{
				PlayerController->ClientMessage(PlayerMessage);
			}
		}
		return;
	}
	const bool bKeepPlacing = PlayerController
		&& (PlayerController->IsInputKeyDown(EKeys::LeftShift)
			|| PlayerController->IsInputKeyDown(EKeys::RightShift));

	OnHashGridSelectionCommitted.Broadcast(Result);

	if (CommandToIssue.IsValid())
	{
		if (ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr)
		{
			if (URTSSelectionSubsystem* SelectionSubsystem = LocalPlayer->GetSubsystem<URTSSelectionSubsystem>())
			{
				SelectionSubsystem->IssueCommandWithLocation(CommandToIssue, Result.WorldLocation, bKeepPlacing);
			}
		}
	}

	if (bKeepPlacing)
	{
		// The just-placed structure changes collision/buildability even when the
		// cursor remains in the same cell. Force the guide to resample.
		ClearBuildPlacementGuidance(true);
		UpdateHashGridSelectionPreview();
		return;
	}

	bIsHashGridSelecting = false;
	EndHashGridSelectionPreview();
	bIsTargeting = false;
	PendingCommandTag = FGameplayTag::EmptyTag;
	PendingTargetType = ERTSCommandTargetType::Location;
	ActiveHashGridFootprintCells = FVector2D::ZeroVector;
	ActiveHashGridCellSize = 0.0f;
	if (PlayerController)
	{
		PlayerController->CurrentMouseCursor = EMouseCursor::Default;
	}
}
