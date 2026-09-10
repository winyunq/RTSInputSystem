// Copyright 2024 Winy unq All Rights Reserved.

#include "UI/RTSActiveGroupWidget.h"
#include "UI/RTSUnitIconWidget.h"
#include "RTSSelectionSubsystem.h" 
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SizeBox.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Fragments/RenderBatchData.h"
#include "Fragments/Render.h"
#include "Fragments/Transform.h"
#include "MassAPISubsystem.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "Renderers/MassBattleAgentRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"

namespace
{
	const FVector PortraitStageLocation(0.0, 0.0, 100000.0);

	FString GetActiveGroupUnitGroupKey(const FRTSUnitData& Data)
	{
		return Data.GroupKey.IsEmpty() ? Data.Name : Data.GroupKey;
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
	if (PortraitSourceEntity.IsSet()
		&& PortraitPreviewComponent
		&& PortraitCaptureComponent)
	{
		CapturePortraitFrame();
	}
}

bool URTSActiveGroupWidget::StartSelectedUnitPortrait(const FRTSUnitData& Data)
{
	if (!bEnableLivePortrait || !AvatarImage || !EnsurePortraitCaptureResources())
	{
		return false;
	}

	if (!Data.bIsMassEntity || !Data.EntityHandle.IsSet())
	{
		return false;
	}

	UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this);
	if (!MassAPI || !MassAPI->IsValid(Data.EntityHandle))
	{
		return false;
	}

	const FVisualizing* Visualizing = MassAPI->GetFragmentPtr<FVisualizing>(Data.EntityHandle);
	AMassBattleAgentRenderer* Renderer = Visualizing
		? Cast<AMassBattleAgentRenderer>(Visualizing->RendererActor.Get())
		: nullptr;
	if (!Renderer
		|| Visualizing->RenderBatchId < 0
		|| Visualizing->InstanceId < 0
		|| !EnsurePortraitPreviewComponent(Renderer))
	{
		return false;
	}

	ClearPortraitSource();
	PortraitSourceEntity = Data.EntityHandle;
	PortraitSourceRenderer = Renderer;
	if (!UpdatePortraitPreview())
	{
		DestroyPortraitPreview();
		return false;
	}

