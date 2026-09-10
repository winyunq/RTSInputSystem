// Copyright 2026 Winyunq. All Rights Reserved.

#include "RTSInputPanelSettings.h"
#include "RTSInputSystem.h"
#include "GameplayTagsManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	int32 ResolveProtocolSubTypeIndex(const FRTSMassUnitTypeProtocol& Protocol, int32 ArrayIndex)
	{
		return Protocol.SubTypeIndex != INDEX_NONE ? Protocol.SubTypeIndex : ArrayIndex;
	}

	const FRTSCommandLoadoutDefinition* FindLoadout(
		const URTSInputPanelSettings* Settings,
		FName LoadoutId)
	{
		if (!Settings || LoadoutId.IsNone())
		{
			return nullptr;
		}

		return Settings->CommandLoadouts.FindByPredicate([LoadoutId](const FRTSCommandLoadoutDefinition& Loadout)
		{
			return Loadout.LoadoutId == LoadoutId;
		});
	}

	void ApplyBuiltInBuilderDefenseLayout(URTSInputPanelSettings* Settings)
	{
		if (!Settings)
		{
			return;
		}

		FRTSCommandLoadoutDefinition* DefenseLoadout = Settings->CommandLoadouts.FindByPredicate(
			[](const FRTSCommandLoadoutDefinition& Loadout)
			{
				return Loadout.LoadoutId == FName(TEXT("BuilderDefense"));
			});
		if (!DefenseLoadout)
		{
			return;
		}

		for (FRTSMassUnitCommandSlotDefinition& Slot : DefenseLoadout->CommandSlots)
		{
			const FName CommandName = Slot.CommandTag.IsValid()
				? Slot.CommandTag.GetTagName()
				: Slot.CommandTagName;
			if (CommandName == FName(TEXT("RTS.Command.Build.FieldCover")))
			{
				Slot.SlotIndex = 10;
				Slot.DisplayName = TEXT("沙袋");
				Slot.Hotkey = FName(TEXT("Z"));
			}
			else if (CommandName == FName(TEXT("RTS.Command.Build.AntiAircraftEmplacement")))
			{
				Slot.SlotIndex = 5;
				Slot.DisplayName = TEXT("防空");
				Slot.Hotkey = FName(TEXT("A"));
			}
			else if (CommandName == FName(TEXT("RTS.Command.Build.AntiTankObstacle")))
			{
				Slot.SlotIndex = 12;
				Slot.DisplayName = TEXT("反坦克陷阱");
				Slot.Hotkey = FName(TEXT("C"));
			}
			else if (CommandName == FName(TEXT("RTS.Command.Build.BarbedWire")))
			{
				Slot.SlotIndex = 11;
				Slot.Hotkey = FName(TEXT("X"));
			}
		}
	}

	FGameplayTag EnsureInputTag(FName TagName, const TCHAR* Comment)
	{
		FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, false);
		if (!Tag.IsValid())
		{
			Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(TagName, Comment);
		}
		return Tag;
	}

	void ApplyBuiltInBuilderProductionLayout(URTSInputPanelSettings* Settings)
	{
		if (!Settings)
		{
			return;
		}

		const FName TankFactoryTagName(TEXT("RTS.Command.Build.TankFactory"));
		const FGameplayTag TankFactoryTag = EnsureInputTag(
			TankFactoryTagName,
			TEXT("Officer constructs a 4x4 tank factory."));
		const FName VehicleFactoryTagName(TEXT("RTS.Command.Build.VehicleFactory"));
		const FGameplayTag VehicleFactoryTag = EnsureInputTag(
			VehicleFactoryTagName,
			TEXT("Officer constructs a 4x4 vehicle and artillery factory."));
		FRTSCommandLoadoutDefinition* ProductionLoadout = Settings->CommandLoadouts.FindByPredicate(
			[](const FRTSCommandLoadoutDefinition& Loadout)
			{
				return Loadout.LoadoutId == FName(TEXT("BuilderProduction"));
			});
		if (!ProductionLoadout)
		{
			FRTSCommandLoadoutDefinition NewLoadout;
			NewLoadout.LoadoutId = FName(TEXT("BuilderProduction"));
			NewLoadout.bIncludeDefaultUnitCommands = false;
			NewLoadout.BackToLoadoutId = FName(TEXT("Builder"));
			NewLoadout.BackButtonDisplayName = TEXT("返回");
			NewLoadout.BackButtonDescription = TEXT("返回军官主命令卡。");
			NewLoadout.BackButtonHotkey = FName(TEXT("B"));
			ProductionLoadout = &Settings->CommandLoadouts.Add_GetRef(MoveTemp(NewLoadout));
		}

		FRTSMassUnitCommandSlotDefinition* TankFactorySlot = ProductionLoadout->CommandSlots.FindByPredicate(
			[TankFactoryTagName](const FRTSMassUnitCommandSlotDefinition& Slot)
			{
				const FName CommandName = Slot.CommandTag.IsValid()
					? Slot.CommandTag.GetTagName()
					: Slot.CommandTagName;
				return CommandName == TankFactoryTagName;
			});
		if (!TankFactorySlot)
		{
			TankFactorySlot = &ProductionLoadout->CommandSlots.AddDefaulted_GetRef();
		}
		TankFactorySlot->SlotIndex = 2;
		TankFactorySlot->CommandTag = TankFactoryTag;
		TankFactorySlot->CommandTagName = TankFactoryTagName;
		TankFactorySlot->DisplayName = TEXT("坦克工厂");
		TankFactorySlot->Description = TEXT("指定4×4格占地后，系统指派最近的空闲同队军官，用28个游戏日完成坦克工厂。按住Shift可追加并行施工地点。");
		TankFactorySlot->TargetType = ERTSCommandTargetType::Location;
		TankFactorySlot->Hotkey = FName(TEXT("E"));

		FRTSMassUnitCommandSlotDefinition* VehicleFactorySlot = ProductionLoadout->CommandSlots.FindByPredicate(
			[VehicleFactoryTagName](const FRTSMassUnitCommandSlotDefinition& Slot)
			{
				const FName CommandName = Slot.CommandTag.IsValid()
					? Slot.CommandTag.GetTagName()
					: Slot.CommandTagName;
				return CommandName == VehicleFactoryTagName;
			});
		if (!VehicleFactorySlot)
		{
			VehicleFactorySlot = &ProductionLoadout->CommandSlots.AddDefaulted_GetRef();
		}
		VehicleFactorySlot->SlotIndex = 1;
		VehicleFactorySlot->CommandTag = VehicleFactoryTag;
		VehicleFactorySlot->CommandTagName = VehicleFactoryTagName;
		VehicleFactorySlot->DisplayName = TEXT("战车工厂");
		VehicleFactorySlot->Description = TEXT("指定4×4格占地后，系统指派最近的空闲同队军官，用28个游戏日完成战车工厂。按住Shift可追加并行施工地点。");
		VehicleFactorySlot->TargetType = ERTSCommandTargetType::Location;
		VehicleFactorySlot->Hotkey = FName(TEXT("W"));
	}

	FGameplayTag ResolveSlotCommandTag(const FRTSMassUnitCommandSlotDefinition& Slot)
	{
		if (Slot.CommandTag.IsValid())
		{
			return Slot.CommandTag;
		}

		return Slot.CommandTagName.IsNone()
			? FGameplayTag()
			: FGameplayTag::RequestGameplayTag(Slot.CommandTagName, false);
	}

	bool GridContainsCommand(const URTSCommandGridAsset* Grid, const FGameplayTag& CommandTag)
	{
		if (!Grid || !CommandTag.IsValid())
		{
			return false;
		}

		for (const URTSCommandButton* Button : Grid->GetAllButtons())
		{
			if (Button && Button->CommandTag.MatchesTagExact(CommandTag))
			{
				return true;
			}
		}

		return false;
	}

	bool IsDefaultUnitCommand(const FGameplayTag& CommandTag)
	{
		const FName TagName = CommandTag.GetTagName();
		return TagName == FName(TEXT("RTS.Command.Move"))
			|| TagName == FName(TEXT("RTS.Command.Attack"))
			|| TagName == FName(TEXT("RTS.Command.Stop"))
			|| TagName == FName(TEXT("RTS.Command.Hold"))
			|| TagName == FName(TEXT("RTS.Command.Patrol"));
	}

	bool LoadoutContainsCommandRecursive(
		const URTSInputPanelSettings* Settings,
		FName LoadoutId,
		const FGameplayTag& CommandTag,
		TSet<FName>& VisitedLoadouts)
	{
		if (LoadoutId.IsNone() || VisitedLoadouts.Contains(LoadoutId))
		{
			return false;
		}
		VisitedLoadouts.Add(LoadoutId);

		const FRTSCommandLoadoutDefinition* Loadout = FindLoadout(Settings, LoadoutId);
		if (!Loadout)
		{
			return false;
		}

		if (Loadout->bIncludeDefaultUnitCommands && IsDefaultUnitCommand(CommandTag))
		{
			return true;
		}

		if (!Loadout->CommandGrid.IsNull()
			&& GridContainsCommand(Loadout->CommandGrid.LoadSynchronous(), CommandTag))
		{
			return true;
		}

		for (const FRTSMassUnitCommandSlotDefinition& Slot : Loadout->CommandSlots)
		{
			if (ResolveSlotCommandTag(Slot).MatchesTagExact(CommandTag))
			{
				return true;
			}

			if (!Slot.SubMenuLoadoutId.IsNone()
				&& LoadoutContainsCommandRecursive(
					Settings,
					Slot.SubMenuLoadoutId,
					CommandTag,
					VisitedLoadouts))
			{
				return true;
			}
		}

		return false;
	}
}

