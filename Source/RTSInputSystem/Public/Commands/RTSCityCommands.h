// Copyright 2024 Winy unq All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/RTSCommandGridAsset.h"
#include "Data/RTSBuiltinCommandButton.h"
#include "Data/RTSUnitProductionCommandButton.h"
#include "RTSCmd_BuildFactory.h"
#include "RTSCityCommands.generated.h"

/** Row 1 / Col 3: choose the capital-base location and establish this city as capital. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_EstablishCapital : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_EstablishCapital()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.City.EstablishCapital"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("定都"));
		Description = FText::FromString(TEXT("选择一个合法网格放置首都基地，并将当前城市设为本阵营首都。"));
		PreferredIndex = 2;
		DefaultCooldown = 1.0f;
		// Three shared 16 uu build cells produce a 48 x 48 uu base footprint.
		PlacementFootprintCells = FIntPoint(3, 3);
	}
};

/** Row 1 / Col 2: build the city's military base. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildCityBase : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildCityBase()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.City.BuildBase"), false);
		TargetType = ERTSCommandTargetType::Instant;
		DisplayName = FText::FromString(TEXT("建造基地"));
		Description = FText::FromString(TEXT("在当前已占领城市建立军事基地，解锁城市驻军与征召能力。"));
		PreferredIndex = 1;
		DefaultCooldown = 30.0f;
	}
};

// --- ROW 2: Buildings ---

/** Compatibility command: the former generic bunker now resolves to the merged field cover. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildDefense : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildDefense()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.Defense"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("修建沙袋堑壕"));
		Description = FText::FromString(TEXT("修建六边形三层沙袋工事；该建筑同时表达堑壕、掩体与碉堡。优先由附近同队步兵施工；附近没有步兵时由军官亲自施工。"));
		PreferredIndex = 0;
		DefaultCooldown = 0.0f;
		PlacementFootprintCells = FIntPoint(2, 2);
	}
};

/** Officer order: merged hexagonal sandbag cover. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildFieldCover : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildFieldCover()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.FieldCover"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("沙袋堑壕"));
		Description = FText::FromString(TEXT("第一排碉堡：六边形三层沙袋工事，同时代表堑壕、掩体与碉堡。优先由附近同队步兵施工；附近没有步兵时由军官亲自施工。"));
		PreferredIndex = 0;
		DefaultCooldown = 0.0f;
		PlacementFootprintCells = FIntPoint(2, 2);
	}
};

/** Officer order: sandbag machine-gun bunker with one articulated gunner. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildMachineGunBunker : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildMachineGunBunker()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.MachineGunBunker"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("机枪碉堡"));
		Description = FText::FromString(TEXT("在沙袋工事内部署一名机枪手；射手和机枪作为炮塔追踪目标。优先由附近同队步兵施工。"));
		PreferredIndex = 1;
		DefaultCooldown = 0.0f;
		PlacementFootprintCells = FIntPoint(2, 2);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Defence/Bunker/China/China_MachineGunBunker_DP28/China_MachineGunBunker_DP28.China_MachineGunBunker_DP28")));
	}
};

/** Officer order: sandbag anti-tank bunker with an articulated two-man gun crew. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildAntiTankBunker : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildAntiTankBunker()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.AntiTankBunker"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("反坦克碉堡"));
		Description = FText::FromString(TEXT("在沙袋工事内部署两名炮手和一门57毫米反坦克炮；炮组整体作为炮塔追踪目标。优先由附近同队步兵施工。"));
		PreferredIndex = 2;
		DefaultCooldown = 0.0f;
		PlacementFootprintCells = FIntPoint(2, 2);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Defence/Bunker/China/China_AntiTankBunker_ZiS2_57mm/China_AntiTankBunker_ZiS2_57mm.China_AntiTankBunker_ZiS2_57mm")));
	}
};

/** Officer order: twin-autocannon anti-aircraft emplacement with a raised firing silhouette. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildAntiAircraftEmplacement : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildAntiAircraftEmplacement()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.AntiAircraftEmplacement"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("防空炮阵地"));
		Description = FText::FromString(TEXT("修建双联机关炮防空阵地，以高仰角火力保护附近部队和建筑。优先由附近同队步兵施工。"));
		PreferredIndex = 3;
		DefaultCooldown = 0.0f;
		LowValueCost = 220;
		HighValueCost = 40;
		PlacementFootprintCells = FIntPoint(2, 2);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Defence/AntiAircraftEmplacement/Common_AntiAircraftEmplacement_TwinAutocannon/SM_Common_AntiAircraftEmplacement_TwinAutocannon.SM_Common_AntiAircraftEmplacement_TwinAutocannon")));
	}
};

/** Officer order: hexagonal anti-tank trench and obstacle position. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildAntiTankObstacle : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildAntiTankObstacle()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.AntiTankObstacle"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("反坦克陷阱"));
		Description = FText::FromString(TEXT("第三排增益/减益工事：六边形反坦克障碍阵地，使敌方坦克减速并削弱装甲。优先由附近同队步兵施工。"));
		PreferredIndex = 10;
		DefaultCooldown = 0.0f;
		PlacementFootprintCells = FIntPoint(2, 2);
	}
};

/** Officer order: hexagonal barbed-wire position. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildBarbedWire : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildBarbedWire()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.BarbedWire"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("铁丝网"));
		Description = FText::FromString(TEXT("第三排增益/减益工事：六边形铁丝网阵地，用于减缓敌方步兵。优先由附近同队步兵施工。"));
		PreferredIndex = 11;
		DefaultCooldown = 0.0f;
		PlacementFootprintCells = FIntPoint(2, 2);
	}
};

// Col 1: Factory (Already in separate file, but we can consolidate here or keep separate. User has separate file.)
// We will focus on the others.

/**
 * Col 2: University (Tech)
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildUniversity : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildUniversity()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.University"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("建造大学"));
		Description = FText::FromString(TEXT("在选定网格建造大学，发展科技并提升科研效率。<n/><n/><RichText.Yellow>定位： 科技。</>"));
		PreferredIndex = 6; // Row 2, Col 2
		DefaultCooldown = 60.0f;
        bAllowAutoCast = false;
        
        LowValueCost = 150; // Minerals
        HighValueCost = 50; // Gas
		PlacementFootprintCells = FIntPoint(1, 1);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Economics/University/Common_University_College/SM_Common_University_College.SM_Common_University_College")));
	}
};

/**
 * Col 3: Barracks (Infantry)
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildBarracks : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildBarracks()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.Barracks"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("建造兵营"));
		Description = FText::FromString(TEXT("选择网格后，系统指派最近的空闲同队军官完成兵营施工。按住Shift可追加并行施工命令。<n/><n/><RichText.Yellow>定位： 步兵生产。</>"));
		PreferredIndex = 7; // Row 2, Col 3
		DefaultCooldown = 0.0f;
        
		LowValueCost = 150;
		PlacementFootprintCells = FIntPoint(3, 3);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Production/Barracks/Common_Barracks_FieldCamp/SM_Common_Barracks_FieldCamp.SM_Common_Barracks_FieldCamp")));
	}
};

/** Officer order: formal 4x4 tank factory with a concrete placement preview. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildTankFactory : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildTankFactory()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.TankFactory"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("建造坦克工厂"));
		Description = FText::FromString(TEXT("建造4×4格坦克工厂，生产本国装甲单位。按住Shift可追加并行施工命令。"));
		PreferredIndex = 1;
		LowValueCost = 400;
		HighValueCost = 100;
		PlacementFootprintCells = FIntPoint(4, 4);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Production/TankFactory/SM_TankFactory.SM_TankFactory")));
	}
};

/** Officer order: formal 4x4 vehicle and artillery works. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildVehicleFactory : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildVehicleFactory()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.VehicleFactory"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("建造战车工厂"));
		Description = FText::FromString(TEXT("建造4×4格战车与火炮车间，生产防空、反坦克炮、自行火炮和支援车辆。按住Shift可追加并行施工命令。"));
		PreferredIndex = 2;
		LowValueCost = 360;
		HighValueCost = 80;
		PlacementFootprintCells = FIntPoint(4, 4);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Production/VehicleFactory/Common_VehicleFactory_ArtilleryWorks/SM_Common_VehicleFactory_ArtilleryWorks.SM_Common_VehicleFactory_ArtilleryWorks")));
	}
};

/**
 * Col 4: Tank Factory (Mechanized) -> "Vehicles"
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildVehicleDepot : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildVehicleDepot()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.VehicleDepot"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("建造辎重营")); // Flavor: Supply/Vehicle Depot
		Description = FText::FromString(TEXT("生产卡车与轻型装甲车。<n/><n/><RichText.Yellow>定位： 机械化。</>"));
		PreferredIndex = 8; // Row 2, Col 4
		DefaultCooldown = 90.0f;
        
        LowValueCost = 200;
        HighValueCost = 100;
	}
};

/**
 * Col 5: Airfield
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildAirfield : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildAirfield()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.Airfield"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("建造机场"));
		Description = FText::FromString(TEXT("建造一座6×6格区域机场，作为空军生产与调度设施。<n/><n/><RichText.Yellow>定位： 空军产能。</>"));
		PreferredIndex = 9; // Row 2, Col 5
		DefaultCooldown = 0.0f;
		LowValueCost = 0;
		HighValueCost = 0;
		PlacementFootprintCells = FIntPoint(6, 6);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Production/Airfield/Common_Airfield_RegionalAirport/SM_Common_Airfield_RegionalAirport.SM_Common_Airfield_RegionalAirport")));
	}
};

/** City capacity building: an 8x8 coastal shipyard with a 2x4 open-water basin. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildShipyard : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildShipyard()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.Shipyard"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("建造船厂"));
		Description = FText::FromString(TEXT("建造一座8×8格船厂；朝海侧保留2×4格开放水槽。海岸层接入前仅校验陆地主锚点。<n/><n/><RichText.Yellow>定位： 海军产能。</>"));
		PreferredIndex = 10;
		DefaultCooldown = 0.0f;
		PlacementFootprintCells = FIntPoint(8, 8);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Production/Shipyard/Common_Shipyard_CoastalDrydock/SM_Common_Shipyard_CoastalDrydock.SM_Common_Shipyard_CoastalDrydock")));
	}
};

/** City capacity building: a compact research-panel entry distinct from a university. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildResearchCenter : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildResearchCenter()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.ResearchCenter"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("建造科研中心"));
		Description = FText::FromString(TEXT("建造一座2×2格应用科研中心，用于打开科技研发界面。大学仍负责三级科研效率升级。<n/><n/><RichText.Yellow>定位： 科技研发入口。</>"));
		PreferredIndex = 7;
		DefaultCooldown = 0.0f;
		PlacementFootprintCells = FIntPoint(2, 2);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Economics/ResearchCenter/Common_ResearchCenter_AppliedLaboratory/SM_Common_ResearchCenter_AppliedLaboratory.SM_Common_ResearchCenter_AppliedLaboratory")));
	}
};

/** City expansion node: a 2x2 utility hub with a pooled circular factory aura. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildIndustrialPark : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildIndustrialPark()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.IndustrialPark"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("建造工业园"));
		Description = FText::FromString(TEXT("建造一座2×2格公用工程中心，并显示半径40uu的圆形工业光环；工厂中心必须位于城市或同队工业园光环内。<n/><n/><RichText.Yellow>定位： 城市工业扩展。</>"));
		PreferredIndex = 8;
		DefaultCooldown = 0.0f;
		PlacementFootprintCells = FIntPoint(2, 2);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Economics/IndustrialPark/Common_IndustrialPark_UtilityHub/SM_Common_IndustrialPark_UtilityHub.SM_Common_IndustrialPark_UtilityHub")));
	}
};

/** City capacity building: a 3x3 petroleum refinery. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_BuildRefinery : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_BuildRefinery()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Build.Refinery"), false);
		TargetType = ERTSCommandTargetType::Location;
		DisplayName = FText::FromString(TEXT("建造炼油厂"));
		Description = FText::FromString(TEXT("建造一座3×3格炼油厂。OilDeposit资源层接入后再启用资源点强制校验。<n/><n/><RichText.Yellow>定位： 石油加工产能。</>"));
		PreferredIndex = 11;
		DefaultCooldown = 0.0f;
		PlacementFootprintCells = FIntPoint(3, 3);
		PlacementPreviewMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Building/Economics/Refinery/Common_Refinery_PetroleumWorks/SM_Common_Refinery_PetroleumWorks.SM_Common_Refinery_PetroleumWorks")));
	}
};

// --- ROW 3: Units ---

/** Col 1: Officer (SCV equivalent). */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_TrainOfficer : public URTSUnitProductionCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_TrainOfficer()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Train.Officer"), false);
		UnitId = FName(TEXT("Germany_Officer_LugerP08_SkinA"));
		UnitConfig = TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Army/Infantry/ToonWW2/Germany/Gen_Germany_Officer_LugerP08_SkinA/Germany_Officer_LugerP08_SkinA.Germany_Officer_LugerP08_SkinA")));
		DisplayName = FText::FromString(TEXT("招募军官"));
		Description = FText::FromString(TEXT("负责前线建设与指挥。<n/><n/><RichText.Yellow>定位： 工程/指挥。</>"));
		PreferredIndex = 0; // Row 1, Col 1
		DefaultCooldown = 0.0f;
		TrainTime = 18.0f;
		LowValueCost = 50;
	}
};

