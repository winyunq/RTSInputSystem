// Copyright 2024 Jesus Bracho All Rights Reserved.

#include "RTSInputSystem.h"
#include "RTSInputPanelSettings.h"
#include "GameplayTagsManager.h"

#define LOCTEXT_NAMESPACE "FRTSInputSystemModule"

DEFINE_LOG_CATEGORY(LogRTSInputSystem);

void FRTSInputSystemModule::StartupModule()
{
	UGameplayTagsManager& TagsManager = UGameplayTagsManager::Get();
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Move")), TEXT("Default RTS unit move command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Attack")), TEXT("Default RTS unit attack command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Stop")), TEXT("Default RTS unit stop command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Hold")), TEXT("Default RTS unit hold command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Patrol")), TEXT("Default RTS unit patrol command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.Factory")), TEXT("Default RTS build command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.University")), TEXT("Default RTS build command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.Barracks")), TEXT("Default RTS build command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.TankFactory")), TEXT("Officer constructs a 4x4 tank factory."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.VehicleFactory")), TEXT("Officer constructs a 4x4 vehicle and artillery factory."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.VehicleDepot")), TEXT("Default RTS build command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.Airfield")), TEXT("Default RTS build command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.Shipyard")), TEXT("City constructs an 8x8 coastal shipyard."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.ResearchCenter")), TEXT("City constructs a 2x2 research center."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.IndustrialPark")), TEXT("City constructs a 2x2 industrial expansion hub."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.Refinery")), TEXT("City constructs a 3x3 petroleum refinery."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.City.EstablishCapital")), TEXT("Establish the selected captured city as capital."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.City.BuildBase")), TEXT("Build a military base inside the selected captured city."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.City.Transfer")), TEXT("Transfer the selected city to an allied team."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.Defense")), TEXT("Officer orders nearby infantry to construct a defensive building."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.GarrisonBunker")), TEXT("Officer construction: GarrisonBunker."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.MortarBunker")), TEXT("Officer construction: MortarEmplacement."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.CoastalBattery")), TEXT("Officer construction: CoastalBattery."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.FieldCover")), TEXT("Officer orders nearby infantry to construct the merged sandbag field cover."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.MachineGunBunker")), TEXT("Officer orders nearby infantry to construct a turreted machine-gun bunker."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.AntiTankBunker")), TEXT("Officer orders nearby infantry to construct a turreted anti-tank bunker."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.AntiAircraftEmplacement")), TEXT("Officer orders nearby infantry to construct a twin-autocannon anti-aircraft emplacement."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.AntiTankObstacle")), TEXT("Officer orders nearby infantry to construct anti-tank obstacles."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.BarbedWire")), TEXT("Officer orders nearby infantry to construct barbed wire."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Build.Production")), TEXT("Officer constructs and enters a production building."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Menu.Build.Defense")), TEXT("Open the officer defensive-construction command card."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Menu.Build.Production")), TEXT("Open the officer production-construction command card."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Menu.Back")), TEXT("Return to the parent command card."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Train.Officer")), TEXT("Default RTS train command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Train.Militia")), TEXT("Default RTS train command"));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Navy.OpenProduction")), TEXT("Open naval production panel."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Air.OpenProduction")), TEXT("Open air production panel."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Decision.OpenPanel")), TEXT("Open player decision panel."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Market.OpenForeignUnits")), TEXT("Open foreign arms market panel."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Research.OpenPanel")), TEXT("Open research panel."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Command.Structure.Upgrade")), TEXT("Upgrade an owned economy structure after its required technology is researched."));

	// Country assets are implementations of these universal unit classes. Unit classes
	// supply default button cards; a nation's subtype index never creates a skill.
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.UnitClass.Officer")), TEXT("Universal officer class; all national officer implementations inherit officer capabilities."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.UnitClass.Infantry")), TEXT("Universal infantry class."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.UnitClass.Armor")), TEXT("Universal armored fighting vehicle class."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.UnitClass.Artillery")), TEXT("Universal artillery class."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.UnitClass.Structure")), TEXT("Universal structure class."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.UnitClass.Air")), TEXT("Universal air unit class."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.UnitClass.Naval")), TEXT("Universal naval unit class."));

	// Mass-first hierarchical quick-selection protocol shared by TopSelect, UMG query buttons,
	// Actor-backed selectables, and Mass unit protocol records.
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Army")), TEXT("All controllable military units."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Army.Ground")), TEXT("All ground military units."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Army.Ground.Infantry")), TEXT("Infantry units."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Army.Ground.Armor")), TEXT("Armored units and combat vehicles."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Army.Ground.Artillery")), TEXT("Artillery and indirect-fire units."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Army.Ground.Engineer")), TEXT("Engineer units."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Army.Air")), TEXT("Aircraft."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Army.Naval")), TEXT("Naval units."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Worker")), TEXT("Worker or builder units."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Structure")), TEXT("All selectable structures."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Structure.Defense")), TEXT("Defensive structures."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Structure.City")), TEXT("Cities."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Structure.University")), TEXT("Universities."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Structure.Research")), TEXT("Research structures."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Structure.Government")), TEXT("Government structures."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Structure.Port")), TEXT("Ports and shipyards."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Structure.Airport")), TEXT("Airports and airfields."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Structure.Barracks")), TEXT("Barracks."));
	TagsManager.AddNativeGameplayTag(FName(TEXT("RTS.Selection.Structure.MilitaryCamp")), TEXT("Military camps and bases."));

	RTSUnitTypeProtocol::GetSettings();
}

void FRTSInputSystemModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FRTSInputSystemModule, RTSInputSystem)
