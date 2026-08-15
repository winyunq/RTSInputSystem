// Copyright 2024 Winy unq All Rights Reserved.

#include "UI/RTSActiveGroupWidget.h"
#include "UI/RTSUnitIconWidget.h"
#include "RTSSelectionSubsystem.h" 
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/MassBattleAgentComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SizeBox.h"
#include "DataAssets/MassBattleAgentConfigDataAsset.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Fragments/Render.h"
#include "Fragments/Select.h"
#include "Fragments/Transform.h"
#include "FuncLibs/MassBattleFuncLib.h"
#include "Kismet/GameplayStatics.h"
#include "MassAPISubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"

namespace
{
	FString GetActiveGroupUnitGroupKey(const FRTSUnitData& Data)
	{
		return Data.GroupKey.IsEmpty() ? Data.Name : Data.GroupKey;
	}

	bool GetVisiblePortraitContentBounds(
		UTextureRenderTarget2D* RenderTarget,
		FIntRect& OutBounds)
	{
		if (!RenderTarget)
		{
			return false;
		}

		FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
		TArray<FColor> Pixels;
		if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.IsEmpty())
		{
			return false;
		}

		const int32 Width = RenderTarget->SizeX;
		const int32 Height = RenderTarget->SizeY;
		if (Width <= 0 || Height <= 0 || Pixels.Num() < Width * Height)
		{
			return false;
		}

		const FColor CornerColors[] = {
			Pixels[0],
			Pixels[Width - 1],
			Pixels[(Height - 1) * Width],
			Pixels[Height * Width - 1]
		};
		FColor BackgroundColor(0, 0, 0, 255);
		for (const FColor& Corner : CornerColors)
		{
			BackgroundColor.R += Corner.R / UE_ARRAY_COUNT(CornerColors);
			BackgroundColor.G += Corner.G / UE_ARRAY_COUNT(CornerColors);
			BackgroundColor.B += Corner.B / UE_ARRAY_COUNT(CornerColors);
		}
		FIntPoint Min(Width, Height);
		FIntPoint Max(-1, -1);
		for (int32 Y = 0; Y < Height; Y += 2)
		{
			for (int32 X = 0; X < Width; X += 2)
			{
				const FColor& Pixel = Pixels[Y * Width + X];
				const int32 ColorDifference =
					FMath::Abs(static_cast<int32>(Pixel.R) - static_cast<int32>(BackgroundColor.R))
					+ FMath::Abs(static_cast<int32>(Pixel.G) - static_cast<int32>(BackgroundColor.G))
					+ FMath::Abs(static_cast<int32>(Pixel.B) - static_cast<int32>(BackgroundColor.B));
				if (Pixel.A > 0 && ColorDifference > 36)
				{
					Min.X = FMath::Min(Min.X, X);
					Min.Y = FMath::Min(Min.Y, Y);
					Max.X = FMath::Max(Max.X, X);
					Max.Y = FMath::Max(Max.Y, Y);
				}
			}
		}

		if (Max.X < Min.X || Max.Y < Min.Y)
		{
			return false;
		}
		OutBounds = FIntRect(Min, Max + FIntPoint(1, 1));
		return true;
	}
}

