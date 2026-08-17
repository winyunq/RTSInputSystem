// Copyright 2024 Jesus Bracho All Rights Reserved.

#define RTS_CAMERA_CPP
#include "RTSCamera.h"
#include "RTSSelector.h"

// 定义 RTSCamera 专用日志分类
DEFINE_LOG_CATEGORY_STATIC(LogRTSCamera, Log, All);

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Runtime/CoreUObject/Public/UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

namespace
{
	constexpr float DefaultMapRegionSizeUU = 65536.0f;
	constexpr float MaxCameraInputDeltaSeconds = 1.0f / 30.0f;
	const TCHAR* MapRegionDirectoryName = TEXT("MapRegion");
	const TCHAR* MapRegionFileName = TEXT("MapRegion.ini");
	const TCHAR* MapRegionSectionName = TEXT("MapRegion");

	struct FRTSCameraBoundaryData
	{
		FVector Origin = FVector::ZeroVector;
		FVector Extents = FVector::ZeroVector;
		float MapOverflowUU = 0.0f;
	};

	FString GetCleanMapName(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("Default");
		}

		FString MapName = World->GetMapName();
		MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		if (MapName.IsEmpty())
		{
			return TEXT("Default");
		}
		return MapName;
	}

	FString GetMapRegionIniPath(const UWorld* World)
	{
		return FPaths::ProjectConfigDir() / MapRegionDirectoryName / GetCleanMapName(World) / MapRegionFileName;
	}

	void ApplyDefaultMapRegion(FRTSCameraBoundaryData& OutData)
	{
		OutData.Origin = FVector::ZeroVector;
		OutData.Extents = FVector(DefaultMapRegionSizeUU * 0.5f, DefaultMapRegionSizeUU * 0.5f, 1.0f);
		OutData.MapOverflowUU = 0.0f;
	}

	bool TryLoadSection(const FConfigFile& IniFile, const TCHAR* SectionName, FRTSCameraBoundaryData& OutData)
	{
		float OriginX = 0.0f;
		float OriginY = 0.0f;
		float SizeX = 0.0f;
		float SizeY = 0.0f;
		if (!IniFile.GetFloat(SectionName, TEXT("OriginX"), OriginX) ||
			!IniFile.GetFloat(SectionName, TEXT("OriginY"), OriginY) ||
			!IniFile.GetFloat(SectionName, TEXT("SizeX"), SizeX) ||
			!IniFile.GetFloat(SectionName, TEXT("SizeY"), SizeY))
		{
			return false;
		}

		float MapOverflowUU = 0.0f;
		IniFile.GetFloat(SectionName, TEXT("MapOverflowUU"), MapOverflowUU);
		OutData.Origin = FVector(OriginX + SizeX * 0.5f, OriginY + SizeY * 0.5f, 0.0f);
		OutData.Extents = FVector(SizeX * 0.5f, SizeY * 0.5f, 1.0f);
		OutData.MapOverflowUU = FMath::Max(0.0f, MapOverflowUU);
		return OutData.Extents.X > 0.0f && OutData.Extents.Y > 0.0f;
	}

	void LoadMapRegionIni(const UWorld* World, FRTSCameraBoundaryData& OutData, FString& OutIniPath)
	{
		FConfigFile IniFile;
		OutIniPath = GetMapRegionIniPath(World);
		if (!FPaths::FileExists(OutIniPath))
		{
			ApplyDefaultMapRegion(OutData);
			return;
		}
		IniFile.Read(OutIniPath);

		if (TryLoadSection(IniFile, MapRegionSectionName, OutData))
		{
			return;
		}

		ApplyDefaultMapRegion(OutData);
	}

	void RemoveCommandCardKeysFromCameraContext(UInputMappingContext* MappingContext)
	{
		if (!MappingContext)
		{
			return;
		}

		const FKey CommandCardKeys[] =
		{
			EKeys::Q, EKeys::W, EKeys::E, EKeys::R, EKeys::T,
			EKeys::A, EKeys::S, EKeys::D, EKeys::F, EKeys::G,
			EKeys::Z, EKeys::X, EKeys::C, EKeys::V, EKeys::B
		};
		TArray<TPair<const UInputAction*, FKey>> MappingsToRemove;

		for (const FEnhancedActionKeyMapping& Mapping : MappingContext->GetMappings())
		{
			for (const FKey& CommandCardKey : CommandCardKeys)
			{
				if (Mapping.Action && Mapping.Key == CommandCardKey)
				{
					MappingsToRemove.Emplace(Mapping.Action.Get(), Mapping.Key);
					break;
				}
			}
		}

		for (const TPair<const UInputAction*, FKey>& Mapping : MappingsToRemove)
		{
			MappingContext->UnmapKey(Mapping.Key, Mapping.Value);
		}
	}
}

