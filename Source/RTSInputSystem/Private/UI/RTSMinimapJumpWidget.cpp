// Copyright Winyunq, 2025. All Rights Reserved.

#include "UI/RTSMinimapJumpWidget.h"
#include "RTSCamera.h"
#include "RTSSelectionSubsystem.h"
#include "RTSSelector.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ActorComponent.h"
#include "Components/Border.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Rendering/DrawElements.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Engine/World.h"

URTSMinimapJumpWidget::URTSMinimapJumpWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::Visible);
	ForceVolatile(true);
}

TSharedRef<SWidget> URTSMinimapJumpWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* InputSurface = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("MinimapInputSurface"));
		InputSurface->SetBrushColor(FLinearColor::Transparent);
		InputSurface->SetVisibility(ESlateVisibility::HitTestInvisible);
		WidgetTree->RootWidget = InputSurface;
	}

	return Super::RebuildWidget();
}

void URTSMinimapJumpWidget::NativeConstruct()
{
	Super::NativeConstruct();
	InitializeJumpWidget();
}

void URTSMinimapJumpWidget::InitializeJumpWidget()
{
	SetVisibility(ESlateVisibility::Visible);
	SetIsFocusable(true);

	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		PlayerController->bEnableClickEvents = true;
		PlayerController->bEnableMouseOverEvents = true;
	}

	LoadMapRegionBounds();

	CachedJumpComponent = FindRTSCameraJumpComponent();
	BindRTSCameraFrustumUpdates();
}

void URTSMinimapJumpWidget::NativeDestruct()
{
	if (URTSCamera* RTSCamera = Cast<URTSCamera>(CachedJumpComponent.Get()))
	{
		RTSCamera->onMinimapFrustumUpdated.RemoveAll(this);
	}

	Super::NativeDestruct();
}

void URTSMinimapJumpWidget::LoadMapRegionBounds()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		// 缺省大小 65536
		MapOrigin = FVector::ZeroVector;
		MapExtents = FVector(32768.0f, 32768.0f, 1.0f);
		return;
	}

	FString MapName = World->GetMapName();
	MapName.RemoveFromStart(World->StreamingLevelsPrefix);
	if (MapName.IsEmpty())
	{
		MapName = TEXT("Default");
	}

	FString IniPath = FPaths::ProjectConfigDir() / TEXT("MapRegion") / MapName / TEXT("MapRegion.ini");
	if (!FPaths::FileExists(IniPath))
	{
		// 缺省大小 65536
		MapOrigin = FVector::ZeroVector;
		MapExtents = FVector(32768.0f, 32768.0f, 1.0f);
		return;
	}

	FConfigFile IniFile;
	IniFile.Read(IniPath);

	float OriginX = 0.0f;
	float OriginY = 0.0f;
	float SizeX = 0.0f;
	float SizeY = 0.0f;
	if (!IniFile.GetFloat(TEXT("MapRegion"), TEXT("OriginX"), OriginX) ||
		!IniFile.GetFloat(TEXT("MapRegion"), TEXT("OriginY"), OriginY) ||
		!IniFile.GetFloat(TEXT("MapRegion"), TEXT("SizeX"), SizeX) ||
		!IniFile.GetFloat(TEXT("MapRegion"), TEXT("SizeY"), SizeY))
	{
		// 缺省大小 65536
		MapOrigin = FVector::ZeroVector;
		MapExtents = FVector(32768.0f, 32768.0f, 1.0f);
		return;
	}

	MapOrigin = FVector(OriginX + SizeX * 0.5f, OriginY + SizeY * 0.5f, 0.0f);
	MapExtents = FVector(SizeX * 0.5f, SizeY * 0.5f, 1.0f);
}

void URTSMinimapJumpWidget::BindRTSCameraFrustumUpdates()
{
	URTSCamera* RTSCamera = Cast<URTSCamera>(CachedJumpComponent.Get());
	if (!RTSCamera)
	{
		return;
	}

	RTSCamera->onMinimapFrustumUpdated.RemoveAll(this);
	RTSCamera->onMinimapFrustumUpdated.AddUObject(this, &URTSMinimapJumpWidget::HandleMinimapFrustumUpdated);
	RTSCamera->updateMinimapFrustum();
}

