// Copyright 2024 Jesus Bracho All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include "GameFramework/HUD.h"
#include "RTSHUD.generated.h"

UCLASS()
class RTSINPUTSYSTEM_API ARTSHUD : public AHUD
{
	GENERATED_BODY()

public:
	ARTSHUD();

	/** Execute the RTS selection query without requiring an ARTSHUD instance. */
	static void PerformScreenSelection(
		APlayerController* PlayerController,
		class URTSSelector* SelectorComponent,
		const FVector2D& StartPoint,
		const FVector2D& EndPoint,
		float ClickThresholdSq = 1.0f);

	/**
	 * Resolves exactly one controllable selectable under a screen point.
	 * Actor and Mass hits are depth-arbitrated so a click can never return both.
	 */
	static bool ResolveSingleSelectableAtScreenPosition(
		APlayerController* PlayerController,
		const FVector2D& ScreenPosition,
		AActor*& OutActor,
		struct FEntityHandle& OutEntity,
		FVector& OutWorldLocation);
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Selection Box")
	FLinearColor SelectionBoxColor;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Selection Box")
	float SelectionBoxThickness;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Selection Box")
	FLinearColor SelectionBoxFillColor;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Selection Box")
	float MinSelectionSizeSq;

	UFUNCTION(BlueprintCallable, Category = "Selection Box")
	void BeginSelection(const FVector2D& StartPoint);

	UFUNCTION(BlueprintCallable, Category = "Selection Box")
	void UpdateSelection(const FVector2D& EndPoint);

	UFUNCTION(BlueprintCallable, Category = "Selection Box")
	void EndSelection();

	UFUNCTION(BlueprintNativeEvent, Category = "Selection Box")
	void DrawSelectionBox(const FVector2D& StartPoint, const FVector2D& EndPoint);

	UFUNCTION(BlueprintNativeEvent, Category = "Selection Box")
	void PerformSelection();

protected:
	/** Draw the selection box */
	void DrawSelectionMarquee();

	virtual void DrawHUD() override;

private:
	void PerformMassSelection(TArray<struct FEntityHandle>& OutEntities);

	bool bIsDrawingSelectionBox;
	FVector2D SelectionStart;
	FVector2D SelectionEnd;
};