TSharedRef<SWidget> URTSActiveGroupWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* RootSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("DefaultActiveGroupSize"));
		RootSize->SetWidthOverride(PortraitPanelWidth);
		RootSize->SetHeightOverride(PortraitPanelHeight);

		UBorder* Background = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("DefaultActiveGroupBackground"));
		Background->SetBrushColor(FLinearColor(0.006f, 0.018f, 0.028f, 0.99f));
		Background->SetPadding(FMargin(4.0f));
		RootSize->AddChild(Background);

		AvatarImage = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), TEXT("AvatarImage"));
		Background->SetContent(AvatarImage);
		WidgetTree->RootWidget = RootSize;
	}

	const TSharedRef<SWidget> BuiltWidget = Super::RebuildWidget();
	const bool bHasPortraitContent = AvatarImage
		|| GroupIcon
		|| (WidgetTree
			&& (WidgetTree->FindWidget(TEXT("AvatarImage"))
				|| WidgetTree->FindWidget(TEXT("GroupIcon"))));
	if (!bHasPortraitContent)
	{
		// RTSAvatar owns the portrait frame. Derived widgets such as ControlGrid
		// keep their own desired size and must not inherit the 316x512 wrapper.
		return BuiltWidget;
	}
	return SNew(SBox)
		.WidthOverride(FMath::Clamp(PortraitPanelWidth, 96.0f, 512.0f))
		.HeightOverride(FMath::Clamp(PortraitPanelHeight, 128.0f, 512.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.08f, 0.42f, 0.58f, 1.0f))
			.Padding(FMargin(2.0f))
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
				.BorderBackgroundColor(FLinearColor(0.006f, 0.018f, 0.028f, 0.99f))
				.Padding(FMargin(4.0f))
				[
					BuiltWidget
				]
			]
		];
}

void URTSActiveGroupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* Subsystem = LP->GetSubsystem<URTSSelectionSubsystem>())
			{
				Subsystem->OnSelectionChanged.AddUniqueDynamic(this, &URTSActiveGroupWidget::OnSelectionUpdated);
			}
		}
	}
}

void URTSActiveGroupWidget::NativeDestruct()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* Subsystem = LP->GetSubsystem<URTSSelectionSubsystem>())
			{
				Subsystem->OnSelectionChanged.RemoveDynamic(this, &URTSActiveGroupWidget::OnSelectionUpdated);
			}
		}
	}
	StopLivePortrait();

	Super::NativeDestruct();
}

void URTSActiveGroupWidget::NativeTick(
	const FGeometry& MyGeometry,
	float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const bool bHasMassPreview = IsValid(PortraitPreviewAgentComponent)
		&& PortraitPreviewAgentComponent->IsInitialized();
	if (!bHasMassPreview || !PortraitCaptureComponent)
	{
		return;
	}

	PortraitCaptureAccumulator += InDeltaTime;
	const float CaptureInterval = 1.0f / FMath::Max(1.0f, PortraitCaptureRate);
	if (PortraitCaptureAccumulator >= CaptureInterval)
	{
		PortraitCaptureAccumulator = FMath::Fmod(
			PortraitCaptureAccumulator,
			CaptureInterval);
		CapturePortraitFrame();
	}
}