void URTSMinimapJumpWidget::HandleMinimapFrustumUpdated()
{
	Invalidate(EInvalidateWidgetReason::Paint);
}

#if WITH_EDITOR
const FText URTSMinimapJumpWidget::GetPaletteCategory()
{
	return NSLOCTEXT("RTSInputSystem", "RTSMinimapJumpWidgetPaletteCategory", "RTS Input System");
}
#endif

int32 URTSMinimapJumpWidget::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	int32 MaxLayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (GetWorld() && GetWorld()->GetTimeSeconds() < MarkerExpiresAt)
	{
		const FVector2D Center = ConvertWorldToWidgetLocal(FVector2D(MarkerLocation), AllottedGeometry.GetLocalSize());
		const float DrawScale = FMath::Max(AllottedGeometry.GetAccumulatedLayoutTransform().GetScale(), 0.01f);
		const float Radius = 12.0f / DrawScale;
		TArray<FVector2D> Diamond = { Center + FVector2D(0, -Radius), Center + FVector2D(Radius, 0),
			Center + FVector2D(0, Radius), Center + FVector2D(-Radius, 0), Center + FVector2D(0, -Radius) };
		FSlateDrawElement::MakeLines(OutDrawElements, ++MaxLayerId, AllottedGeometry.ToPaintGeometry(),
			Diamond, ESlateDrawEffect::None, MarkerColor, true, 2.0f / DrawScale);
	}
	if (!bDrawCameraFrustum || FrustumLineThickness <= 0.0f)
	{
		return MaxLayerId;
	}

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
	{
		return MaxLayerId;
	}

	if (!CachedJumpComponent.IsValid())
	{
		URTSMinimapJumpWidget* MutableThis = const_cast<URTSMinimapJumpWidget*>(this);
		MutableThis->CachedJumpComponent = MutableThis->FindRTSCameraJumpComponent();
		MutableThis->BindRTSCameraFrustumUpdates();
	}

	const URTSCamera* RTSCamera = Cast<URTSCamera>(CachedJumpComponent.Get());
	if (!RTSCamera)
	{
		return MaxLayerId;
	}

	TArray<FVector2D> Points;
	Points.Reserve(5);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector& WorldPoint = RTSCamera->minimapFrustumPoints[Index];
		Points.Add(ConvertWorldToWidgetLocal(FVector2D(WorldPoint.X, WorldPoint.Y), LocalSize));
	}
	const FVector2D FirstPoint = Points[0];
	Points.Add(FirstPoint);

	const int32 FrustumLayerId = MaxLayerId + 1;
	FSlateDrawElement::MakeLines(
		OutDrawElements,
		FrustumLayerId,
		AllottedGeometry.ToPaintGeometry(),
		Points,
		ESlateDrawEffect::None,
		FrustumLineColor,
		true,
		FrustumLineThickness);

	return FrustumLayerId;
}

FVector2D URTSMinimapJumpWidget::ConvertWorldToWidgetLocal(const FVector2D& WorldPos, const FVector2D& WidgetSize) const
{
	if (MapExtents.X >= KINDA_SMALL_NUMBER && MapExtents.Y >= KINDA_SMALL_NUMBER)
	{
		float NormalizedX = (WorldPos.X - (MapOrigin.X - MapExtents.X)) / (2.0f * MapExtents.X);
		float NormalizedY = (WorldPos.Y - (MapOrigin.Y - MapExtents.Y)) / (2.0f * MapExtents.Y);
		NormalizedX = FMath::Clamp(NormalizedX, 0.0f, 1.0f);
		NormalizedY = FMath::Clamp(NormalizedY, 0.0f, 1.0f);

		return FVector2D(NormalizedY * WidgetSize.X, (1.0f - NormalizedX) * WidgetSize.Y);
	}

	return FVector2D::ZeroVector;
}