/**
 * Col 2: Militia
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_TrainMilitia : public URTSUnitProductionCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_TrainMilitia()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.Train.Militia"), false);
		UnitId = FName(TEXT("Germany_Rifleman_Kar98k"));
		UnitConfig = TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT(
			"/Game/Unit/Actor/Army/Infantry/ToonWW2/Germany/Gen_Germany_Rifleman_Kar98k/Germany_Rifleman_Kar98k.Germany_Rifleman_Kar98k")));
		DisplayName = FText::FromString(TEXT("动员民兵"));
		Description = FText::FromString(TEXT("基础防御单位。<n/><n/><RichText.Yellow>定位： 轻步兵。</>"));
		PreferredIndex = 1; // Row 1, Col 2
		DefaultCooldown = 0.0f;
		TrainTime = 10.0f;
		LowValueCost = 25;
	}
};

/** Row 1 / Col 5: transfer the selected city to the allied team clicked next. */
UCLASS()
class RTSINPUTSYSTEM_API URTSCmd_TransferCity : public URTSBuiltinCommandButton
{
	GENERATED_BODY()
public:
	URTSCmd_TransferCity()
	{
		CommandTag = FGameplayTag::RequestGameplayTag(FName("RTS.Command.City.Transfer"), false);
		TargetType = ERTSCommandTargetType::LocationOrTarget;
		DisplayName = FText::FromString(TEXT("转让城市"));
		Description = FText::FromString(TEXT("点击队友的单位或城市，将当前城市及其建筑转让给该队友。"));
		PreferredIndex = 4;
	}
};