bool URTSActiveGroupWidget::StartMassPreviewPortrait(const FRTSUnitData& Data)
{
	if (!bEnableLivePortrait
		|| !AvatarImage
		|| Data.UnitAssetPath.IsEmpty()
		|| !EnsurePortraitCaptureResources())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_Standalone)
	{
		// A local-only preview entity would diverge lockstep state in network play.
		return false;
	}
	UMassBattleAgentConfigDataAsset* UnitConfig =
		LoadObject<UMassBattleAgentConfigDataAsset>(
			nullptr,
			*Data.UnitAssetPath);
	if (!UnitConfig)
	{
		return false;
	}

	if (PortraitPreviewUnitAssetPath == Data.UnitAssetPath
		&& IsValid(PortraitPreviewActor)
		&& IsValid(PortraitPreviewAgentComponent)
		&& PortraitPreviewAgentComponent->IsInitialized())
	{
		PortraitCaptureAccumulator = 0.0f;
		CapturePortraitFrame();
		return true;
	}

	DestroyMassPreviewPortrait();

	FVector PreviewStageLocation = PortraitPreviewLocation;
	if (const APlayerController* PlayerController = GetOwningPlayer())
	{
		if (const APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
		{
			// Keep the Mass preview inside the primary view frustum so its renderer does not
			// cull it, but place it well beyond the terrain along the camera ray so it cannot
			// leak into the gameplay view.
			PreviewStageLocation = CameraManager->GetCameraLocation()
				+ CameraManager->GetCameraRotation().Vector() * 20000.0f;
		}
	}
	const FTransform PreviewTransform(FRotator::ZeroRotator, PreviewStageLocation);
	AActor* PreviewActor = World->SpawnActorDeferred<AActor>(
		AActor::StaticClass(),
		PreviewTransform,
		GetOwningPlayer(),
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!PreviewActor)
	{
		return false;
	}

	UMassBattleAgentComponent* PreviewAgent = NewObject<UMassBattleAgentComponent>(
		PreviewActor,
		TEXT("RTSMassPortraitAgent"),
		RF_Transient);
	if (!PreviewAgent)
	{
		PreviewActor->Destroy();
		return false;
	}
	PreviewAgent->InitializationMode = EAgentInitMode::Manual;
	PreviewAgent->AgentConfigAsset = UnitConfig;
	PreviewAgent->TeamIndex = 0;
	PreviewAgent->LocationSyncMode = EMassAgentSyncMode::None;
	PreviewAgent->RotationSyncMode = EMassAgentSyncMode::None;
	PreviewAgent->ScaleSyncMode = EMassAgentSyncMode::None;
	PreviewAgent->bDestroyEntityOnEndPlay = true;
	PreviewActor->AddInstanceComponent(PreviewAgent);
	PreviewActor->SetRootComponent(PreviewAgent);

	UPointLightComponent* KeyLight = NewObject<UPointLightComponent>(
		PreviewActor,
		TEXT("RTSMassPortraitKeyLight"),
		RF_Transient);
	UPointLightComponent* FillLight = NewObject<UPointLightComponent>(
		PreviewActor,
		TEXT("RTSMassPortraitFillLight"),
		RF_Transient);
	if (KeyLight)
	{
		KeyLight->SetupAttachment(PreviewAgent);
		KeyLight->SetIntensity(6500.0f);
		KeyLight->SetLightColor(FLinearColor(0.86f, 0.94f, 1.0f));
		KeyLight->SetCastShadows(true);
		PreviewActor->AddInstanceComponent(KeyLight);
	}
	if (FillLight)
	{
		FillLight->SetupAttachment(PreviewAgent);
		FillLight->SetIntensity(2200.0f);
		FillLight->SetLightColor(FLinearColor(0.18f, 0.48f, 0.72f));
		FillLight->SetCastShadows(false);
		PreviewActor->AddInstanceComponent(FillLight);
	}

	UGameplayStatics::FinishSpawningActor(PreviewActor, PreviewTransform);
	PreviewActor->SetActorEnableCollision(false);
	if (!PreviewAgent->IsRegistered())
	{
		PreviewAgent->RegisterComponent();
	}
	if (!PreviewAgent->ManualInitialize())
	{
		PreviewActor->Destroy();
		return false;
	}

	PortraitPreviewActor = PreviewActor;
	PortraitPreviewAgentComponent = PreviewAgent;
	PortraitPreviewUnitConfig = UnitConfig;
	PortraitKeyLightComponent = KeyLight;
	PortraitFillLightComponent = FillLight;
	PortraitPreviewUnitAssetPath = Data.UnitAssetPath;

	if (UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this))
	{
		const FEntityHandle PreviewEntity = PreviewAgent->GetEntityHandle();
		if (MassAPI->IsValid(PreviewEntity))
		{
			UMassBattleFuncLib::SetAgentControlMode(
				this,
				PreviewEntity,
				EAgentControlMode::PlayerDriven);
			if (FSelect* Select = MassAPI->GetFragmentPtr<FSelect>(PreviewEntity))
			{
				Select->bEnable = false;
			}
			if (FVisualize* Visualize = MassAPI->GetFragmentPtr<FVisualize>(PreviewEntity))
			{
				Visualize->bEnable = true;
			}
		}
	}

	PortraitCaptureComponent->ClearShowOnlyComponents();
	PortraitCaptureComponent->PrimitiveRenderMode =
		ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	PortraitCaptureAccumulator = 0.0f;
	PortraitMassValidationAttempt = 0;
	bPortraitLiveFrameConfirmed = false;
	CapturePortraitFrame();
	return true;
}