URTSCamera::URTSCamera()
{
	/// 设置组件基本生存期属性
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	this->collisionChannel = ECC_WorldStatic;
	this->dragExtent = 0.6f;
	this->distanceFromEdgeThreshold = 0.1f;
	this->enableCameraLag = false;
	this->enableCameraRotationLag = false;
	this->enableDynamicCameraHeight = true;
	this->enableEdgeScrolling = true;
	this->findGroundTraceLength = 100000;
	this->maximumZoomLength = 5000;
	this->minimumZoomLength = 500;
	this->maxMovementSpeed = 1024.0f;
	this->minMovementSpeed = 128.0f;
	this->currentMovementSpeed = this->minMovementSpeed; 
	this->rotationSpeed = 45;
	this->startingPitchAngle = -45.0f;
	this->startingYawAngle = 0;
	this->zoomCatchupSpeed = 4;
	this->zoomSpeed = -200;
	this->minimumZoomBoundaryConstraint = 0.5f;
	this->boundaryTransitionZoneRatio = 0.15f;
	this->bEnableXBoundaryConstraint = true;
	this->bEnableYBoundaryConstraint = true;
	this->currentLateralSocketOffset = 0.0f;
	this->currentVerticalSocketOffset = 0.0f;

	/// 载入并关联输入资产
	static ConstructorHelpers::FObjectFinder<UInputAction> xMoveActionFinder(TEXT("/RTSInputSystem/Inputs/MoveCameraXAxis"));
	static ConstructorHelpers::FObjectFinder<UInputAction> yMoveActionFinder(TEXT("/RTSInputSystem/Inputs/MoveCameraYAxis"));
	static ConstructorHelpers::FObjectFinder<UInputAction> rotateActionFinder(TEXT("/RTSInputSystem/Inputs/RotateCameraAxis"));
	static ConstructorHelpers::FObjectFinder<UInputAction> leftTurnActionFinder(TEXT("/RTSInputSystem/Inputs/TurnCameraLeft"));
	static ConstructorHelpers::FObjectFinder<UInputAction> rightTurnActionFinder(TEXT("/RTSInputSystem/Inputs/TurnCameraRight"));
	static ConstructorHelpers::FObjectFinder<UInputAction> zoomActionFinder(TEXT("/RTSInputSystem/Inputs/ZoomCamera"));
	static ConstructorHelpers::FObjectFinder<UInputAction> dragActionFinder(TEXT("/RTSInputSystem/Inputs/DragCamera"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> contextFinder(TEXT("/RTSInputSystem/Inputs/RTSInputSystemInputs"));

	this->moveCameraXAxisAction = xMoveActionFinder.Object;
	this->moveCameraYAxisAction = yMoveActionFinder.Object;
	this->rotateCameraAxisAction = rotateActionFinder.Object;
	this->turnCameraLeftAction = leftTurnActionFinder.Object;
	this->turnCameraRightAction = rightTurnActionFinder.Object;
	this->dragCameraAction = dragActionFinder.Object;
	this->zoomCameraAction = zoomActionFinder.Object;
	this->inputMappingContext = contextFinder.Object;
}

void URTSCamera::BeginPlay()
{
	Super::BeginPlay();
	// Blueprint component templates created before the camera Tick was restored
	// may still carry a disabled tick flag. Runtime camera motion always needs it.
	this->PrimaryComponentTick.bCanEverTick = true;
	this->SetComponentTickEnabled(true);

	const auto netMode = this->GetNetMode();
	if (netMode != NM_DedicatedServer)
	{
		/// 初始化依赖架构，建立输入映射链条
		this->resolveComponentDependencyPointers();
		this->setupInitialSpringArmState();
		this->initializeMovementBoundsFromMapRegion();
		this->configureInputModeForEdgeScrolling();
		this->validateEnhancedInputAvailability();
		RemoveCommandCardKeysFromCameraContext(this->inputMappingContext);
		this->registerInputMappingContext();
		this->bindActionCallbacks();
		FViewport::ViewportResizedEvent.AddUObject(this, &URTSCamera::handleViewportResized);
	}
}

void URTSCamera::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	this->SetComponentTickEnabled(false);
	this->pendingMoveXAxis = 0.0f;
	this->pendingMoveYAxis = 0.0f;
	this->unFollowTarget();
	FViewport::ViewportResizedEvent.RemoveAll(this);

	if (this->realTimeStrategyPlayerController)
	{
		if (UEnhancedInputComponent* EnhancedInputComponent =
			Cast<UEnhancedInputComponent>(this->realTimeStrategyPlayerController->InputComponent))
		{
			EnhancedInputComponent->ClearBindingsForObject(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void URTSCamera::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!this->rootComponent ||
		!this->realTimeStrategyPlayerController ||
		this->realTimeStrategyPlayerController->GetViewTarget() != this->cameraOwner)
	{
		return;
	}

	const float cameraDeltaSeconds =
		this->getClampedInputDeltaSeconds(DeltaTime);
	if (!this->isDragging &&
		(!FMath::IsNearlyZero(this->pendingMoveXAxis) ||
			!FMath::IsNearlyZero(this->pendingMoveYAxis)))
	{
		const FVector worldMovement =
			this->rootComponent->GetRightVector() * this->pendingMoveXAxis +
			this->rootComponent->GetForwardVector() * this->pendingMoveYAxis;
		const FVector2D horizontalMovement(
			worldMovement.X,
			worldMovement.Y);
		this->requestCameraMovement(
			horizontalMovement.X,
			horizontalMovement.Y,
			horizontalMovement.Size(),
			cameraDeltaSeconds);
	}

	if (this->springArmComponent &&
		!FMath::IsNearlyEqual(
			this->springArmComponent->TargetArmLength,
			this->desiredZoomLength,
			0.1f))
	{
		this->springArmComponent->TargetArmLength = FMath::FInterpTo(
			this->springArmComponent->TargetArmLength,
			this->desiredZoomLength,
			cameraDeltaSeconds,
			this->zoomCatchupSpeed);
		this->applyCameraStateChange();
	}

	if (!this->isDragging)
	{
		this->executeEdgeScrollingEvaluation(
			UWidgetLayoutLibrary::GetMousePositionOnViewport(this->GetWorld()),
			cameraDeltaSeconds);
	}

	FVector2D pointerPosition = FVector2D::ZeroVector;
	if (!this->realTimeStrategyPlayerController->GetMousePosition(
		pointerPosition.X,
		pointerPosition.Y))
	{
		return;
	}

	// Units and the camera can move beneath a stationary pointer. Keep this
	// frontend-only refresh independent from simulation progress notifications.
	this->refreshPointerWorldState(pointerPosition);
}

void URTSCamera::followTarget(AActor* target)
{
	this->unFollowTarget();
	if (!IsValid(target) || target == this->cameraOwner)
	{
		return;
	}

	this->activeCameraFollowTarget = target;
	if (USceneComponent* TargetRootComponent = target->GetRootComponent())
	{
		this->activeCameraFollowRootComponent = TargetRootComponent;
		TargetRootComponent->TransformUpdated.AddUObject(
			this,
			&URTSCamera::handleFollowTargetTransformUpdated);
	}

	this->jumpTo(target->GetActorLocation());
}

void URTSCamera::unFollowTarget()
{
	if (USceneComponent* TargetRootComponent = this->activeCameraFollowRootComponent.Get())
	{
		TargetRootComponent->TransformUpdated.RemoveAll(this);
	}

	this->activeCameraFollowRootComponent.Reset();
	this->activeCameraFollowTarget.Reset();
}

void URTSCamera::onZoomCameraActionTriggered(const FInputActionValue& value)
{
	/// 更新意图缩放距离并通过该参数联动调整移动阻尼感
	this->desiredZoomLength = FMath::Clamp(
		this->desiredZoomLength + value.Get<float>() * this->zoomSpeed,
		this->minimumZoomLength,
		this->maximumZoomLength
	);

	const float zoomRange = FMath::Max(this->maximumZoomLength - this->minimumZoomLength, UE_SMALL_NUMBER);
	const float speedAlpha = (this->desiredZoomLength - this->minimumZoomLength) / zoomRange;
	this->currentMovementSpeed = FMath::Lerp(this->minMovementSpeed, this->maxMovementSpeed, speedAlpha);

}

void URTSCamera::onRotateCameraActionTriggered(const FInputActionValue& value)
{
	/// 处理水平偏航角输入并更新世界变换
	const auto actorRotation = this->rootComponent->GetComponentRotation();
	this->rootComponent->SetWorldRotation(
		FRotator::MakeFromEuler(
			FVector(
				actorRotation.Euler().X,
				actorRotation.Euler().Y,
				actorRotation.Euler().Z + value.Get<float>()
			)
		)
	);
	this->applyCameraStateChange();
}

void URTSCamera::onTurnCameraLeftActionTriggered(const FInputActionValue&)
{
	/// 向左执行定量的步进式偏转
	const FRotator relativeRotation = this->rootComponent->GetRelativeRotation();
	this->rootComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				relativeRotation.Euler().X,
				relativeRotation.Euler().Y,
				relativeRotation.Euler().Z - this->rotationSpeed
			)
		)
	);
	this->applyCameraStateChange();
}

