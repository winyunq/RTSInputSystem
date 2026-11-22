// Copyright 2026 Winyunq. All Rights Reserved.

/**
 * @file RTSUnitTypeProtocol.cpp
 * @brief 从兵种配置和面板配置解析可用命令。
 * @details ExposesCommandButton 每次检查一个命令标签。已加载的面板资产直接检查按钮，槽位配置按覆盖顺序解析，待检查的子面板通过已访问集合去重。
 * @code{.unparsed}
 * 兵种协议 → 根面板或本地槽位
 *               ↓
 * 已加载面板按钮 或 逆序槽位覆盖
 *               ↓
 * 已登记命令命中 → true
 *               ↓ 未命中
 * 检查子面板与未占用的默认槽 → 结果
 * @endcode
 */
#include "RTSInputPanelSettings.h"
#include "RTSInputSystem.h"
#include "GameplayTagsManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Engine/World.h"

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
	if (!Settings || !CommandTag.IsValid()) return false;
	    static const FName DefaultNames[] = {FName(TEXT("RTS.Command.Move")), FName(TEXT("RTS.Command.Stop")),
	        FName(TEXT("RTS.Command.Hold")), FName(TEXT("RTS.Command.Patrol")), FName(TEXT("RTS.Command.Attack"))};
	    const auto DefaultSlot = [](const FGameplayTag& Tag) -> int32
	    {
	        for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(DefaultNames)); ++Index)
	            if (DefaultNames[Index] == Tag.GetTagName()) return Index;
	        return INDEX_NONE;
	    };
	    TArray<const FRTSCommandLoadoutDefinition*, TInlineAllocator<8>> Pending;
	    TSet<FName> Visited;
	    const auto Contains = [&](const TArray<FRTSMassUnitCommandSlotDefinition>& Slots, bool bDefaults,
	        const URTSCommandGridAsset* Grid, uint32 Occupied)
	    {
	        if (Grid && !Grid->Buttons.IsEmpty())
	        {
	            for (const URTSCommandButton* Button : Grid->Buttons)
	                if (Button && Button->CommandTag == CommandTag) return true;
	            return false;
	        }
	        for (int32 Index = Slots.Num() - 1; Index >= 0; --Index)
	        {
	            const auto& Slot = Slots[Index];
	            const FGameplayTag Tag = ResolveSlotCommandTag(Slot);
	            const int32 CoreSlot = DefaultSlot(Tag);
	            const int32 Position = CoreSlot != INDEX_NONE ? CoreSlot : Slot.SlotIndex;
	            if (!Tag.IsValid() || Position < 0 || Position >= 15) continue;
	            const uint32 Bit = 1u << Position;
	            if (Occupied & Bit) continue;
	            Occupied |= Bit;
	            if (Tag == CommandTag) return true;
	            if (const auto* Child = FindLoadout(Settings, Slot.SubMenuLoadoutId)) Pending.Add(Child);
	        }
	        const int32 Position = DefaultSlot(CommandTag);
	        return bDefaults && Position != INDEX_NONE && !(Occupied & (1u << Position));
	    };
	    const auto* Root = FindLoadout(Settings, ResolveCommandLoadoutId(Protocol));
	    if (Root) Pending.Add(Root);
	    else if (Contains(Protocol.CommandSlots, Protocol.bUseDefaultCommandGrid, Protocol.CommandGrid.Get(), 0)) return true;
	    while (!Pending.IsEmpty())
	    {
	        const auto* Loadout = Pending.Pop(EAllowShrinking::No);
	        if (Visited.Contains(Loadout->LoadoutId)) continue;
	        Visited.Add(Loadout->LoadoutId);
	        uint32 Occupied = 0;
	        if (!Loadout->BackToLoadoutId.IsNone() && Loadout->BackButtonSlotIndex >= 0 && Loadout->BackButtonSlotIndex < 15)
	            Occupied = 1u << Loadout->BackButtonSlotIndex;
	        if (Contains(Loadout->CommandSlots, Loadout->bIncludeDefaultUnitCommands, Loadout->CommandGrid.Get(), Occupied)) return true;
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
	const FName TankKey(TEXT("/Game/Unit/Actor/Army/Tank/Common/Gen_Common_Tank_TwinMachineGun_Handpainted/Common_Tank_TwinMachineGun_Handpainted.Common_Tank_TwinMachineGun_Handpainted"));
	const FRTSMassUnitTypeProtocol* Tank =
		RTSUnitTypeProtocol::FindByNetworkKeyOrSubType(Settings, TankKey, 52);
	if (TestNotNull(TEXT("Exact tank asset identity resolves a protocol"), Tank))
	{
		TestEqual(TEXT("Subtype collision resolves to the tank protocol"),
			Tank->TypeKey, FString(TEXT("Mass.Common.Armor.Common_Tank_TwinMachineGun_Handpainted")));
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
	TestEqual(TEXT("All active tank slots have exact identity protocols"), TankPaths.Num(), 164);
	return true;
}
#endif

namespace RTSUnitTypeProtocol
{
FSoftObjectPath ResolveCountryTankConfig(const UObject* WorldContext, int32 TeamIndex, const FSoftObjectPath& GenericConfig)
{
	const URTSInputPanelSettings* Settings = GetSettings();
	const FRTSMassUnitTypeProtocol* Generic = FindByNetworkKeyOrSubType(Settings, FName(*GenericConfig.ToString()), INDEX_NONE);
	if (!Generic || Generic->SubTypeIndex < 3801 || Generic->SubTypeIndex > 3814 || Generic->UnitAssetPath != GenericConfig.ToString())
	{
		return GenericConfig;
	}
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) return GenericConfig;
	const FString Theater = World->URL.GetOption(TEXT("Theater="), TEXT(""));
	const FString MapPackage = World->GetOutermost()->GetName();
	const bool bEurope = Theater == TEXT("Europe") || (Theater.IsEmpty() && MapPackage.Contains(TEXT("/Map/Europe/")));
	const bool bEastAsia = Theater == TEXT("EastAsia") || (Theater.IsEmpty() && MapPackage.Contains(TEXT("/Map/EastAsia/")));
	int32 CountryRow = INDEX_NONE;
	if (bEurope)
	{
		switch (TeamIndex)
		{
		case 1: CountryRow = 0; break;
		case 8: CountryRow = 1; break;
		case 15: CountryRow = 2; break;
		case 2: CountryRow = 3; break;
		case 3: CountryRow = 5; break;
		case 4: CountryRow = 6; break;
		case 6: CountryRow = 8; break;
		case 7: CountryRow = 9; break;
		case 13: CountryRow = 10; break;
		case 16: CountryRow = 11; break;
		case 17: CountryRow = 12; break;
		case 26: CountryRow = 13; break;
		case 27: CountryRow = 14; break;
		default: return GenericConfig;
		}
	}
	else if (bEastAsia)
	{
		switch (TeamIndex)
		{
		case 1:
		case 2:
		case 4:
		case 5: CountryRow = 4; break;
		case 3: CountryRow = 7; break;
		default: return GenericConfig;
		}
	}
	else
	{
		return GenericConfig;
	}
	static constexpr int32 Variants[15][14] = {
		{4233, 4224, 4230, 4221, INDEX_NONE, 4227, 4209, 4212, 4206, 4217, 4203, 4240, 4243, 4236},
		{4234, 4225, 4231, 4222, 4215, 4228, 4210, 4213, 4207, 4218, 4204, 4241, 4244, 4238},
		{4235, 4226, 4232, 4223, 4216, 4229, 4211, 4214, 4208, 4219, 4205, 4242, 4245, 4239},
		{4314, 4315, 4316, 4317, 4318, 4319, 4320, 4321, 4322, 4323, 4324, 4325, 4326, 4327},
		{4300, 4301, 4302, 4303, 4304, 4305, 4306, 4307, 4308, 4309, 4310, 4311, 4312, 4313},
		{4330, 4332, 4333, 4334, 4335, 4331, 4336, 4337, 4338, 4339, 4340, 4341, 4342, 4343},
		{4367, 4369, 4370, 4371, INDEX_NONE, 4368, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, 4373, 4374, 4375, INDEX_NONE},
		{4349, 4351, 4352, 4353, 4354, 4350, 4355, 4356, 4357, 4358, 4359, 4360, 4361, 4362},
		{INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, 4397, INDEX_NONE, INDEX_NONE, INDEX_NONE},
		{4381, 4384, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, 4382, INDEX_NONE, INDEX_NONE},
		{INDEX_NONE, 4386, 4387, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, 4388, INDEX_NONE, INDEX_NONE, INDEX_NONE},
		{INDEX_NONE, 4407, 4408, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE},
		{INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, 4406, INDEX_NONE, INDEX_NONE},
		{INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, 4394, 4395, INDEX_NONE},
		{INDEX_NONE, 4389, 4390, 4391, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, 4392, INDEX_NONE, INDEX_NONE, INDEX_NONE}
	};
	const int32 VariantSubType = Variants[CountryRow][Generic->SubTypeIndex - 3801];
	const FRTSMassUnitTypeProtocol* Variant = VariantSubType != INDEX_NONE ? FindBySubType(Settings, VariantSubType) : nullptr;
	if (!Variant || Variant->TypeKey != Generic->TypeKey || Variant->ProductionCommand != Generic->ProductionCommand
		|| !Variant->UnitAssetPath.StartsWith(TEXT("/Game/Unit/Actor/Army/Tank/")))
	{
		return GenericConfig;
	}
	return FSoftObjectPath(Variant->UnitAssetPath);
}
}
