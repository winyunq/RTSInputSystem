// Copyright 2024 Winy unq All Rights Reserved.

#include "UI/RTSActiveGroupWidget.h"
#include "UI/RTSUnitIconWidget.h"
#include "RTSSelectionSubsystem.h" 
#include "RTSInputPanelSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SizeBox.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fragments/RenderBatchData.h"
#include "Fragments/Render.h"
#include "Fragments/Transform.h"
#include "MassAPISubsystem.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "Rendering/MassBattleAgentRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#endif

namespace
{
	const FVector PortraitStageLocation(0.0, 0.0, 100000.0);

	USoundConcurrency* GetCommandVoiceConcurrency(ULocalPlayer* LocalPlayer)
	{
		const FName Name(TEXT("RTSCommandVoiceConcurrency"));
		USoundConcurrency* Concurrency = FindObject<USoundConcurrency>(LocalPlayer, *Name.ToString());
		if (!Concurrency)
		{
			Concurrency = NewObject<USoundConcurrency>(LocalPlayer, Name, RF_Transient);
			Concurrency->Concurrency.MaxCount = 1;
			Concurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::PreventNew;
		}
		return Concurrency;
	}

	FString GetActiveGroupUnitGroupKey(const FRTSUnitData& Data)
	{
		return Data.GroupKey.IsEmpty() ? Data.Name : Data.GroupKey;
	}

	FBox ResolvePortraitBounds(const AMassBattleAgentRenderer* Renderer,
		const FTransform& Transform, FVector& ViewDirection)
	{
		const FBox MeshBounds = Renderer->AgentMesh->GetBoundingBox();
		UMassBattleATTDataAsset* AnimationData = Renderer->BoneAnimDataAsset.Get();
		const FMassBattleBoneStream* ExtraStream = Renderer->ExtraBoneStreams.FindByPredicate(
			[](const FMassBattleBoneStream& Stream) { return Stream.DataAsset || Stream.Skeleton; });
		if (!AnimationData && ExtraStream) AnimationData = ExtraStream->DataAsset.Get();
		USkeletalMesh* SkeletalMesh = AnimationData ? AnimationData->SkeletalMesh.LoadSynchronous() : nullptr;
		const FReferenceSkeleton* ReferenceSkeleton = SkeletalMesh ? &SkeletalMesh->GetRefSkeleton()
			: (ExtraStream && ExtraStream->Skeleton ? &ExtraStream->Skeleton->GetReferenceSkeleton() : nullptr);
		if (ReferenceSkeleton)
		{
			const FReferenceSkeleton& Skeleton = *ReferenceSkeleton;
			const TArray<FTransform>& Pose = Skeleton.GetRefBonePose();
			for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
			{
				const FString Name = Skeleton.GetBoneName(Index).ToString().ToLower();
				if (Name != TEXT("head") && !Name.EndsWith(TEXT("_head"))
					&& !Name.EndsWith(TEXT(":head")) && !Name.StartsWith(TEXT("head_"))) continue;
				FTransform Head = Pose[Index];
				for (int32 Parent = Skeleton.GetParentIndex(Index); Parent != INDEX_NONE;
					Parent = Skeleton.GetParentIndex(Parent)) Head *= Pose[Parent];
				const float Radius = FMath::Max(0.1f, FMath::Max(
					Pose[Index].GetTranslation().Size() * 1.25f,
					MeshBounds.GetExtent().Z * 0.16f));
				const FVector Center = Head.GetTranslation() + FVector(0, 0, Radius * 0.4f);
				const FVector Extent(Radius * 0.85f, Radius * 0.85f, Radius * 1.15f);
				return FBox(Center - Extent, Center + Extent);
			}
		}

		// Look along the longer horizontal axis so non-character models fill the portrait.
		const FVector ScaledExtent = MeshBounds.GetExtent() * Transform.GetScale3D().GetAbs();
		const FVector Axis = ScaledExtent.X >= ScaledExtent.Y
			? FVector::ForwardVector : FVector::RightVector;
		const float Pitch = ViewDirection.Z;
		ViewDirection = (Transform.TransformVectorNoScale(Axis).GetSafeNormal2D()
			* FMath::Sqrt(FMath::Max(0.0f, 1.0f - Pitch * Pitch))
			+ FVector(0, 0, Pitch)).GetSafeNormal();
		return MeshBounds;
	}