void URTSCamera::onTurnCameraRightActionTriggered(const FInputActionValue&)
{
	/// 向右执行定量的步进式偏转
	const FRotator relativeRotation = this->rootComponent->GetRelativeRotation();
	this->rootComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				relativeRotation.Euler().X,
				relativeRotation.Euler().Y,
				relativeRotation.Euler().Z + this->rotationSpeed
			)
		)
	);
	this->applyCameraStateChange();
}

void URTSCamera::onMoveCameraYAxisActionTriggered(const FInputActionValue& value)
{
	/// 只记录纵向意图，位移由相机 Tick 统一积分。
	this->pendingMoveYAxis = value.Get<float>();
}

void URTSCamera::onMoveCameraYAxisActionCompleted(const FInputActionValue&)
{
	this->pendingMoveYAxis = 0.0f;
}

void URTSCamera::onMoveCameraXAxisActionTriggered(const FInputActionValue& value)
{
	/// 只记录横向意图，位移由相机 Tick 统一积分。
	this->pendingMoveXAxis = value.Get<float>();
}

void URTSCamera::onMoveCameraXAxisActionCompleted(const FInputActionValue&)
{
	this->pendingMoveXAxis = 0.0f;
}

void URTSCamera::onDragCameraActionStarted(const FInputActionValue&)
{
	this->isDragging = this->realTimeStrategyPlayerController &&
		this->realTimeStrategyPlayerController->GetMousePosition(
			this->dragInteractionInitialLocation.X,
			this->dragInteractionInitialLocation.Y);
}

void URTSCamera::onDragCameraActionCompleted(const FInputActionValue&)
{
	this->isDragging = false;
	FVector2D pointerPosition = FVector2D::ZeroVector;
	if (this->realTimeStrategyPlayerController &&
		this->realTimeStrategyPlayerController->GetMousePosition(
			pointerPosition.X,
			pointerPosition.Y))
	{
		this->HandlePointerMoved(pointerPosition);
	}
}