/**
 * (New) City Command Grid - Concrete implementation for all Cities
 * Handles the player-facing City commands without allocating UObjects on
 * every UI refresh. Built-in command CDOs are immutable shared definitions.
 */
UCLASS()
class RTSINPUTSYSTEM_API URTSCityCommandGrid : public URTSCommandGridAsset
{
    GENERATED_BODY()

public:
    virtual TArray<URTSCommandButton*> GetAllButtons() const override
    {
        TArray<URTSCommandButton*> Result;
        
        Result.Reserve(11);
        Result.Add(GetMutableDefault<URTSCmd_TrainOfficer>());
        Result.Add(GetMutableDefault<URTSCmd_TrainMilitia>());
        Result.Add(GetMutableDefault<URTSCmd_EstablishCapital>());
        Result.Add(GetMutableDefault<URTSCmd_TransferCity>());
        Result.Add(GetMutableDefault<URTSCmd_BuildFactory>());
        Result.Add(GetMutableDefault<URTSCmd_BuildUniversity>());
        Result.Add(GetMutableDefault<URTSCmd_BuildResearchCenter>());
        Result.Add(GetMutableDefault<URTSCmd_BuildIndustrialPark>());
        Result.Add(GetMutableDefault<URTSCmd_BuildAirfield>());
        Result.Add(GetMutableDefault<URTSCmd_BuildShipyard>());
        Result.Add(GetMutableDefault<URTSCmd_BuildRefinery>());

        return Result;
    }
};