	float FitPortraitBounds(const FBox& Bounds, const FTransform& Transform, const FVector& ViewDirection,
		float HorizontalTan, float VerticalTan, float Padding)
	{
		const FRotationMatrix ViewRotation((-ViewDirection).Rotation());
		const FVector Right = ViewRotation.GetUnitAxis(EAxis::Y);
		const FVector Up = ViewRotation.GetUnitAxis(EAxis::Z);
		const FVector Extent = Bounds.GetExtent();
		float Distance = 1.0f;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Offset = Transform.TransformVector(FVector(Extent.X * ((Corner & 1) ? 1 : -1),
				Extent.Y * ((Corner & 2) ? 1 : -1), Extent.Z * ((Corner & 4) ? 1 : -1)));
			Distance = FMath::Max(Distance, static_cast<float>(FVector::DotProduct(Offset, ViewDirection)
				+ Padding * FMath::Max(FMath::Abs(FVector::DotProduct(Offset, Right)) / HorizontalTan,
					FMath::Abs(FVector::DotProduct(Offset, Up)) / VerticalTan)));
		}
		return Distance;
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
	PrepareLivePortraitResources();

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (URTSSelectionSubsystem* Subsystem = LP->GetSubsystem<URTSSelectionSubsystem>())
			{
				Subsystem->OnSelectionChanged.AddUniqueDynamic(this, &URTSActiveGroupWidget::OnSelectionUpdated);
				if (AvatarImage || GroupIcon)
				{
					CommandVoiceConcurrency = GetCommandVoiceConcurrency(LP);
					Subsystem->OnCommandFeedbackIssued.AddUObject(this, &URTSActiveGroupWidget::HandleCommandFeedback);
				}
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
				Subsystem->OnCommandFeedbackIssued.RemoveAll(this);
			}
		}
	}
	if (UWorld* PreparedWorld = PortraitPreparedWorld.Get())
	{
		PreparedWorld->RemoveOnActorSpawnedHandler(PortraitActorSpawnedHandle);
	}
	PortraitActorSpawnedHandle.Reset();
	PortraitPreparedWorld.Reset();
	DestroyPortraitPreview();
	if (PortraitCaptureComponent)
	{
		PortraitCaptureComponent->DestroyComponent();
		PortraitCaptureComponent = nullptr;
	}
	PortraitRenderTarget = nullptr;

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

bool URTSActiveGroupWidget::PrepareLivePortraitResources()
{
	UWorld* World = GetWorld();
	if (!bEnableLivePortrait || !AvatarImage || !World)
	{
		return false;
	}
	if (!PortraitActorSpawnedHandle.IsValid())
	{
		PortraitPreparedWorld = World;
		PortraitActorSpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(
				this, &URTSActiveGroupWidget::HandlePortraitActorSpawned));
	}
	if (!EnsurePortraitCaptureResources())
	{
		return false;
	}
	for (TActorIterator<AMassBattleAgentRenderer> It(World); It; ++It)
	{
		PreparePortraitPreviewComponent(*It);
	}
	return true;
}

void URTSActiveGroupWidget::HandlePortraitActorSpawned(AActor* Actor)
{
	if (AMassBattleAgentRenderer* Renderer = Cast<AMassBattleAgentRenderer>(Actor))
	{
		PreparePortraitPreviewComponent(Renderer);
	}
}