const URTSInputPanelSettings* RTSUnitTypeProtocol::GetSettings()
{
	URTSInputPanelSettings* Settings = GetMutableDefault<URTSInputPanelSettings>();
	if (!Settings)
	{
		return nullptr;
	}
	if (!Settings->MassUnitTypeProtocols.IsEmpty()
		|| !Settings->CommandLoadouts.IsEmpty())
	{
		ApplyBuiltInBuilderDefenseLayout(Settings);
		ApplyBuiltInBuilderProductionLayout(Settings);
		return Settings;
	}

	// Resolve the plugin by its technical name. The settings class uses the
	// RTSInputSystem custom config category. UE does not merge that custom plugin
	// category in every launch path, so recover it at the first actual consumer too.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("RTSInputSystem"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogRTSInputSystem, Warning,
			TEXT("Could not locate RTSInputSystem while loading RTS unit protocols."));
		ApplyBuiltInBuilderDefenseLayout(Settings);
		ApplyBuiltInBuilderProductionLayout(Settings);
		return Settings;
	}

	FString PluginConfigPath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Config"),
		TEXT("DefaultRTSInputSystem.ini"));
	PluginConfigPath = FConfigCacheIni::NormalizeConfigIniPath(PluginConfigPath);

	FConfigFile PluginDefaults;
	if (!PluginDefaults.Combine(PluginConfigPath, true))
	{
		UE_LOG(LogRTSInputSystem, Warning,
			TEXT("Could not read RTS unit protocol defaults from %s."),
			*PluginConfigPath);
		ApplyBuiltInBuilderDefenseLayout(Settings);
		ApplyBuiltInBuilderProductionLayout(Settings);
		return Settings;
	}

	UObject::FLoadConfigParams LoadParams;
	LoadParams.ConfigClass = URTSInputPanelSettings::StaticClass();
	LoadParams.OverrideFile = &PluginDefaults;
	Settings->LoadConfig(LoadParams);
	ApplyBuiltInBuilderDefenseLayout(Settings);
	ApplyBuiltInBuilderProductionLayout(Settings);
	UE_LOG(LogRTSInputSystem, Display,
		TEXT("Loaded RTS unit protocols and command cards from %s (Protocols=%d, Loadouts=%d)."),
		*PluginConfigPath,
		Settings->MassUnitTypeProtocols.Num(),
		Settings->CommandLoadouts.Num());
	return Settings;
}