void URTSCamera::requestCameraMovement(
	const float xAxisValue,
	const float yAxisValue,
	const float movementScale,
	const float DeltaSeconds)
{
	if (!this->rootComponent || FMath::IsNearlyZero(movementScale))
	{
		return;
	}

	FVector2D directionVector(xAxisValue, yAxisValue);
	if (!directionVector.Normalize())
	{
		return;
	}

	directionVector *= this->currentMovementSpeed * movementScale * DeltaSeconds;
	const FVector currentLocation = this->rootComponent->GetComponentLocation();
	this->rootComponent->SetWorldLocation(
		FVector(currentLocation.X + directionVector.X, currentLocation.Y + directionVector.Y, currentLocation.Z));
	this->applyCameraStateChange();
}

void URTSCamera::resolveComponentDependencyPointers()
{
	/// 获取所有关键组件的指针，作为该插件架构的基石
	this->cameraOwner = this->GetOwner();
	this->rootComponent = this->cameraOwner->GetRootComponent();
	this->cameraComponent = Cast<UCameraComponent>(this->cameraOwner->GetComponentByClass(UCameraComponent::StaticClass()));
	this->springArmComponent = Cast<USpringArmComponent>(this->cameraOwner->GetComponentByClass(USpringArmComponent::StaticClass()));
	this->realTimeStrategyPlayerController = UGameplayStatics::GetPlayerController(this->GetWorld(), 0);
}

void URTSCamera::setupInitialSpringArmState()
{
	/// 配置弹簧臂的物理约束与渲染初始位姿
	this->desiredZoomLength = this->minimumZoomLength;
	this->springArmComponent->TargetArmLength = this->desiredZoomLength;
	this->springArmComponent->bDoCollisionTest = false;
	this->springArmComponent->bEnableCameraLag = false;
	this->springArmComponent->bEnableCameraRotationLag = false;
	this->springArmComponent->SetRelativeRotation(
		FRotator::MakeFromEuler(
			FVector(
				0.0,
				this->startingPitchAngle,
				this->startingYawAngle
			)
		)
	);
	
	if (this->cameraComponent != nullptr)
	{
		// 关键：回归绑定模式以消除缩放抖动，且将相对旋转清零以解决 -90 度视角叠加 Bug。
		this->cameraComponent->bUsePawnControlRotation = false;
		this->cameraComponent->SetUsingAbsoluteRotation(false);
		
		// 视角初始化诊断：验证当前俯仰角设定，此时相机将纯粹继承弹簧臂的俯仰。
		UE_LOG(LogRTSCamera, Warning, TEXT("视角初始化诊断 (V9 零叠加版): 起始俯仰角=%.2f, 起始偏航角=%.2f"), this->startingPitchAngle, this->startingYawAngle);

		// 修复 -90 度叠加：相机作为子组件不需要再次设置 -45 度相对俯角。
		this->cameraComponent->SetRelativeRotation(FRotator::ZeroRotator);
		this->cameraComponent->FieldOfView = 45.0f;
	}
}

bool URTSCamera::getResolvedMovementBounds(FVector& OutOrigin, FVector& OutExtents) const
{
	if (!this->bHasResolvedBoundaryData ||
		this->ResolvedBoundaryExtents.X <= 0.0f ||
		this->ResolvedBoundaryExtents.Y <= 0.0f)
	{
		return false;
	}

	OutOrigin = this->ResolvedBoundaryOrigin;
	OutExtents = this->ResolvedBoundaryExtents;
	return true;
}

bool URTSCamera::initializeMovementBoundsFromMapRegion()
{
	UWorld* World = this->GetWorld();
	if (!World)
	{
		return false;
	}

	FRTSCameraBoundaryData RegionBoundaryData;
	FString MapRegionIniPath;
	LoadMapRegionIni(World, RegionBoundaryData, MapRegionIniPath);

	this->bHasResolvedBoundaryData = true;
	this->ResolvedBoundaryOrigin = RegionBoundaryData.Origin;
	this->ResolvedBoundaryExtents = RegionBoundaryData.Extents;
	this->ResolvedBoundaryOverflowUU = RegionBoundaryData.MapOverflowUU;

	const FVector logicalExtent = RegionBoundaryData.Extents;
	FVector2D viewportSize = FVector2D::ZeroVector;
	this->getViewportSizePixels(viewportSize);
	this->recalculateBoundaryReachFactors(viewportSize);

	UE_LOG(LogRTSCamera, Log, TEXT("RTSCamera 初始化: 边界源 [%s], 逻辑边界: %.1f x %.1f, 溢出保护: %.1f"),
		*MapRegionIniPath, logicalExtent.X * 2.0f, logicalExtent.Y * 2.0f, RegionBoundaryData.MapOverflowUU);
	this->applyBoundaryConstraints();
	this->updateMinimapFrustum();
	return true;
}

void URTSCamera::configureInputModeForEdgeScrolling()
{
	/// 当鼠标用于边缘滚动时，强制应用视口锁定策略
	if (this->enableEdgeScrolling)
	{
		FInputModeGameAndUI gameModeSettings;
		gameModeSettings.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		gameModeSettings.SetHideCursorDuringCapture(false);
		this->realTimeStrategyPlayerController->SetInputMode(gameModeSettings);
	}
}

void URTSCamera::validateEnhancedInputAvailability()
{
	/// 校验当前的 InputComponent 是否为 Enhanced Input 组件
	if (Cast<UEnhancedInputComponent>(this->realTimeStrategyPlayerController->InputComponent) == nullptr)
	{
		UKismetSystemLibrary::PrintString(
			this->GetWorld(),
			TEXT("Warning: RTSCamera requires Enhanced Input Component! Check Project Settings."), true, true,
			FLinearColor::Red,
			100
		);
	}
}