FVector2D URTSMinimapJumpWidget::ConvertWidgetLocalToWorld(const FVector2D& LocalPos, const FVector2D& WidgetSize) const
{
	if (WidgetSize.X <= 0.0f || WidgetSize.Y <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}

	if (MapExtents.X >= KINDA_SMALL_NUMBER && MapExtents.Y >= KINDA_SMALL_NUMBER)
	{
		const float UParam = FMath::Clamp(LocalPos.X / WidgetSize.X, 0.0f, 1.0f);
		const float VParam = FMath::Clamp(LocalPos.Y / WidgetSize.Y, 0.0f, 1.0f);
		const float NormalizedX = 1.0f - VParam;
		const float NormalizedY = UParam;

		return FVector2D(
			(MapOrigin.X - MapExtents.X) + NormalizedX * (2.0f * MapExtents.X),
			(MapOrigin.Y - MapExtents.Y) + NormalizedY * (2.0f * MapExtents.Y));
	}

	return FVector2D::ZeroVector;
}

UActorComponent* URTSMinimapJumpWidget::FindRTSCameraJumpComponent() const
{
	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController)
	{
		return nullptr;
	}

	auto FindOnActor = [this](AActor* Actor) -> UActorComponent*
	{
		return Actor ? Actor->FindComponentByClass<URTSCamera>() : nullptr;
	};

	if (UActorComponent* Component = FindOnActor(PlayerController->GetViewTarget()))
	{
		return Component;
	}

	return FindOnActor(PlayerController->GetPawn());
}

bool URTSMinimapJumpWidget::TryJumpToWorldLocation(const FVector& WorldLocation)
{
	if (!bAutoJumpToRTSCamera)
	{
		return false;
	}

	UActorComponent* JumpComponent = CachedJumpComponent.Get();
	URTSCamera* RTSCamera = Cast<URTSCamera>(JumpComponent);
	if (!RTSCamera)
	{
		JumpComponent = FindRTSCameraJumpComponent();
		CachedJumpComponent = JumpComponent;
		RTSCamera = Cast<URTSCamera>(JumpComponent);
		if (RTSCamera)
		{
			BindRTSCameraFrustumUpdates();
		}
	}

	if (!RTSCamera)
	{
		return false;
	}

	RTSCamera->jumpTo(WorldLocation);
	return true;
}

void URTSMinimapJumpWidget::RequestWorldLocation(const FVector2D& WorldPos)
{
	const FVector WorldLocation(WorldPos, 0.0f);
	OnWorldLocationRequested.Broadcast(WorldLocation);
	TryJumpToWorldLocation(WorldLocation);
}

FVector URTSMinimapJumpWidget::ResolveCommandWorldLocation(const FVector2D& WorldPos) const
{
	FVector WorldLocation(WorldPos, MapOrigin.Z);
	UWorld* World = GetWorld();
	if (!World)
	{
		return WorldLocation;
	}

	// Minimap input has no viewport ray. Project its XY coordinate vertically so
	// movement receives the same kind of ground location as a scene right-click.
	const float TraceHalfHeight = FMath::Max(
		100000.0f,
		FMath::Max(MapExtents.X, MapExtents.Y) * 4.0f);
	const FVector TraceStart(WorldPos, MapOrigin.Z + TraceHalfHeight);
	const FVector TraceEnd(WorldPos, MapOrigin.Z - TraceHalfHeight);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RTSMinimapCommandGroundTrace), false);
	if (const APlayerController* PlayerController = GetOwningPlayer())
	{
		QueryParams.AddIgnoredActor(PlayerController);
		QueryParams.AddIgnoredActor(PlayerController->GetPawn());
	}

	FHitResult GroundHit;
	if (World->LineTraceSingleByChannel(
		GroundHit,
		TraceStart,
		TraceEnd,
		ECC_Visibility,
		QueryParams))
	{
		WorldLocation = GroundHit.Location;
	}

	return WorldLocation;
}