bool URTSActiveGroupWidget::EnsurePortraitCaptureResources()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!PortraitRenderTarget)
	{
		PortraitRenderTarget = NewObject<UTextureRenderTarget2D>(this);
		PortraitRenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
		PortraitRenderTarget->ClearColor = FLinearColor(0.004f, 0.012f, 0.02f, 1.0f);
		const int32 TargetHeight = FMath::Clamp(
			PortraitRenderTargetHeight,
			256,
			1024);
		const float PanelAspect = FMath::Clamp(PortraitPanelWidth, 96.0f, 512.0f)
			/ FMath::Clamp(PortraitPanelHeight, 128.0f, 512.0f);
		const int32 TargetWidth = FMath::Clamp(
			FMath::RoundToInt(static_cast<float>(TargetHeight) * PanelAspect),
			128,
			1024);
		PortraitRenderTarget->InitAutoFormat(TargetWidth, TargetHeight);
		PortraitRenderTarget->UpdateResourceImmediate(true);
	}

	if (!PortraitCaptureComponent)
	{
		PortraitCaptureComponent = NewObject<USceneCaptureComponent2D>(
			GetOwningPlayer(),
			TEXT("RTSUnitPortraitCapture"));
		PortraitCaptureComponent->RegisterComponentWithWorld(World);
		PortraitCaptureComponent->TextureTarget = PortraitRenderTarget;
		PortraitCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		PortraitCaptureComponent->PrimitiveRenderMode =
			ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		PortraitCaptureComponent->MaxViewDistanceOverride = 5000.0f;
		PortraitCaptureComponent->ShowFlags.SetLandscape(false);
		PortraitCaptureComponent->ShowFlags.SetAtmosphere(false);
		PortraitCaptureComponent->ShowFlags.SetFog(false);
		PortraitCaptureComponent->bCaptureEveryFrame = false;
		PortraitCaptureComponent->bCaptureOnMovement = false;
	}

	PortraitCaptureComponent->FOVAngle = PortraitFieldOfView;
	return true;
}

void URTSActiveGroupWidget::ApplyLivePortraitBrush()
{
	if (!AvatarImage || !PortraitRenderTarget)
	{
		return;
	}

	FSlateBrush LiveBrush = AvatarImage->GetBrush();
	LiveBrush.SetResourceObject(PortraitRenderTarget);
	LiveBrush.ImageSize = FVector2D(
		PortraitRenderTarget->SizeX,
		PortraitRenderTarget->SizeY);
	LiveBrush.DrawAs = ESlateBrushDrawType::Image;
	AvatarImage->SetBrush(LiveBrush);
	AvatarImage->SetColorAndOpacity(FLinearColor::White);
	AvatarImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (GroupIcon)
	{
		GroupIcon->SetVisibility(ESlateVisibility::Hidden);
	}
}

void URTSActiveGroupWidget::StopLivePortrait()
{
	PortraitCaptureAccumulator = 0.0f;
	if (PortraitCaptureComponent)
	{
		PortraitCaptureComponent->ClearShowOnlyComponents();
		PortraitCaptureComponent->DestroyComponent();
		PortraitCaptureComponent = nullptr;
	}
	DestroyMassPreviewPortrait();
	PortraitRenderTarget = nullptr;
}

void URTSActiveGroupWidget::DestroyMassPreviewPortrait()
{
	if (IsValid(PortraitPreviewAgentComponent))
	{
		if (UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this))
		{
			const FEntityHandle PreviewEntity =
				PortraitPreviewAgentComponent->GetEntityHandle();
			if (MassAPI->IsValid(PreviewEntity))
			{
				if (FVisualize* Visualize =
					MassAPI->GetFragmentPtr<FVisualize>(PreviewEntity))
				{
					Visualize->bEnable = false;
				}
			}
		}
	}
	if (IsValid(PortraitPreviewActor))
	{
		PortraitPreviewActor->Destroy();
	}
	PortraitPreviewActor = nullptr;
	PortraitPreviewAgentComponent = nullptr;
	PortraitPreviewUnitConfig = nullptr;
	PortraitKeyLightComponent = nullptr;
	PortraitFillLightComponent = nullptr;
	PortraitPreviewUnitAssetPath.Reset();
	PortraitMassValidationAttempt = 0;
	bPortraitLiveFrameConfirmed = false;
}