void URTSCamera::registerInputMappingContext()
{
	/// 向玩家输入子系统注册相机专用的映射上下文
	if (this->realTimeStrategyPlayerController && this->realTimeStrategyPlayerController->GetLocalPlayer())
	{
		if (const auto inputSystem = this->realTimeStrategyPlayerController->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			this->realTimeStrategyPlayerController->bShowMouseCursor = true;

			if (!inputSystem->HasMappingContext(this->inputMappingContext))
			{
				inputSystem->AddMappingContext(this->inputMappingContext, 0);
			}
		}
	}
}

void URTSCamera::bindActionCallbacks()
{
	/// 执行运动指令与 C++ 响应函数的逻辑挂挂接
	if (const auto enhancedInputComponent = Cast<UEnhancedInputComponent>(this->realTimeStrategyPlayerController->InputComponent))
	{
		enhancedInputComponent->BindAction(this->zoomCameraAction, ETriggerEvent::Triggered, this, &URTSCamera::onZoomCameraActionTriggered);
		enhancedInputComponent->BindAction(this->rotateCameraAxisAction, ETriggerEvent::Triggered, this, &URTSCamera::onRotateCameraActionTriggered);
		enhancedInputComponent->BindAction(this->turnCameraLeftAction, ETriggerEvent::Started, this, &URTSCamera::onTurnCameraLeftActionTriggered);
		enhancedInputComponent->BindAction(this->turnCameraRightAction, ETriggerEvent::Started, this, &URTSCamera::onTurnCameraRightActionTriggered);
		enhancedInputComponent->BindAction(this->moveCameraXAxisAction, ETriggerEvent::Triggered, this, &URTSCamera::onMoveCameraXAxisActionTriggered);
		enhancedInputComponent->BindAction(this->moveCameraXAxisAction, ETriggerEvent::Completed, this, &URTSCamera::onMoveCameraXAxisActionCompleted);
		enhancedInputComponent->BindAction(this->moveCameraXAxisAction, ETriggerEvent::Canceled, this, &URTSCamera::onMoveCameraXAxisActionCompleted);
		enhancedInputComponent->BindAction(this->moveCameraYAxisAction, ETriggerEvent::Triggered, this, &URTSCamera::onMoveCameraYAxisActionTriggered);
		enhancedInputComponent->BindAction(this->moveCameraYAxisAction, ETriggerEvent::Completed, this, &URTSCamera::onMoveCameraYAxisActionCompleted);
		enhancedInputComponent->BindAction(this->moveCameraYAxisAction, ETriggerEvent::Canceled, this, &URTSCamera::onMoveCameraYAxisActionCompleted);
		enhancedInputComponent->BindAction(this->dragCameraAction, ETriggerEvent::Started, this, &URTSCamera::onDragCameraActionStarted);
		enhancedInputComponent->BindAction(this->dragCameraAction, ETriggerEvent::Completed, this, &URTSCamera::onDragCameraActionCompleted);
		enhancedInputComponent->BindAction(this->dragCameraAction, ETriggerEvent::Canceled, this, &URTSCamera::onDragCameraActionCompleted);
	}
}

void URTSCamera::setActiveCamera()
{
	/// 将玩家当前的渲染视角强制聚焦于此组件
	this->realTimeStrategyPlayerController->SetViewTarget(this->GetOwner());
	this->SetComponentTickEnabled(true);
}

void URTSCamera::jumpTo(const FVector position)
{
	/// 执行瞬时的视变换同步，并触发视野投影点手动刷新
	float cachedZ = this->rootComponent->GetComponentLocation().Z;
	this->rootComponent->SetWorldLocation(FVector(position.X, position.Y, cachedZ));
	this->applyCameraStateChange();
}

void URTSCamera::HandlePointerMoved(const FVector2D& ViewportPosition)
{
	if (!this->rootComponent || !this->realTimeStrategyPlayerController ||
		this->realTimeStrategyPlayerController->GetViewTarget() != this->cameraOwner)
	{
		return;
	}

	if (this->isDragging)
	{
		this->SetComponentTickEnabled(true);
		FVector2D viewportSizeExtent = FVector2D::ZeroVector;
		if (!this->getViewportSizePixels(viewportSizeExtent))
		{
			return;
		}
		viewportSizeExtent *= FMath::Max(this->dragExtent, UE_SMALL_NUMBER);
		if (viewportSizeExtent.X <= UE_SMALL_NUMBER || viewportSizeExtent.Y <= UE_SMALL_NUMBER)
		{
			return;
		}

		FVector2D dragDelta = ViewportPosition - this->dragInteractionInitialLocation;
		dragDelta.X = FMath::Clamp(dragDelta.X / viewportSizeExtent.X, -1.0f, 1.0f);
		dragDelta.Y = FMath::Clamp(dragDelta.Y / viewportSizeExtent.Y, -1.0f, 1.0f);

		const FVector worldMovement =
			this->rootComponent->GetRightVector() * dragDelta.X -
			this->rootComponent->GetForwardVector() * dragDelta.Y;
		const FVector2D horizontalMovement(worldMovement.X, worldMovement.Y);
		this->requestCameraMovement(
			horizontalMovement.X,
			horizontalMovement.Y,
			horizontalMovement.Size(),
			this->getClampedInputDeltaSeconds(FApp::GetDeltaTime()));
		return;
	}

	this->SetComponentTickEnabled(true);
}