const FRTSMassUnitTypeProtocol* RTSUnitTypeProtocol::FindBySubType(
	const URTSInputPanelSettings* Settings,
	int32 SubTypeIndex)
{
	if (!Settings)
	{
		return nullptr;
	}

	for (int32 Index = 0; Index < Settings->MassUnitTypeProtocols.Num(); ++Index)
	{
		const FRTSMassUnitTypeProtocol& Protocol = Settings->MassUnitTypeProtocols[Index];
		if (ResolveProtocolSubTypeIndex(Protocol, Index) == SubTypeIndex)
		{
			return &Protocol;
		}
	}

	return nullptr;
}

const FRTSMassUnitTypeProtocol* RTSUnitTypeProtocol::FindByNetworkKeyOrSubType(
	const URTSInputPanelSettings* Settings,
	FName NetworkKey,
	int32 SubTypeIndex)
{
	if (Settings && !NetworkKey.IsNone())
	{
		const FString KeyString = NetworkKey.ToString();
		for (const FRTSMassUnitTypeProtocol& Protocol : Settings->MassUnitTypeProtocols)
		{
			if (!Protocol.UnitAssetPath.IsEmpty()
				&& Protocol.UnitAssetPath.Equals(KeyString, ESearchCase::CaseSensitive))
			{
				return &Protocol;
			}
		}
	}

	return FindBySubType(Settings, SubTypeIndex);
}