void URTSActiveGroupWidget::CapturePortraitFrame()
{
	const bool bHasMassPreview = IsValid(PortraitPreviewAgentComponent)
		&& PortraitPreviewAgentComponent->IsInitialized()
		&& IsValid(PortraitPreviewUnitConfig);
	if (!bHasMassPreview || !PortraitCaptureComponent)
	{
		return;
	}

	FVector BoundsOrigin = IsValid(PortraitPreviewActor)
		? PortraitPreviewActor->GetActorLocation()
		: PortraitPreviewLocation;
	if (UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this))
	{
		const FEntityHandle PreviewEntity =
			PortraitPreviewAgentComponent->GetEntityHandle();
		if (MassAPI->IsValid(PreviewEntity))
		{
			if (const FLocating* Locating =
				MassAPI->GetFragmentPtr<FLocating>(PreviewEntity))
			{
				BoundsOrigin = Locating->Location;
			}
		}
	}
	const float UnitScale = FMath::Max(
		UE_KINDA_SMALL_NUMBER,
		PortraitPreviewUnitConfig->Scaling.Scale);
	const float ScaledRadius = FMath::Max(
		12.0f,
		PortraitPreviewUnitConfig->Collider.Radius * UnitScale);
	const float CapsuleHalfHeight = FMath::Max(
		ScaledRadius,
		(PortraitPreviewUnitConfig->Collider.Radius
			+ PortraitPreviewUnitConfig->Collider.Height * 0.5f)
		* UnitScale);
	const FVector BoundsExtent(ScaledRadius, ScaledRadius, CapsuleHalfHeight);
	const float HalfFovRadians = FMath::DegreesToRadians(
		FMath::Clamp(PortraitFieldOfView, 10.0f, 90.0f) * 0.5f);
	const float HorizontalTan = FMath::Max(0.1f, FMath::Tan(HalfFovRadians));
	const float CaptureAspect = PortraitRenderTarget && PortraitRenderTarget->SizeY > 0
		? static_cast<float>(PortraitRenderTarget->SizeX)
			/ static_cast<float>(PortraitRenderTarget->SizeY)
		: FMath::Clamp(PortraitPanelWidth, 96.0f, 512.0f)
			/ FMath::Clamp(PortraitPanelHeight, 128.0f, 512.0f);
	const float VerticalTan = HorizontalTan / FMath::Max(0.1f, CaptureAspect);
	const float DistanceForWidth = FMath::Max(25.0f, BoundsExtent.Y) / HorizontalTan;
	const float DistanceForHeight = FMath::Max(25.0f, BoundsExtent.Z) / VerticalTan;
	float Distance = FMath::Max(DistanceForWidth, DistanceForHeight)
		* FMath::Max(1.0f, PortraitFramingPadding)
		+ FMath::Max(0.0f, BoundsExtent.X);
	const FVector FocusPoint = BoundsOrigin + FVector(
		0.0f,
		0.0f,
		BoundsExtent.Z * 0.08f);
	const FVector ViewDirection = PortraitViewDirection.Vector().GetSafeNormal();
	const FVector CameraLocation = FocusPoint + ViewDirection * Distance;
	const float LightRadius = FMath::Max(500.0f, Distance * 3.0f);
	if (PortraitKeyLightComponent)
	{
		PortraitKeyLightComponent->SetAttenuationRadius(LightRadius);
		PortraitKeyLightComponent->SetWorldLocation(
			FocusPoint + FVector(Distance * 0.45f, -Distance * 0.35f, Distance * 0.5f));
	}
	if (PortraitFillLightComponent)
	{
		PortraitFillLightComponent->SetAttenuationRadius(LightRadius);
		PortraitFillLightComponent->SetWorldLocation(
			FocusPoint + FVector(Distance * 0.2f, Distance * 0.5f, Distance * 0.15f));
	}

	PortraitCaptureComponent->SetWorldLocationAndRotation(
		CameraLocation,
		(FocusPoint - CameraLocation).Rotation());
	PortraitCaptureComponent->CaptureScene();

	if (bHasMassPreview && !bPortraitLiveFrameConfirmed)
	{
		++PortraitMassValidationAttempt;
		const bool bShouldValidate = PortraitMassValidationAttempt == 4
			|| PortraitMassValidationAttempt == 12;
		FIntRect ContentBounds;
		if (bShouldValidate
			&& GetVisiblePortraitContentBounds(PortraitRenderTarget, ContentBounds))
		{
			bPortraitLiveFrameConfirmed = true;
			ApplyLivePortraitBrush();
		}
		else if (PortraitMassValidationAttempt >= 12)
		{
			// The 2D portrait is already visible underneath this attempt. Give up the
			// local preview instead of ever replacing it with an empty render target.
			DestroyMassPreviewPortrait();
		}
	}
}