bool URTSCamera::executeEdgeScrollingEvaluation(
	const FVector2D& ViewportPosition,
	const float DeltaSeconds)
{
	if (!this->enableEdgeScrolling)
	{
		return false;
	}

	// Keep pointer and extent in the same Slate-local coordinate space. Unlike
	// APlayerController::GetMousePosition, this remains valid at and just beyond
	// a PIE viewport edge when another system configures DoNotLock.
	const FVector2D viewportSize =
		UWidgetLayoutLibrary::GetViewportWidgetGeometry(this->GetWorld())
		.GetLocalSize();
	const float thresholdRatio = FMath::Clamp(this->distanceFromEdgeThreshold, 0.0f, 0.5f);
	const float horizontalThreshold = viewportSize.X * thresholdRatio;
	const float verticalThreshold = viewportSize.Y * thresholdRatio;
	if (horizontalThreshold <= UE_SMALL_NUMBER || verticalThreshold <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float leftAlpha = FMath::Clamp(1.0f - ViewportPosition.X / horizontalThreshold, 0.0f, 1.0f);
	const float rightAlpha = FMath::Clamp(
		(ViewportPosition.X - (viewportSize.X - horizontalThreshold)) / horizontalThreshold,
		0.0f,
		1.0f);
	const float upAlpha = FMath::Clamp(1.0f - ViewportPosition.Y / verticalThreshold, 0.0f, 1.0f);
	const float downAlpha = FMath::Clamp(
		(ViewportPosition.Y - (viewportSize.Y - verticalThreshold)) / verticalThreshold,
		0.0f,
		1.0f);

	const FVector worldMovement =
		this->rootComponent->GetRightVector() * (rightAlpha - leftAlpha) +
		this->rootComponent->GetForwardVector() * (upAlpha - downAlpha);
	const FVector2D horizontalMovement(worldMovement.X, worldMovement.Y);
	if (horizontalMovement.SizeSquared() <= UE_SMALL_NUMBER)
	{
		return false;
	}
	this->requestCameraMovement(
		horizontalMovement.X,
		horizontalMovement.Y,
		horizontalMovement.Size(),
		DeltaSeconds);
	return true;
}

void URTSCamera::refreshPointerWorldState(
	const FVector2D& ViewportPosition)
{
	if (!this->realTimeStrategyPlayerController)
	{
		return;
	}

	if (URTSSelector* Selector =
		this->realTimeStrategyPlayerController->FindComponentByClass<URTSSelector>())
	{
		Selector->RefreshPointerWorldState(ViewportPosition);
	}
}

void URTSCamera::handleFollowTargetTransformUpdated(
	USceneComponent*,
	EUpdateTransformFlags,
	ETeleportType)
{
	if (AActor* FollowTarget = this->activeCameraFollowTarget.Get())
	{
		this->jumpTo(FollowTarget->GetActorLocation());
	}
}

void URTSCamera::handleViewportResized(FViewport* Viewport, uint32)
{
	if (!Viewport || !this->realTimeStrategyPlayerController)
	{
		return;
	}

	const ULocalPlayer* LocalPlayer = this->realTimeStrategyPlayerController->GetLocalPlayer();
	const UGameViewportClient* ViewportClient = LocalPlayer ? LocalPlayer->ViewportClient.Get() : nullptr;
	if (!ViewportClient || ViewportClient->Viewport != Viewport)
	{
		return;
	}

	const FIntPoint viewportSize = Viewport->GetSizeXY();
	this->recalculateBoundaryReachFactors(FVector2D(viewportSize.X, viewportSize.Y));
	this->applyCameraStateChange();
}

void URTSCamera::recalculateBoundaryReachFactors(const FVector2D& ViewportSize)
{
	if (!this->cameraComponent || this->maximumZoomLength <= UE_SMALL_NUMBER)
	{
		return;
	}

	const float pitchAngleInRadians = FMath::DegreesToRadians(FMath::Abs(this->startingPitchAngle));
	const float horizontalFieldOfViewHalf = FMath::DegreesToRadians(this->cameraComponent->FieldOfView) / 2.0f;
	const float viewportAspectRatioValue =
		(ViewportSize.Y > UE_SMALL_NUMBER) ? (ViewportSize.X / ViewportSize.Y) : this->cameraComponent->AspectRatio;
	const float verticalFieldOfViewHalf = FMath::Atan(FMath::Tan(horizontalFieldOfViewHalf) / viewportAspectRatioValue);

	const auto calculateReachForLength = [&](const float length)
	{
		const float z = length * FMath::Sin(pitchAngleInRadians);
		const float slant = z / FMath::Sin(pitchAngleInRadians + verticalFieldOfViewHalf);
		const float lateralReach = slant * FMath::Tan(horizontalFieldOfViewHalf);
		const float forwardReach = slant * FMath::Cos(pitchAngleInRadians + verticalFieldOfViewHalf);
		return FVector(z, lateralReach, forwardReach);
	};

	const FVector maxPhysics = calculateReachForLength(this->maximumZoomLength);
	const float slantBottom = maxPhysics.X / FMath::Sin(pitchAngleInRadians - verticalFieldOfViewHalf);
	const float backwardReach = slantBottom * FMath::Cos(pitchAngleInRadians - verticalFieldOfViewHalf);

	this->lateralReachFactor = maxPhysics.Y / this->maximumZoomLength;
	this->forwardReachFactor = maxPhysics.Z / this->maximumZoomLength;
	this->backwardReachFactor = backwardReach / this->maximumZoomLength;
}

void URTSCamera::applyCameraStateChange()
{
	this->applyBoundaryConstraints();
	this->updateMinimapFrustum();
}

bool URTSCamera::getViewportSizePixels(FVector2D& OutViewportSize) const
{
	OutViewportSize = FVector2D::ZeroVector;
	const UWorld* World = this->GetWorld();
	UGameViewportClient* ViewportClient = World ? World->GetGameViewport() : nullptr;
	if (!ViewportClient)
	{
		return false;
	}

	ViewportClient->GetViewportSize(OutViewportSize);
	return OutViewportSize.X > UE_SMALL_NUMBER && OutViewportSize.Y > UE_SMALL_NUMBER;
}

float URTSCamera::getClampedInputDeltaSeconds(
	const float DeltaSeconds) const
{
	return FMath::Clamp(
		DeltaSeconds,
		0.0f,
		MaxCameraInputDeltaSeconds);
}

void URTSCamera::rectifyRootHeightFromTerrain()
{
	/// 射线补偿检测：使根节点坐标实时贴合地形海拔
	if (this->enableDynamicCameraHeight)
	{
		const FVector currentRootXYZ = this->rootComponent->GetComponentLocation();
		const TArray<AActor*> excludedActors;

		FHitResult floorHit;
		const bool bValidFloor = UKismetSystemLibrary::LineTraceSingle(
			this->GetWorld(),
			FVector(currentRootXYZ.X, currentRootXYZ.Y, currentRootXYZ.Z + this->findGroundTraceLength),
			FVector(currentRootXYZ.X, currentRootXYZ.Y, currentRootXYZ.Z - this->findGroundTraceLength),
			UEngineTypes::ConvertToTraceType(this->collisionChannel),
			true,
			excludedActors,
			EDrawDebugTrace::None,
			floorHit,
			true
		);

		if (bValidFloor)
		{
			this->rootComponent->SetWorldLocation(FVector(floorHit.Location.X, floorHit.Location.Y, floorHit.Location.Z));
		}
	}
}


void URTSCamera::updateMinimapFrustum()
{
	/// 战略視野投影核心逻辑：直接基于相机的世界位姿计算四个角点。
	if (this->cameraComponent == nullptr || this->rootComponent == nullptr) 
	{
		return;
	}
	
	// 核心：强制触发 SpringArm 及其子组件的世界变换计算
	// 否则在同一帧内，GetComponentLocation 拿到的可能是修改 SocketOffset 之前的旧位置
	// 但是我不认可这种逻辑，因为对小地图来说这不重要，我觉得是无稽之谈
	// if (this->springArmComponent)
	// {
	// 	this->springArmComponent->UpdateComponentToWorld();
	// }
	// this->cameraComponent->UpdateComponentToWorld();

	const FVector cameraLocation = this->cameraComponent->GetComponentLocation();
	const FRotator cameraRotation = this->cameraComponent->GetComponentRotation();
	
	const float fieldOfViewValue = this->cameraComponent->FieldOfView;
	
	// 拒绝假设：动态获取视口尺寸计算长宽比
	FVector2D viewportSize = FVector2D::ZeroVector;
	this->getViewportSizePixels(viewportSize);
	float aspectRatioValue = (viewportSize.Y > 0.0f) ? (viewportSize.X / viewportSize.Y) : this->cameraComponent->AspectRatio;
	if (this->cameraComponent->bConstrainAspectRatio)
	{
		aspectRatioValue = this->cameraComponent->AspectRatio;
	}

	const float horizontalFieldOfView = FMath::DegreesToRadians(fieldOfViewValue) / 2.0f;
	const float verticalFieldOfView = FMath::Atan(FMath::Tan(horizontalFieldOfView) / aspectRatioValue);

	const float tangentHorizontal = FMath::Tan(horizontalFieldOfView);
	const float tangentVertical = FMath::Tan(verticalFieldOfView);

	const FVector forwardVector = cameraRotation.Vector();
	const FVector rightVector = FRotationMatrix(cameraRotation).GetScaledAxis(EAxis::Y);
	const FVector upVector = FRotationMatrix(cameraRotation).GetScaledAxis(EAxis::Z);

	/// 计算四个边界射线
	const FVector topRightDirection = (forwardVector + rightVector * tangentHorizontal + upVector * tangentVertical).GetSafeNormal();
	const FVector topLeftDirection = (forwardVector - rightVector * tangentHorizontal + upVector * tangentVertical).GetSafeNormal();
	const FVector bottomRightDirection = (forwardVector + rightVector * tangentHorizontal - upVector * tangentVertical).GetSafeNormal();
	const FVector bottomLeftDirection = (forwardVector - rightVector * tangentHorizontal - upVector * tangentVertical).GetSafeNormal();

	const float groundAltitude = this->rootComponent->GetComponentLocation().Z;
	
	auto calculateIntersection = [&](const FVector& rayOrigin, const FVector& rayDirection) -> FVector
	{
		if (rayDirection.Z >= -0.001f) 
		{
			return rayOrigin + rayDirection * 100000.0f;
		}

		const float timeToIntersection = (groundAltitude - rayOrigin.Z) / rayDirection.Z;
		if (timeToIntersection < 0.0f) 
		{
			return rayOrigin + rayDirection * 100000.0f;
		}

		return rayOrigin + rayDirection * timeToIntersection;
	};

	/// 填充战略投影点数组
	this->minimapFrustumPoints[0] = calculateIntersection(cameraLocation, topLeftDirection);
	this->minimapFrustumPoints[1] = calculateIntersection(cameraLocation, topRightDirection);
	this->minimapFrustumPoints[2] = calculateIntersection(cameraLocation, bottomRightDirection);
	this->minimapFrustumPoints[3] = calculateIntersection(cameraLocation, bottomLeftDirection);

	/// 计算完成后发起视野更新广播
	this->onMinimapFrustumUpdated.Broadcast();
}

void URTSCamera::applyBoundaryConstraints()
{
	if (!this->bHasResolvedBoundaryData || this->springArmComponent == nullptr)
	{
		return;
	}

	// Root 物理坐标先锁定到有效地图范围内，后续高度和镜头偏移都必须基于夹取后的坐标。
	FVector clampedLocation = this->rootComponent->GetComponentLocation();
	clampedLocation.X = FMath::Clamp(
		clampedLocation.X,
		this->ResolvedBoundaryOrigin.X - this->ResolvedBoundaryExtents.X,
		this->ResolvedBoundaryOrigin.X + this->ResolvedBoundaryExtents.X);
	clampedLocation.Y = FMath::Clamp(
		clampedLocation.Y,
		this->ResolvedBoundaryOrigin.Y - this->ResolvedBoundaryExtents.Y,
		this->ResolvedBoundaryOrigin.Y + this->ResolvedBoundaryExtents.Y);
	this->rootComponent->SetWorldLocation(clampedLocation);

	// 地形高度同步必须在 XY 夹取后执行，避免越界位置参与地形射线。
	this->rectifyRootHeightFromTerrain();

	// 计算并应用偏移
	const FVector currentPos = this->rootComponent->GetComponentLocation();
	this->currentLateralSocketOffset = this->calculateYOffset(currentPos.Y);
	this->currentVerticalSocketOffset = this->calculateXOffset(currentPos.X);

	this->springArmComponent->SocketOffset = FVector(this->currentVerticalSocketOffset, this->currentLateralSocketOffset, 0.0f);
}

float URTSCamera::calculateYOffset(float worldY) const
{
	if (!this->bEnableYBoundaryConstraint || !this->bHasResolvedBoundaryData) return 0.0f;

	const FVector boxOrigin = this->ResolvedBoundaryOrigin;
	const FVector boxExtents = this->ResolvedBoundaryExtents;

	const float differenceY = worldY - boxOrigin.Y;
	const float normalizedDistanceY = FMath::Abs(differenceY) / FMath::Max(boxExtents.Y, 1.0f);
	const float safeZoneRatio = 1.0f - this->boundaryTransitionZoneRatio;

	if (normalizedDistanceY > safeZoneRatio)
	{
		const float triggerAlpha = (normalizedDistanceY - safeZoneRatio) / FMath::Max(this->boundaryTransitionZoneRatio, 0.01f);
		const float reach = this->springArmComponent->TargetArmLength * this->lateralReachFactor;
		const float offset = triggerAlpha * (reach * this->minimumZoomBoundaryConstraint) * ((differenceY > 0.0f) ? -1.0f : 1.0f);
		
		UE_LOG(LogRTSCamera, VeryVerbose, TEXT("横向 (Y) 比例补偿: alpha=%.2f, reach=%.1f, offset=%.1f"), triggerAlpha, reach, offset);
		return offset;
	}

	return 0.0f;
}

float URTSCamera::calculateXOffset(float worldX) const
{
	if (!this->bEnableXBoundaryConstraint || !this->bHasResolvedBoundaryData) return 0.0f;

	const FVector boxOrigin = this->ResolvedBoundaryOrigin;
	const FVector boxExtents = this->ResolvedBoundaryExtents;

	const float differenceX = worldX - boxOrigin.X;
	const float normalizedDistanceX = FMath::Abs(differenceX) / FMath::Max(boxExtents.X, 1.0f);
	const float safeZoneRatio = 1.0f - this->boundaryTransitionZoneRatio;

	if (normalizedDistanceX > safeZoneRatio)
	{
		const float triggerAlpha = (normalizedDistanceX - safeZoneRatio) / FMath::Max(this->boundaryTransitionZoneRatio, 0.01f);
		
		// 南北向由于 Pitch 倾角是不对称的
		// 北端 (diff > 0) 使用前进伸展 forwardReach；南端 (diff < 0) 使用后退伸展 backwardReach
		const float currentFactor = (differenceX > 0.0f) ? this->forwardReachFactor : this->backwardReachFactor;
		const float reach = this->springArmComponent->TargetArmLength * currentFactor;

		// 核心逻辑：
		// 1. 在北端 (diff > 0)，我们需要向南 (Negative X) 偏移，把视口顶部的地图拉回来。
		// 2. 在南端 (diff < 0)，我们需要向北 (Positive X) 偏移，把视口底部的地图边界拉进来。
		const float direction = (differenceX > 0.0f) ? -1.0f : 1.0f;
		const float offset = direction * triggerAlpha * (reach * this->minimumZoomBoundaryConstraint);

		UE_LOG(LogRTSCamera, VeryVerbose, TEXT("南北 (X) 修正方案: diff=%.1f, factor=%.4f, offset=%.1f"), differenceX, currentFactor, offset);
		return offset;
	}

	return 0.0f;
}