bool URTSActiveGroupWidget::StartSelectedUnitPortrait(const FRTSUnitData& Data)
{
	if (!bEnableLivePortrait || !AvatarImage || !PortraitCaptureComponent)
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
		StopLivePortrait();
		return false;
	}

	// A selection change may display the previous portrait until the next main view.
	// Use the engine's deferred capture queue; do not force World end-of-frame updates here.
	PortraitCaptureComponent->CaptureSceneDeferred();
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
	if (!PortraitPreviewActor)
	{
		// Controllers are hidden actors. Their primitive components inherit that
		// visibility even when a scene capture explicitly includes the component.
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PortraitPreviewActor = World->SpawnActor<AActor>(SpawnParameters);
		if (!PortraitPreviewActor)
		{
			return false;
		}
		PortraitPreviewActor->SetActorEnableCollision(false);
		USceneComponent* PreviewRoot = NewObject<USceneComponent>(
			PortraitPreviewActor,
			NAME_None,
			RF_Transient);
		PortraitPreviewActor->SetRootComponent(PreviewRoot);
		PreviewRoot->RegisterComponentWithWorld(World);
		PortraitPreviewActor->SetActorLocation(PortraitStageLocation);
	}

	if (!PortraitKeyLightComponent)
	{
		PortraitKeyLightComponent = NewObject<UPointLightComponent>(
			PortraitPreviewActor,
			NAME_None,
			RF_Transient);
		if (PortraitKeyLightComponent)
		{
			PortraitKeyLightComponent->SetupAttachment(PortraitPreviewActor->GetRootComponent());
			PortraitKeyLightComponent->SetMobility(EComponentMobility::Movable);
			PortraitKeyLightComponent->SetIntensity(6500.0f);
			PortraitKeyLightComponent->SetLightColor(
				FLinearColor(0.86f, 0.94f, 1.0f));
			PortraitKeyLightComponent->SetCastShadows(true);
			PortraitKeyLightComponent->SetVisibility(false);
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
			PortraitFillLightComponent->SetupAttachment(PortraitPreviewActor->GetRootComponent());
			PortraitFillLightComponent->SetMobility(EComponentMobility::Movable);
			PortraitFillLightComponent->SetIntensity(2200.0f);
			PortraitFillLightComponent->SetLightColor(
				FLinearColor(0.18f, 0.48f, 0.72f));
			PortraitFillLightComponent->SetCastShadows(false);
			PortraitFillLightComponent->SetVisibility(false);
			PortraitFillLightComponent->RegisterComponentWithWorld(GetWorld());
		}
	}
	return true;
}

void URTSActiveGroupWidget::PreparePortraitPreviewComponent(
	AMassBattleAgentRenderer* Renderer)
{
	if (!IsValid(Renderer)
		|| !IsValid(Renderer->NiagaraSystemAsset)
		|| !IsValid(Renderer->AgentMesh)
		|| !PortraitPreviewActor
		|| PortraitPreviewComponents.Contains(Renderer->NiagaraSystemAsset))
	{
		return;
	}
	UNiagaraComponent* Preview = NewObject<UNiagaraComponent>(
		PortraitPreviewActor, NAME_None, RF_Transient);
	Preview->SetupAttachment(PortraitPreviewActor->GetRootComponent());
	Preview->SetAsset(Renderer->NiagaraSystemAsset);
	Preview->SetForceSolo(true);
	Preview->SetAllowScalability(false);
	Preview->SetAutoActivate(false);
	Preview->SetAutoDestroy(false);
	Preview->SetCastShadow(true);
	Preview->SetVisibleInSceneCaptureOnly(true);
	Preview->SetVisibility(false);
	Preview->SetSystemFixedBounds(FBox(FVector(-50000.0), FVector(50000.0)));
	Preview->SetVariableStaticMesh(TEXT("AgentMesh"), Renderer->AgentMesh);
	Preview->RegisterComponentWithWorld(GetWorld());
	Preview->Activate(true);
	Preview->SetPaused(true);
	PortraitPreviewComponents.Add(Renderer->NiagaraSystemAsset, Preview);
}