	PortraitCaptureComponent->CaptureScene();
	ApplyLivePortraitBrush();
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

bool URTSActiveGroupWidget::EnsurePortraitPreviewComponent(
	AMassBattleAgentRenderer* Renderer)
{
	if (!IsValid(Renderer)
		|| !IsValid(Renderer->NiagaraSystemAsset)
		|| !IsValid(Renderer->AgentMesh)
		|| !PortraitCaptureComponent)
	{
		return false;
	}

	if (PortraitPreviewComponent
		&& PortraitPreviewComponent->GetAsset() != Renderer->NiagaraSystemAsset)
	{
		DestroyPortraitPreview();
	}

	if (!PortraitPreviewComponent)
	{
		// Controllers are hidden actors. Their primitive components inherit that
		// visibility even when a scene capture explicitly includes the component.
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PortraitPreviewActor = GetWorld()->SpawnActor<AActor>(SpawnParameters);
		if (!PortraitPreviewActor)
		{
			return false;
		}
		PortraitPreviewActor->SetActorEnableCollision(false);
		PortraitPreviewComponent = NewObject<UNiagaraComponent>(
			PortraitPreviewActor,
			NAME_None,
			RF_Transient);
		if (!PortraitPreviewComponent)
		{
			DestroyPortraitPreview();
			return false;
		}
		PortraitPreviewActor->SetRootComponent(PortraitPreviewComponent);

		PortraitPreviewComponent->SetAsset(Renderer->NiagaraSystemAsset);
		PortraitPreviewComponent->SetForceSolo(true);
		PortraitPreviewComponent->SetAllowScalability(false);
		PortraitPreviewComponent->SetAutoDestroy(false);
		PortraitPreviewComponent->SetCastShadow(true);
		PortraitPreviewComponent->SetVisibleInSceneCaptureOnly(true);
		PortraitPreviewComponent->SetSystemFixedBounds(
			FBox(FVector(-50000.0), FVector(50000.0)));
		PortraitPreviewComponent->SetVariableStaticMesh(
			TEXT("AgentMesh"),
			Renderer->AgentMesh);
		PortraitPreviewComponent->RegisterComponentWithWorld(GetWorld());
		PortraitPreviewComponent->Activate(true);
	}
	else
	{
		PortraitPreviewComponent->SetVariableStaticMesh(
			TEXT("AgentMesh"),
			Renderer->AgentMesh);
	}

	if (!PortraitKeyLightComponent)
	{
		PortraitKeyLightComponent = NewObject<UPointLightComponent>(
			PortraitPreviewActor,
			NAME_None,
			RF_Transient);
		if (PortraitKeyLightComponent)
		{
			PortraitKeyLightComponent->SetMobility(EComponentMobility::Movable);
			PortraitKeyLightComponent->SetIntensity(6500.0f);
			PortraitKeyLightComponent->SetLightColor(
				FLinearColor(0.86f, 0.94f, 1.0f));
			PortraitKeyLightComponent->SetCastShadows(true);
			PortraitKeyLightComponent->RegisterComponentWithWorld(GetWorld());
		}
	}
	if (!PortraitFillLightComponent)
	{
		PortraitFillLightComponent = NewObject<UPointLightComponent>(
			PortraitPreviewActor,
			NAME_None,
			RF_Transient);
		if (PortraitFillLightComponent)
		{
			PortraitFillLightComponent->SetMobility(EComponentMobility::Movable);
			PortraitFillLightComponent->SetIntensity(2200.0f);
			PortraitFillLightComponent->SetLightColor(
				FLinearColor(0.18f, 0.48f, 0.72f));
			PortraitFillLightComponent->SetCastShadows(false);
			PortraitFillLightComponent->RegisterComponentWithWorld(GetWorld());
		}
	}

	PortraitCaptureComponent->PrimitiveRenderMode =
		ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	PortraitCaptureComponent->ClearShowOnlyComponents();
	PortraitCaptureComponent->ShowOnlyComponent(PortraitPreviewComponent);
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
	DestroyPortraitPreview();
	if (PortraitCaptureComponent)
	{
		PortraitCaptureComponent->DestroyComponent();
		PortraitCaptureComponent = nullptr;
	}
	PortraitRenderTarget = nullptr;
}

void URTSActiveGroupWidget::DestroyPortraitPreview()
{
	ClearPortraitSource();
	if (PortraitCaptureComponent)
	{
		PortraitCaptureComponent->ClearShowOnlyComponents();
	}
	if (PortraitPreviewComponent)
	{
		PortraitPreviewComponent->DestroyComponent();
		PortraitPreviewComponent = nullptr;
	}
	if (PortraitKeyLightComponent)
	{
		PortraitKeyLightComponent->DestroyComponent();
		PortraitKeyLightComponent = nullptr;
	}
	if (PortraitFillLightComponent)
	{
		PortraitFillLightComponent->DestroyComponent();
		PortraitFillLightComponent = nullptr;
	}
	if (PortraitPreviewActor)
	{
		PortraitPreviewActor->Destroy();
		PortraitPreviewActor = nullptr;
	}
}

void URTSActiveGroupWidget::ClearPortraitSource()
{
	PortraitSourceEntity = FEntityHandle();
	PortraitSourceRenderer.Reset();
}

bool URTSActiveGroupWidget::UpdatePortraitPreview()
{
	if (!PortraitCaptureComponent
		|| !PortraitPreviewComponent
		|| !PortraitSourceEntity.IsSet())
	{
		return false;
	}

	UMassAPISubsystem* MassAPI = UMassAPISubsystem::GetPtr(this);
	if (!MassAPI || !MassAPI->IsValid(PortraitSourceEntity))
	{
		return false;
	}

	const FVisualizing* Visualizing =
		MassAPI->GetFragmentPtr<FVisualizing>(PortraitSourceEntity);
	AMassBattleAgentRenderer* Renderer = PortraitSourceRenderer.Get();
	if (!Visualizing
		|| !IsValid(Renderer)
		|| Visualizing->RendererActor.Get() != Renderer
		|| !IsValid(Renderer->AgentMesh))
	{
		return false;
	}

	const FAgentRenderBatchData* Batch =
		Renderer->SpawnedRenderBatches.Find(Visualizing->RenderBatchId);
	const int32 SourceIndex = Visualizing->InstanceId;
	if (!Batch
		|| !Batch->LocationArray.IsValidIndex(SourceIndex)
		|| !Batch->OrientationArray.IsValidIndex(SourceIndex)
		|| !Batch->ScaleArray.IsValidIndex(SourceIndex)
		|| !Batch->DynamicParams0_Array.IsValidIndex(SourceIndex))
	{
		return false;
	}

	const FVector PreviewStageLocation = PortraitStageLocation;

	const FLocating* Locating =
		MassAPI->GetFragmentPtr<FLocating>(PortraitSourceEntity);
	const FRotating* Rotating =
		MassAPI->GetFragmentPtr<FRotating>(PortraitSourceEntity);
	const FVector SourceParentLocation = Locating
		? Locating->Location
		: Batch->LocationArray[SourceIndex];
	const FQuat SourceFacing = Rotating
		? FQuat(Rotating->RotationQuat)
		: FQuat::Identity;

	const bool bHasRelativeTransform = Batch->bNewPredictionModel
		&& Batch->RelLocArray.IsValidIndex(SourceIndex)
		&& Batch->RelRotArray.IsValidIndex(SourceIndex)
		&& Batch->RelScaleArray.IsValidIndex(SourceIndex);
	const FVector RelativeLocation = bHasRelativeTransform
		? FVector(Batch->RelLocArray[SourceIndex])
		: SourceFacing.Inverse().RotateVector(
			Batch->LocationArray[SourceIndex] - SourceParentLocation);
	const FQuat RelativeRotation = bHasRelativeTransform
		? FQuat(Batch->RelRotArray[SourceIndex])
		: SourceFacing.Inverse() * FQuat(Batch->OrientationArray[SourceIndex]);
	const FVector PreviewScale = bHasRelativeTransform
		? FVector(Batch->RelScaleArray[SourceIndex])
		: FVector(Batch->ScaleArray[SourceIndex]);
	const FVector PreviewRenderLocation = PreviewStageLocation + RelativeLocation;
	const FQuat PreviewRenderRotation = RelativeRotation.GetNormalized();
	const FVector NiagaraLocation = bHasRelativeTransform
		? PreviewStageLocation
		: PreviewRenderLocation;
	const FQuat4f NiagaraOrientation = bHasRelativeTransform
		? FQuat4f::Identity
		: FQuat4f(PreviewRenderRotation);
	const FVector3f NiagaraScale = bHasRelativeTransform
		? FVector3f(1.0f)
		: FVector3f(PreviewScale);

	PortraitPreviewComponent->SetWorldLocation(PreviewStageLocation);
	PortraitPreviewComponent->SetSystemFixedBounds(
		FBox(FVector(-50000.0), FVector(50000.0)));
	PortraitPreviewComponent->SetVariableStaticMesh(
		TEXT("AgentMesh"),
		Renderer->AgentMesh);

	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
		PortraitPreviewComponent,
		FName("LocationArray"),
		TArray<FVector>{NiagaraLocation});
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(
		PortraitPreviewComponent,
		FName("OrientationArray"),
		TArray<FQuat4f>{NiagaraOrientation});
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
		PortraitPreviewComponent,
		FName("ScaleArray"),
		TArray<FVector3f>{NiagaraScale});
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(
		PortraitPreviewComponent,
		FName("DynamicParams0_Array"),
		TArray<FVector4f>{Batch->DynamicParams0_Array[SourceIndex]});
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
		PortraitPreviewComponent,
		FName("HealthBar_Opacity_CurrentRatio_TargetRatio_Array"),
		TArray<FVector3f>{FVector3f::ZeroVector});
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayBool(
		PortraitPreviewComponent,
		FName("IsHidden_Array"),
		TArray<bool>{false});

	if (Batch->bUsePositionArray)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(
			PortraitPreviewComponent,
			FName("PositionArray"),
			TArray<FVector>{NiagaraLocation});
	}
	if (Batch->bUseMeshIndexArray)
	{
		const int32 RequestedLOD = Batch->CurrentLODArray.IsValidIndex(SourceIndex)
			? Batch->CurrentLODArray[SourceIndex]
			: 0;
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(
			PortraitPreviewComponent,
			FName("MeshIndex_Array"),
			TArray<int32>{Renderer->GetRenderableMeshIndex(RequestedLOD)});
	}
	if (Batch->bUseStyleArray)
	{
		const int32 Style = Batch->StyleArray.IsValidIndex(SourceIndex)
			? Batch->StyleArray[SourceIndex]
			: 0;
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(
			PortraitPreviewComponent,
			FName("StyleArray"),
			TArray<int32>{Style});
	}
	if (Batch->bUseVelocityArray)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
			PortraitPreviewComponent,
			FName("VelocityArray"),
			TArray<FVector3f>{FVector3f::ZeroVector});
	}
	if (Batch->bUseAngVelArray)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
			PortraitPreviewComponent,
			FName("AngVelArray"),
			TArray<FVector3f>{FVector3f::ZeroVector});
	}
	if (Batch->bUseInterpParamsArray)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(
			PortraitPreviewComponent,
			FName("InterpParamsArray"),
			TArray<FVector4f>{FVector4f::Zero()});
	}
	if (Batch->bUseAnimTracksA)
	{
		const FVector4f Tracks = Batch->AnimTracksA.IsValidIndex(SourceIndex)
			? Batch->AnimTracksA[SourceIndex]
			: FVector4f::Zero();
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(
			PortraitPreviewComponent,
			FName("AnimTracksA"),
			TArray<FVector4f>{Tracks});
	}
	if (Batch->bUseAnimTracksB)
	{
		const FVector4f Tracks = Batch->AnimTracksB.IsValidIndex(SourceIndex)
			? Batch->AnimTracksB[SourceIndex]
			: FVector4f::Zero();
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(
			PortraitPreviewComponent,
			FName("AnimTracksB"),
			TArray<FVector4f>{Tracks});
	}
	if (Batch->bUseUniqueIDArray)
	{
		const int32 UniqueId = Batch->UniqueIDArray.IsValidIndex(SourceIndex)
			? FMath::Max(1, Batch->UniqueIDArray[SourceIndex])
			: 1;
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(
			PortraitPreviewComponent,
			FName("UniqueIDArray"),
			TArray<int32>{UniqueId});
	}
	if (Batch->bUseRelLocArray)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
			PortraitPreviewComponent,
			FName("RelLocArray"),
			TArray<FVector3f>{FVector3f(RelativeLocation)});
	}
	if (Batch->bUseRelRotArray)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayQuat(
			PortraitPreviewComponent,
			FName("RelRotArray"),
			TArray<FQuat4f>{FQuat4f(PreviewRenderRotation)});
	}
	if (Batch->bUseRelScaleArray)
	{
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(
			PortraitPreviewComponent,
			FName("RelScaleArray"),
			TArray<FVector3f>{FVector3f(PreviewScale)});
	}

	PortraitPreviewComponent->SetVariableBool(FName("EnableTextPop"), false);
	PortraitPreviewComponent->SetVariableInt(
		FName("SubType"),
		Renderer->SubType.Index);
	PortraitPreviewComponent->SetVariableFloat(
		FName("User.LogicTickTime"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);

	// Frame the rendered model independently of its gameplay collision shape.
	const FBoxSphereBounds PreviewBounds = Renderer->AgentMesh->GetBounds().TransformBy(
		FTransform(PreviewRenderRotation, PreviewRenderLocation, PreviewScale));
	const FVector BoundsExtent = PreviewBounds.BoxExtent.ComponentMax(FVector(0.1));

	const float HalfFovRadians = FMath::DegreesToRadians(
		FMath::Clamp(PortraitFieldOfView, 10.0f, 90.0f) * 0.5f);
	const float HorizontalTan = FMath::Max(0.1f, FMath::Tan(HalfFovRadians));
	const float CaptureAspect = PortraitRenderTarget && PortraitRenderTarget->SizeY > 0
		? static_cast<float>(PortraitRenderTarget->SizeX)
			/ static_cast<float>(PortraitRenderTarget->SizeY)
		: FMath::Clamp(PortraitPanelWidth, 96.0f, 512.0f)
			/ FMath::Clamp(PortraitPanelHeight, 128.0f, 512.0f);
	const float VerticalTan = HorizontalTan / FMath::Max(0.1f, CaptureAspect);
	const float DistanceForWidth = BoundsExtent.Y / HorizontalTan;
	const float DistanceForHeight = BoundsExtent.Z / VerticalTan;
	float Distance = FMath::Max(DistanceForWidth, DistanceForHeight)
		* FMath::Max(1.0f, PortraitFramingPadding)
		+ FMath::Max(0.0f, BoundsExtent.X);
	const FVector FocusPoint = PreviewBounds.Origin + FVector(
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
			FocusPoint
			+ FVector(Distance * 0.45f, -Distance * 0.35f, Distance * 0.5f));
	}
	if (PortraitFillLightComponent)
	{
		PortraitFillLightComponent->SetAttenuationRadius(LightRadius);
		PortraitFillLightComponent->SetWorldLocation(
			FocusPoint
			+ FVector(Distance * 0.2f, Distance * 0.5f, Distance * 0.15f));
	}

	PortraitCaptureComponent->MaxViewDistanceOverride =
		FMath::Max(5000.0f, Distance * 4.0f);
	PortraitCaptureComponent->SetWorldLocationAndRotation(
		CameraLocation,
		(FocusPoint - CameraLocation).Rotation());
	return true;
}

void URTSActiveGroupWidget::CapturePortraitFrame()
{
	if (!UpdatePortraitPreview())
	{
		DestroyPortraitPreview();
		return;
	}

	PortraitCaptureComponent->CaptureScene();
}

void URTSActiveGroupWidget::ApplyStaticPortrait(UTexture2D* Texture)
{
	DestroyPortraitPreview();

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
		const bool bAlreadyShowingThisUnit = ActiveData->bIsMassEntity
			&& ActiveData->EntityHandle.IsSet()
			&& PortraitSourceEntity == ActiveData->EntityHandle;
		if (bEnableLivePortrait && ActiveData->bIsMassEntity && AvatarImage)
		{
			if (!bAlreadyShowingThisUnit)
			{
				StopLivePortrait();
				AvatarImage->SetVisibility(ESlateVisibility::Hidden);
				StartSelectedUnitPortrait(*ActiveData);
			}
			if (GroupIcon)
			{
				GroupIcon->SetVisibility(ESlateVisibility::Hidden);
			}
		}
		else
		{
			ApplyStaticPortrait(AvatarTexture);
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
