// Copyright 2024 Jesus Bracho All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include "InputMappingContext.h"
#include "Camera/CameraComponent.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "RTSCamera.generated.h"

class FViewport;

/** @brief 视野框数据更新时的多播委托声明 */
DECLARE_MULTICAST_DELEGATE(FOnMinimapFrustumUpdated);

/**
 * @brief       RTS 相机组件，处理视口平移、边缘滚动、意图缩放及视野投影逻辑。
 * 
 * 组件遵循“战略由人，战术由AI”的设计原则，旨在平衡操作的顺滑感与 UI 的即时反馈。
 **/
UCLASS(Blueprintable, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class RTSINPUTSYSTEM_API URTSCamera : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * @brief       初始化相机组件的默认属性与子对象引用
	 **/
	URTSCamera();

	/**
	 * @brief       使相机视野物理锁定并跟随指定的目标 Actor
	 * 
	 * @param       参数名称: target                         数据类型:        AActor*
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void followTarget(AActor* target);

	/**
	 * @brief       取消当前的相机目标跟随状态
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void unFollowTarget();

	/**
	 * @brief       将本地控制器的相机观察点设置为此组件的所有者
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void setActiveCamera();
	
	/**
	 * @brief       将相机组件瞬間移动至指定的 X/Y 地面坐标
	 * 
	 * @param       参数名称: position                       数据类型:        FVector
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera")
	void jumpTo(FVector position);

	/**
	 * @brief       由视口的 Slate 鼠标移动事件转发，即时驱动拖拽和边缘滚动。
	 *
	 * @param       ViewportPosition 当前鼠标在视口像素坐标系中的位置。
	 **/
	void HandlePointerMoved(const FVector2D& ViewportPosition);

	/**
	 * @brief       获取当前相机实际使用的移动边界。该数据来自关卡级 MapRegion ini，
	 *              与小地图单位显示保持同一套坐标系。
	 *
	 * @param       OutOrigin                       数据类型:        FVector&
	 * @param       OutExtents                      数据类型:        FVector&
	 *
	 * @return      返回值类型:      bool
	 **/
	bool getResolvedMovementBounds(FVector& OutOrigin, FVector& OutExtents) const;

	/// 相机缩放的最小目标距离（最接近地面）
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings", meta = (DisplayName = "最小缩放高度", ToolTip = "相机距离地面的最近距离。"))
	float minimumZoomLength;

	/// 相机缩放的最大目标距离（最高视野）
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings", meta = (DisplayName = "最大缩放高度", ToolTip = "相机距离地面的最远距离。"))
	float maximumZoomLength;

	/// 缩放插值的补全速率（值越大，物理位置追赶意图的速度越快）
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings", meta = (DisplayName = "缩放平滑速度", ToolTip = "物理相机追赶缩放意图的速度。"))
	float zoomCatchupSpeed;

	/// 单次滚轮操作触发的缩放距离步长
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Zoom Settings", meta = (DisplayName = "单次缩进步幅", ToolTip = "鼠标滚轮单次滚动引起的距离变化量。"))
	float zoomSpeed;

	/// 初始化时的相机俯仰角 (Pitch)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera", meta = (DisplayName = "初始俯仰角", ToolTip = "相机的初始垂直倾斜角度（度）。"))
	float startingPitchAngle;

	/// 初始化时的相机偏航角 (Yaw)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera", meta = (DisplayName = "初始偏航角", ToolTip = "相机的初始水平旋转角度（度）。"))
	float startingYawAngle;

	/// 相机在最大缩放高度时的移动速度上限
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera", meta = (DisplayName = "最大平移速度", ToolTip = "在高空缩放时的移动速度。"))
	float maxMovementSpeed;

	/// 相机在最小缩放高度时的基础移动速度
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera", meta = (DisplayName = "最小平移速度", ToolTip = "在低空缩放时的移动速度。"))
	float minMovementSpeed;

	/// 输入控制下的水平旋转感官速度
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera", meta = (DisplayName = "旋转响应速度", ToolTip = "相机水平旋转的速度权重。"))
	float rotationSpeed;
	
	/// 相机拖拽操作在视口中的拉伸增量比例
	UPROPERTY(
		BlueprintReadWrite,
		EditAnywhere,
		Category = "RTSCamera",
		meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "拖拽惯性系数", ToolTip = "鼠标拖拽移动时的平滑权重。")
	)
	float dragExtent;

	/** 当接近边界时，约束触发的插值强度 (0=相机根部重合边缘, 1=视野边缘对齐边缘) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera | Boundary", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "低空边界约束强度", ToolTip = "0表示相机的根组件中心能够到达边界，1表示相机的视野边缘会被锁在地图内部。"))
	float minimumZoomBoundaryConstraint;

	/** 边界侧倾过渡区的比例 (0.15 代表最后 15% 区域触发) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera | Boundary", meta = (ClampMin = "0.0", ClampMax = "0.5", DisplayName = "边界侧倾过渡比例", ToolTip = "定义从地图边缘向内多少比例开始产生侧倾效果。"))
	float boundaryTransitionZoneRatio;

	/** 是否启用 X 轴 (南北/上下) 方向的边界视野约束 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera | Boundary", meta = (DisplayName = "启用 X 轴边界约束"))
	bool bEnableXBoundaryConstraint;

	/** 是否启用 Y 轴 (东西/左右) 方向的边界视野约束 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera | Boundary", meta = (DisplayName = "启用 Y 轴边界约束"))
	bool bEnableYBoundaryConstraint;
	/// 启用相机位置移动的插值延迟
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera", meta = (DisplayName = "启用位置平滑", ToolTip = "开启后相机移动将具有物理惯性。"))
	bool enableCameraLag;

	/// 启用相机视野旋转的插值延迟
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera", meta = (DisplayName = "启用旋转平滑", ToolTip = "开启后相机旋转将具有物理惯性。"))
	bool enableCameraRotationLag;

	/// 启用基于地形起伏动态修正相机根高度的功能
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Dynamic Camera Height Settings")
	bool enableDynamicCameraHeight;

	/// 用于地形高度探测的碰撞通道类型
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Dynamic Camera Height Settings")
	TEnumAsByte<ECollisionChannel> collisionChannel;

	/// 地形校正射线在向上/向下扫描时的最大探测距离
	UPROPERTY(
		BlueprintReadWrite,
		EditAnywhere,
		Category = "RTSCamera - Dynamic Camera Height Settings",
		meta=(EditCondition="enableDynamicCameraHeight")
	)
	float findGroundTraceLength;

	/// 启用鼠标触发视口边缘后的相机滚动
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Edge Scroll Settings")
	bool enableEdgeScrolling;

	/// 距各视口边缘的触发距离，占该方向尺寸的比例；0.02 表示 2%，0 禁用。
	/// 鼠标停在区域内持续滚动，越靠近边缘越快；HUD 按钮焦点不影响滚动。
	UPROPERTY(
		BlueprintReadWrite,
		EditAnywhere,
		Category = "RTSCamera - Edge Scroll Settings",
		meta=(EditCondition="enableEdgeScrolling", ClampMin="0.0", ClampMax="0.5", UIMin="0.0", UIMax="0.5")
	)
	float distanceFromEdgeThreshold;

	/// 相机对应的增强输入上下文
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputMappingContext* inputMappingContext;

	/// 旋转控制输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* rotateCameraAxisAction;

	/// 向左步进旋转输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* turnCameraLeftAction;

	/// 向右步进旋转输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* turnCameraRightAction;

	/// 前后移动输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* moveCameraYAxisAction;

	/// 左右移动输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* moveCameraXAxisAction;

	/// 鼠标拖拽输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* dragCameraAction;

	/// 缩放输入动作映射
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RTSCamera - Inputs")
	UInputAction* zoomCameraAction;

	/// 委托实例：当视野投影点数组被成功更新后触发
	FOnMinimapFrustumUpdated onMinimapFrustumUpdated;

protected:
	/**
	 * @brief       生命周期起始点：建立组件依赖与输入绑定
	 **/
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * @brief       响应增强输入事件。执行缩放并标记視野更新。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onZoomCameraActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。执行相机旋转并标记視野更新。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onRotateCameraActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。执行向左转向操作。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onTurnCameraLeftActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。执行向右转向操作。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onTurnCameraRightActionTriggered(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。请求纵向平移指令。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onMoveCameraYAxisActionTriggered(const FInputActionValue& value);
	void onMoveCameraYAxisActionCompleted(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。请求横向平移指令。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onMoveCameraXAxisActionTriggered(const FInputActionValue& value);
	void onMoveCameraXAxisActionCompleted(const FInputActionValue& value);

	/**
	 * @brief       响应增强输入事件。控制鼠标拖拽状态下的相机平移逻辑。
	 * 
	 * @param       参数名称: value                         数据类型:        const FInputActionValue&
	 **/
	void onDragCameraActionStarted(const FInputActionValue& value);
	void onDragCameraActionCompleted(const FInputActionValue& value);

	/**
	 * @brief       将已组合的移动意图按相机帧 DeltaTime 积分为位移
	 * 
	 * @param       xAxisValue                      数据类型:        float
	 * @param       yAxisValue                      数据类型:        float
	 * @param       movementScale                   数据类型:        float
	 **/
	void requestCameraMovement(
		float xAxisValue,
		float yAxisValue,
		float movementScale,
		float DeltaSeconds);

	/// 组件所属的 Actor 引用，定义了相机的生命周期主体
	UPROPERTY()
	AActor* cameraOwner;

	/// 根场景组件，控制整个相机组在该坐标下的水平面移动
	UPROPERTY()
	USceneComponent* rootComponent;

	/// 相机组件，定义视野视野参数与最终渲染输出
	UPROPERTY()
	UCameraComponent* cameraComponent;

	/// 弹簧臂组件，控制相机与中心点之间的意图长度与俯仰关系
	UPROPERTY()
	USpringArmComponent* springArmComponent;

	/// 实时战略专用的玩家控制器，分发输入事件
	UPROPERTY()
	APlayerController* realTimeStrategyPlayerController;

	bool bHasResolvedBoundaryData = false;
	FVector ResolvedBoundaryOrigin = FVector::ZeroVector;
	FVector ResolvedBoundaryExtents = FVector::ZeroVector;
	float ResolvedBoundaryOverflowUU = 0.0f;

	/// 玩家输入的理想缩放目标高度，视野计算将优先同步此意图而非物理插值过程
	UPROPERTY()
	float desiredZoomLength;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FWinyunqSelectionTabInputTest;
#endif
	void resolveComponentDependencyPointers();
	void setupInitialSpringArmState();

	bool initializeMovementBoundsFromMapRegion();
	void configureInputModeForEdgeScrolling();
	void validateEnhancedInputAvailability();
	void registerInputMappingContext();
	void bindActionCallbacks();

	bool executeEdgeScrollingEvaluation(
		const FVector2D& ViewportPosition,
		float DeltaSeconds);
	void refreshPointerWorldState(const FVector2D& ViewportPosition);

	void handleFollowTargetTransformUpdated(
		USceneComponent* UpdatedComponent,
		EUpdateTransformFlags UpdateTransformFlags,
		ETeleportType Teleport);
	void handleViewportResized(FViewport* Viewport, uint32 Unused);
	void recalculateBoundaryReachFactors(const FVector2D& ViewportSize);
	void applyCameraStateChange();
	bool getViewportSizePixels(FVector2D& OutViewportSize) const;
	float getClampedInputDeltaSeconds(float DeltaSeconds) const;
	void rectifyRootHeightFromTerrain();
	
	/** @brief 计算当前坐标下的边界补偿并应用 */
	void applyBoundaryConstraints();

	/** @brief 基于 Y 坐标计算 Lateral Socket Offset */
	float calculateYOffset(float worldY) const;

	/** @brief 基于 X 坐标计算 Vertical Socket Offset */
	float calculateXOffset(float worldX) const;

	/// 相机当前正在锁定跟随的 Actor 实测对象
	UPROPERTY()
	TWeakObjectPtr<AActor> activeCameraFollowTarget;
	TWeakObjectPtr<USceneComponent> activeCameraFollowRootComponent;

	/// 缓存边界侧移量 (SocketOffset.Y)
	float currentLateralSocketOffset;
	/// 缓存边界纵移量 (SocketOffset.X)
	float currentVerticalSocketOffset;
	/** @brief 预计算的横向延伸系数 (Lateral Reach / TargetArmLength) */
	float lateralReachFactor;

	/** @brief 预计算的纵向延伸系数 (Forward Reach / TargetArmLength) */
	float forwardReachFactor;

	/** @brief 预计算的后向延伸系数 (Backward Reach / TargetArmLength) */
	float backwardReachFactor;

	/// 状态位：指示是否正在进行鼠标拖拽操作
	UPROPERTY()
	bool isDragging;

	/// 拖拽操作开始时的视口坐标缓存
	UPROPERTY()
	FVector2D dragInteractionInitialLocation;

	/// 鼠标事件仅写入最新拖拽位置，相机 Tick 每个画面帧消费一次。
	FVector2D dragInteractionCurrentLocation = FVector2D::ZeroVector;

	/// 当前瞬时计算的相机移动速度值
	UPROPERTY()
	float currentMovementSpeed;

	/// Enhanced Input 事件只更新轴意图，由相机 Tick 统一积分。
	float pendingMoveXAxis = 0.0f;
	float pendingMoveYAxis = 0.0f;

	/// 静止光标下的世界命中查询独立限制为 24Hz，不阻塞逐画面帧的相机运动。
	float pointerWorldRefreshAccumulator = 0.0f;

public:
	/// 静态数组，存储由视野投影计算出的地平面四个接地区顶点。
	/// 顺序遵循：[0]左上, [1]右上, [2]右下, [3]左下。
	FVector minimapFrustumPoints[4];

	/**
	 * @brief       强制触发視野投影计算，基于当前位置及缩放意图刷新 minimapFrustumPoint 数组数据。
	 *              注意：仅在相机产生明确的战略意图变更（移动、缩放、跳转）时产生计算开销。
	 **/
	UFUNCTION(BlueprintCallable, Category = "RTSCamera|Minimap")
	void updateMinimapFrustum();
};