bool URTSActiveGroupWidget::EnsurePortraitPreviewComponent(
	AMassBattleAgentRenderer* Renderer)
{
	if (!IsValid(Renderer) || !IsValid(Renderer->AgentMesh) || !PortraitCaptureComponent)
	{
		return false;
	}
	const TObjectPtr<UNiagaraComponent>* Prepared =
		PortraitPreviewComponents.Find(Renderer->NiagaraSystemAsset);
	if (!Prepared || !IsValid(Prepared->Get()))
	{
		return false;
	}
	if (PortraitPreviewComponent != Prepared->Get())
	{
		StopLivePortrait();
		PortraitPreviewComponent = Prepared->Get();
	}
	PortraitPreviewComponent->SetVariableStaticMesh(TEXT("AgentMesh"), Renderer->AgentMesh);
	PortraitPreviewComponent->SetVisibility(true);
	PortraitPreviewComponent->SetPaused(false);
	if (PortraitKeyLightComponent) PortraitKeyLightComponent->SetVisibility(true);
	if (PortraitFillLightComponent) PortraitFillLightComponent->SetVisibility(true);

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
	ClearPortraitSource();
	if (PortraitPreviewComponent)
	{
		PortraitPreviewComponent->SetPaused(true);
		PortraitPreviewComponent->SetVisibility(false);
		PortraitPreviewComponent = nullptr;
	}
	if (PortraitKeyLightComponent) PortraitKeyLightComponent->SetVisibility(false);
	if (PortraitFillLightComponent) PortraitFillLightComponent->SetVisibility(false);
	if (PortraitCaptureComponent)
	{
		PortraitCaptureComponent->ClearShowOnlyComponents();
	}
}

void URTSActiveGroupWidget::DestroyPortraitPreview()
{
	StopLivePortrait();
	for (auto& Pair : PortraitPreviewComponents)
	{
		if (Pair.Value) Pair.Value->DestroyComponent();
	}
	PortraitPreviewComponents.Reset();
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
		// 选中实体 → 渲染批次实例 → 独立头像预览 → 拍摄
		// 对只有 LOD0 的网格保留头像所用的 LOD 上限。
		const int32 RequestedLOD = Batch->CurrentLODArray.IsValidIndex(SourceIndex)
			? Batch->CurrentLODArray[SourceIndex]
			: 0;
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayInt32(
			PortraitPreviewComponent,
			FName("MeshIndex_Array"),
			TArray<int32>{FMath::Clamp(
				RequestedLOD, 0, FMath::Max(Renderer->AgentMesh->GetNumLODs() - 1, 0))});
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

	const float HalfFovRadians = FMath::DegreesToRadians(
		FMath::Clamp(PortraitFieldOfView, 10.0f, 90.0f) * 0.5f);
	const float HorizontalTan = FMath::Max(0.1f, FMath::Tan(HalfFovRadians));
	const float CaptureAspect = PortraitRenderTarget && PortraitRenderTarget->SizeY > 0
		? static_cast<float>(PortraitRenderTarget->SizeX)
			/ static_cast<float>(PortraitRenderTarget->SizeY)
		: FMath::Clamp(PortraitPanelWidth, 96.0f, 512.0f)
			/ FMath::Clamp(PortraitPanelHeight, 128.0f, 512.0f);
	const float VerticalTan = HorizontalTan / FMath::Max(0.1f, CaptureAspect);
	const FTransform PreviewTransform(PreviewRenderRotation, PreviewRenderLocation, PreviewScale);
	FVector ViewDirection = PortraitViewDirection.Vector().GetSafeNormal();
	const FBox FramingBounds = ResolvePortraitBounds(Renderer, PreviewTransform, ViewDirection);
	const FVector FocusPoint = PreviewTransform.TransformPosition(FramingBounds.GetCenter());
	const float Distance = FitPortraitBounds(FramingBounds, PreviewTransform, ViewDirection,
		HorizontalTan, VerticalTan, FMath::Clamp(PortraitFramingPadding, 1.0f, 1.08f));
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

#if WITH_DEV_AUTOMATION_TESTS




IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPortraitFramingTest, "Winyunq.Input.UnitPresentation.PortraitFraming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSPortraitFramingTest::RunTest(const FString& Parameters)
{
	const FVector Extents[] = {FVector(15, 14, 20), FVector(240, 65, 50), FVector(60, 60, 180)};
	for (const FVector& Extent : Extents)
	{
		const FBox Bounds(-Extent, Extent);
		const FTransform Transform(FRotator(0, 37, 0), FVector(0, 0, 100000), FVector(1.2, 0.8, 1.5));
		const FVector ViewDirection = FRotator(8, 90, 0).Vector();
		const FRotationMatrix Rotation((-ViewDirection).Rotation());
		const double HorizontalTan = FMath::Tan(FMath::DegreesToRadians(14.0));
		const double VerticalTan = HorizontalTan / (316.0 / 512.0);
		const float Distance = FitPortraitBounds(Bounds, Transform, ViewDirection, HorizontalTan, VerticalTan, 1.04f);
		double Occupancy = 0;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point = Transform.TransformVector(FVector(Extent.X * ((Corner & 1) ? 1 : -1),
				Extent.Y * ((Corner & 2) ? 1 : -1), Extent.Z * ((Corner & 4) ? 1 : -1)));
			const double Depth = Distance - FVector::DotProduct(Point, ViewDirection);
			TestTrue(TEXT("Model remains in front of the portrait camera"), Depth > 0);
			const double X = FMath::Abs(FVector::DotProduct(Point, Rotation.GetUnitAxis(EAxis::Y))) / (Depth * HorizontalTan);
			const double Y = FMath::Abs(FVector::DotProduct(Point, Rotation.GetUnitAxis(EAxis::Z))) / (Depth * VerticalTan);
			TestTrue(TEXT("Portrait contains every model corner"), X <= 1.0001 && Y <= 1.0001);
			Occupancy = FMath::Max(Occupancy, FMath::Max(X, Y));
		}
		TestTrue(TEXT("Portrait uses at least 90 percent of one frame dimension"), Occupancy >= 0.9);
	}
	return true;
}
#endif