bool URTSMinimapJumpWidget::TryCommitPendingCommand(const FVector& WorldLocation) const
{
	APlayerController* PlayerController = GetOwningPlayer();
	URTSSelector* Selector = PlayerController
		? PlayerController->FindComponentByClass<URTSSelector>()
		: nullptr;
	return Selector && Selector->CommitPendingTargetingAtWorldLocation(WorldLocation);
}

bool URTSMinimapJumpWidget::HandlePendingTargetConfirmationAtScreenPosition(const FVector2D& ScreenPosition)
{
	if (!IsVisible())
	{
		return false;
	}

	const FGeometry Geometry = GetCachedGeometry();
	const FVector2D LocalSize = Geometry.GetLocalSize();
	if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
	{
		return false;
	}

	const FVector2D LocalPosition = Geometry.AbsoluteToLocal(ScreenPosition);
	if (LocalPosition.X < 0.0f
		|| LocalPosition.Y < 0.0f
		|| LocalPosition.X > LocalSize.X
		|| LocalPosition.Y > LocalSize.Y)
	{
		return false;
	}

	const FVector2D WorldPosition = ConvertWidgetLocalToWorld(LocalPosition, LocalSize);
	TryCommitPendingCommand(ResolveCommandWorldLocation(WorldPosition));

	// The pointer belongs to the minimap. Do not let an unsupported minimap
	// target fall through and hit the scene hidden behind the widget.
	return true;
}

bool URTSMinimapJumpWidget::TryIssueMoveCommand(const FVector& WorldLocation) const
{
	APlayerController* PlayerController = GetOwningPlayer();
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	URTSSelectionSubsystem* SelectionSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<URTSSelectionSubsystem>()
		: nullptr;
	if (!SelectionSubsystem)
	{
		return false;
	}

	const FGameplayTag MoveTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.Command.Move")),
		false);
	if (!MoveTag.IsValid())
	{
		return false;
	}

	const bool bQueueCommand = PlayerController->IsInputKeyDown(EKeys::LeftShift)
		|| PlayerController->IsInputKeyDown(EKeys::RightShift);
	SelectionSubsystem->IssueCommandWithLocation(
		MoveTag,
		WorldLocation,
		bQueueCommand,
		/*bForceStrategicNavigation*/ true);
	return true;
}

void URTSMinimapJumpWidget::RequestMoveCommand(const FVector2D& WorldPos)
{
	const FVector WorldLocation = ResolveCommandWorldLocation(WorldPos);
	TryIssueMoveCommand(WorldLocation);
}

FReply URTSMinimapJumpWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bLocationPicking)
	{
		if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bLocationPicking = false;
			const FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
			OnLocationPicked.Broadcast(ResolveCommandWorldLocation(ConvertWidgetLocalToWorld(LocalPos, InGeometry.GetLocalSize())));
			return FReply::Handled();
		}
		if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			bLocationPicking = false;
			return FReply::Handled();
		}
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D WorldPos = ConvertWidgetLocalToWorld(LocalPos, InGeometry.GetLocalSize());
		bIsDragging = true;
		RequestWorldLocation(WorldPos);
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		const FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D WorldPos = ConvertWidgetLocalToWorld(LocalPos, InGeometry.GetLocalSize());
		const FVector WorldLocation = ResolveCommandWorldLocation(WorldPos);
		if (!TryCommitPendingCommand(WorldLocation))
		{
			TryIssueMoveCommand(WorldLocation);
		}
		// Consume RMB here so the global viewport command cannot issue a second
		// command at the scene location hidden behind the minimap.
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void URTSMinimapJumpWidget::ShowLocationMarker(const FVector& Location, const FLinearColor& Color)
{
	MarkerLocation = Location;
	MarkerColor = Color;
	MarkerExpiresAt = GetWorld() ? GetWorld()->GetTimeSeconds() + 12.0 : 0.0;
	InvalidateLayoutAndVolatility();
}

FReply URTSMinimapJumpWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply URTSMinimapJumpWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsDragging && HasMouseCapture())
	{
		const FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D WorldPos = ConvertWidgetLocalToWorld(LocalPos, InGeometry.GetLocalSize());
		RequestWorldLocation(WorldPos);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
