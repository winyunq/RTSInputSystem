// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/RTSBuiltinCommandButton.h"
#include "RTSCmd_BuildFactory.generated.h"

/**
 * Built-in Command: Build Factory
 * - Cost: 365 cash
 * - CD: 75s
 * - Auto-Cast: Yes
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildFactory : public URTSBuiltinCommandButton
{
	GENERATED_BODY()

public:
	URTSCmd_BuildFactory()
	{
		// Setup Defaults
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.Factory"), false);
		TargetType = ERTSCommandTargetType::Location;
		
        // Note: Loading assets in constructor can be risky for cooked builds if not careful, 
        // but standard for CDO. 
        // Ideally we use TSoftObjectPtr or just leave Icon null for now (User can assign in BP subclass if they want)
        // Or we use a placeholder if available.
        
		DisplayName = FText::FromString(TEXT("建造工厂"));
		
		FString DescStr = TEXT("消耗75现金和365材料，建造一座1×1格工厂。命令来源不限，只要工厂中心位于任意己方城市或己方工业园的工业范围内即可；施工需要180秒（约6个游戏月），完工前不提供GDP；一级工厂在平原提供7 GDP，在山地提供5 GDP。<n/><n/><RichText.Yellow>定位： 经济。</>");
        // Formatting fixes for XML/RichText
		Description = FText::FromString(DescStr);

		PreferredIndex = 5; // Row 2, Col 1
		DefaultCooldown = 0.0f;
		bAllowAutoCast = false;
		LowValueCost = 75;
		PlacementFootprintCells = FIntPoint(1, 1);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Economics/Factory/Common_IndustrialFactory_Workshop/SM_Common_IndustrialFactory_Workshop.SM_Common_IndustrialFactory_Workshop")));
	}
};