void URTSActiveGroupWidget::CapturePortraitFrame()
{
	if (!UpdatePortraitPreview())
	{
		StopLivePortrait();
		return;
	}

	// Reuse the last image while the engine coalesces and submits this capture.
	// Deferred capture still shares the render thread; it is not a separate GPU queue.
	PortraitCaptureComponent->CaptureSceneDeferred();
}

void URTSActiveGroupWidget::ApplyStaticPortrait(UTexture2D* Texture)
{
	StopLivePortrait();

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

void URTSActiveGroupWidget::HandleCommandFeedback( const FRTSSelectionView& Context, FGameplayTag CommandTag, FVector, bool, bool)
{
	 const FRTSUnitData* Unit = Context.Mode == ERTSSelectionMode::Single ? &Context.SingleUnit : nullptr; if (!Unit) for (const FRTSUnitData& Item : Context. Items)
	  if (GetActiveGroupUnitGroupKey(Item) == Context.ActiveGroupKey) { Unit = &Item; break; }
	 if (!Unit && !Context.Items.IsEmpty()) Unit = &Context.Items[0];
	 if (!Unit || Unit-> Name.IsEmpty()) return;
	OnUnitCommandAcknowledged(CommandTag, *Unit);
	USoundBase* Sound = GetDefault<URTSInputPanelSettings>()->ResolveCommandVoice(
		Unit->AnnouncerId, CommandTag);
	if (!Sound) Sound = Unit->ConfirmationSound.LoadSynchronous();
	if (Sound)
		UGameplayStatics::PlaySound2D(this, Sound, 1.0f, 1.0f, 0.0f, CommandVoiceConcurrency);
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

	if (ActiveData && CommandVoiceConcurrency
		&& (CommandFeedbackUnit.EntityHandle != ActiveData->EntityHandle
			|| CommandFeedbackUnit.GroupKey != ActiveData->GroupKey
			|| CommandFeedbackUnit.Name != ActiveData->Name))
	{
		if (USoundBase* Sound = ActiveData->SelectionSound.LoadSynchronous())
			UGameplayStatics::PlaySound2D(this, Sound, 1.0f, 1.0f, 0.0f, CommandVoiceConcurrency);
	}
	CommandFeedbackUnit = ActiveData ? *ActiveData : FRTSUnitData();
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