FName RTSUnitTypeProtocol::ResolveCommandLoadoutId(const FRTSMassUnitTypeProtocol& Protocol)
{
	const FGameplayTag OfficerClassTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.UnitClass.Officer")), false);
	if (OfficerClassTag.IsValid()
		&& Protocol.UnitTypeTag.IsValid()
		&& Protocol.UnitTypeTag.MatchesTagExact(OfficerClassTag))
	{
		// This attaches the Builder button card to the generic Officer class. The buttons,
		// not this class check, remain the capabilities consulted by command execution.
		return FName(TEXT("Builder"));
	}

	return Protocol.CommandLoadoutId;
}

bool RTSUnitTypeProtocol::ExposesCommandButton(
	const URTSInputPanelSettings* Settings,
	const FRTSMassUnitTypeProtocol& Protocol,
	const FGameplayTag& CommandTag)
{
	if (!Settings || !CommandTag.IsValid())
	{
		return false;
	}

	const FName LoadoutId = ResolveCommandLoadoutId(Protocol);
	if (!LoadoutId.IsNone())
	{
		TSet<FName> VisitedLoadouts;
		if (LoadoutContainsCommandRecursive(Settings, LoadoutId, CommandTag, VisitedLoadouts))
		{
			return true;
		}
	}

	if (Protocol.bUseDefaultCommandGrid && IsDefaultUnitCommand(CommandTag))
	{
		return true;
	}

	if (!Protocol.CommandGrid.IsNull()
		&& GridContainsCommand(Protocol.CommandGrid.LoadSynchronous(), CommandTag))
	{
		return true;
	}

	for (const FRTSMassUnitCommandSlotDefinition& Slot : Protocol.CommandSlots)
	{
		if (ResolveSlotCommandTag(Slot).MatchesTagExact(CommandTag))
		{
			return true;
		}

		if (!Slot.SubMenuLoadoutId.IsNone())
		{
			TSet<FName> VisitedLoadouts;
			if (LoadoutContainsCommandRecursive(
				Settings,
				Slot.SubMenuLoadoutId,
				CommandTag,
				VisitedLoadouts))
			{
				return true;
			}
		}
	}

	return false;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSOfficerCommandCardTest,
	"Winyunq.RTSInputSystem.OfficerCommandCard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSOfficerCommandCardTest::RunTest(const FString& Parameters)
{
	const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();
	if (!TestNotNull(TEXT("RTS input panel settings"), Settings))
	{
		return false;
	}

	TestTrue(TEXT("Unit protocols were loaded"), !Settings->MassUnitTypeProtocols.IsEmpty());
	TestTrue(TEXT("Command loadouts were loaded"), !Settings->CommandLoadouts.IsEmpty());

	const FRTSMassUnitTypeProtocol* Officer = RTSUnitTypeProtocol::FindBySubType(Settings, 3206);
	if (!TestNotNull(TEXT("German officer subtype 3206 uses a protocol"), Officer))
	{
		return false;
	}

	const FGameplayTag DefenseMenu = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.Command.Menu.Build.Defense")), false);
	const FGameplayTag ProductionMenu = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.Command.Menu.Build.Production")), false);
	const FGameplayTag Barracks = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.Command.Build.Barracks")), false);
	const FGameplayTag TankFactory = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.Command.Build.TankFactory")), false);
	const FGameplayTag VehicleFactory = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.Command.Build.VehicleFactory")), false);

	TestTrue(TEXT("Officer exposes Build Buildings button"),
		RTSUnitTypeProtocol::ExposesCommandButton(Settings, *Officer, DefenseMenu));
	TestTrue(TEXT("Officer exposes Build Production button"),
		RTSUnitTypeProtocol::ExposesCommandButton(Settings, *Officer, ProductionMenu));
	TestTrue(TEXT("Officer exposes Barracks through the production submenu"),
		RTSUnitTypeProtocol::ExposesCommandButton(Settings, *Officer, Barracks));
	TestTrue(TEXT("Officer exposes Tank Factory through the production submenu"),
		RTSUnitTypeProtocol::ExposesCommandButton(Settings, *Officer, TankFactory));
	TestTrue(TEXT("Officer exposes Vehicle Factory through the production submenu"),
		RTSUnitTypeProtocol::ExposesCommandButton(Settings, *Officer, VehicleFactory));

	const FRTSCommandLoadoutDefinition* ProductionLoadout = FindLoadout(
		Settings,
		FName(TEXT("BuilderProduction")));
	if (TestNotNull(TEXT("Officer production submenu exists"), ProductionLoadout))
	{
		const FRTSMassUnitCommandSlotDefinition* TankFactorySlot = ProductionLoadout->CommandSlots.FindByPredicate(
			[](const FRTSMassUnitCommandSlotDefinition& Candidate)
			{
				const FName CandidateName = Candidate.CommandTag.IsValid()
					? Candidate.CommandTag.GetTagName()
					: Candidate.CommandTagName;
				return CandidateName == FName(TEXT("RTS.Command.Build.TankFactory"));
			});
		if (TestNotNull(TEXT("Tank Factory build command exists"), TankFactorySlot))
		{
			TestEqual(TEXT("Tank Factory build command uses production submenu slot 2"), TankFactorySlot->SlotIndex, 2);
		}

		const FRTSMassUnitCommandSlotDefinition* VehicleFactorySlot = ProductionLoadout->CommandSlots.FindByPredicate(
			[](const FRTSMassUnitCommandSlotDefinition& Candidate)
			{
				const FName CandidateName = Candidate.CommandTag.IsValid()
					? Candidate.CommandTag.GetTagName()
					: Candidate.CommandTagName;
				return CandidateName == FName(TEXT("RTS.Command.Build.VehicleFactory"));
			});
		if (TestNotNull(TEXT("Vehicle Factory build command exists"), VehicleFactorySlot))
		{
			TestEqual(TEXT("Vehicle Factory build command uses production submenu slot 1"), VehicleFactorySlot->SlotIndex, 1);
		}
	}

	const FRTSCommandLoadoutDefinition* DefenseLoadout = FindLoadout(
		Settings,
		FName(TEXT("BuilderDefense")));
	if (TestNotNull(TEXT("Officer defense submenu exists"), DefenseLoadout))
	{
		auto TestCommandSlot = [this, DefenseLoadout](const TCHAR* TagName, int32 ExpectedSlot)
		{
			const FRTSMassUnitCommandSlotDefinition* Slot = DefenseLoadout->CommandSlots.FindByPredicate(
				[TagName](const FRTSMassUnitCommandSlotDefinition& Candidate)
				{
					const FName CandidateName = Candidate.CommandTag.IsValid()
						? Candidate.CommandTag.GetTagName()
						: Candidate.CommandTagName;
					return CandidateName == FName(TagName);
				});
			if (TestNotNull(FString::Printf(TEXT("Defense command %s exists"), TagName), Slot))
			{
				TestEqual(FString::Printf(TEXT("Defense command %s slot"), TagName), Slot->SlotIndex, ExpectedSlot);
			}
		};
		TestCommandSlot(TEXT("RTS.Command.Build.GarrisonBunker"), 0);
		TestCommandSlot(TEXT("RTS.Command.Build.MortarBunker"), 3);
		TestCommandSlot(TEXT("RTS.Command.Build.CoastalBattery"), 6);
		TestCommandSlot(TEXT("RTS.Command.Build.FieldCover"), 10);
		TestCommandSlot(TEXT("RTS.Command.Build.MachineGunBunker"), 1);
		TestCommandSlot(TEXT("RTS.Command.Build.AntiTankBunker"), 2);
		TestCommandSlot(TEXT("RTS.Command.Build.AntiAircraftEmplacement"), 5);
		TestCommandSlot(TEXT("RTS.Command.Build.AntiTankObstacle"), 12);
		TestCommandSlot(TEXT("RTS.Command.Build.BarbedWire"), 11);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRTSTankAssetIdentityProtocolTest,
	"Winyunq.RTSInputSystem.TankAssetIdentityProtocol",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSTankAssetIdentityProtocolTest::RunTest(const FString& Parameters)
{
	const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();
	if (!TestNotNull(TEXT("RTS input panel settings"), Settings))
	{
		return false;
	}

	const FName TankKey(TEXT("/Game/Unit/Actor/Army/Tank/France/MCPGenerated/France_Renault_FT17_MG/France_Renault_FT17_MG.France_Renault_FT17_MG"));
	const FRTSMassUnitTypeProtocol* Tank =
		RTSUnitTypeProtocol::FindByNetworkKeyOrSubType(Settings, TankKey, 52);
	if (TestNotNull(TEXT("Exact tank asset identity resolves a protocol"), Tank))
	{
		TestEqual(TEXT("Subtype collision resolves to the tank protocol"),
			Tank->TypeKey, FString(TEXT("Mass.France.Armor.Tank.RenaultFT17MG")));
	}

	const FRTSMassUnitTypeProtocol* Legacy =
		RTSUnitTypeProtocol::FindByNetworkKeyOrSubType(Settings, NAME_None, 52);
	if (TestNotNull(TEXT("Legacy subtype fallback remains available"), Legacy))
	{
		TestEqual(TEXT("Legacy subtype 52 remains the research center"),
			Legacy->TypeKey, FString(TEXT("Mass.Structure.ResearchCenter")));
	}

	TSet<FString> TankPaths;
	for (const FRTSMassUnitTypeProtocol& Protocol : Settings->MassUnitTypeProtocols)
	{
		if (Protocol.UnitAssetPath.StartsWith(TEXT("/Game/Unit/Actor/Army/Tank/")))
		{
			TestTrue(TEXT("Tank asset protocol path is unique"),
				!TankPaths.Contains(Protocol.UnitAssetPath));
			TankPaths.Add(Protocol.UnitAssetPath);
		}
	}
	TestEqual(TEXT("All active tank slots have exact identity protocols"), TankPaths.Num(), 45);
	return true;
}
#endif