void URTSActiveGroupWidget::ApplyStaticPortrait(UTexture2D* Texture)
{
	DestroyMassPreviewPortrait();
	if (PortraitCaptureComponent)
	{
		PortraitCaptureComponent->ClearShowOnlyComponents();
	}

	if (AvatarImage && Texture)
	{
		AvatarImage->SetBrushFromTexture(Texture);
		AvatarImage->SetColorAndOpacity(FLinearColor::White);
		AvatarImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (GroupIcon)
		{
			GroupIcon->SetVisibility(ESlateVisibility::Hidden);
		}
	}
	else if (AvatarImage)
	{
		AvatarImage->SetVisibility(ESlateVisibility::Hidden);
	}
}

void URTSActiveGroupWidget::OnSelectionUpdated(const FRTSSelectionView& View)
{
	const FRTSUnitData* ActiveData = nullptr;
	FString ActiveKey = View.ActiveGroupKey;
	
	if (!ActiveKey.IsEmpty())
	{
		ActiveData = View.Items.FindByPredicate([&](const FRTSUnitData& Item) {
			return GetActiveGroupUnitGroupKey(Item) == ActiveKey;
		});
	}

	// Fallback: If no ActiveKey but items exist, use first item.
	if (!ActiveData && View.Items.Num() > 0)
	{
		ActiveData = &View.Items[0];
	}

	if (ActiveData)
	{
		// We have an active group/unit.
		// If we wrap an internal icon widget, update it.
		if (GroupIcon)
		{
			// Active avatar uses the pushed portrait when available; roster cells keep their small icon.
			FRTSUnitData AvatarData = *ActiveData;
			if (AvatarData.Portrait)
			{
				AvatarData.Icon = AvatarData.Portrait;
			}
			GroupIcon->InitData(AvatarData, true, true);
			GroupIcon->SetIsActive(true);
			GroupIcon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}

		UTexture2D* AvatarTexture = ActiveData->Portrait
			? ActiveData->Portrait
			: ActiveData->Icon;
		const bool bAlreadyShowingThisUnitType =
			PortraitPreviewUnitAssetPath == ActiveData->UnitAssetPath
			&& IsValid(PortraitPreviewAgentComponent)
			&& PortraitPreviewAgentComponent->IsInitialized();
		if (!bAlreadyShowingThisUnitType)
		{
			ApplyStaticPortrait(AvatarTexture);
		}
		if (!StartMassPreviewPortrait(*ActiveData))
		{
			ApplyStaticPortrait(AvatarTexture);
			if (!AvatarTexture && AvatarImage)
			{
				AvatarImage->SetVisibility(ESlateVisibility::Hidden);
			}
		}
		
		// Ensure self is visible (hit test invisible to allow tooltips on children)
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		
		// Notify BP
		OnActiveGroupChanged(*ActiveData, true);
	}
	else
	{
		// No selection at all.
		StopLivePortrait();
		SetVisibility(ESlateVisibility::Hidden);
		if (AvatarImage)
		{
			AvatarImage->SetVisibility(ESlateVisibility::Hidden);
		}
		
		// Notify BP (Empty Data)
		OnActiveGroupChanged(FRTSUnitData(), false);
	}
}
