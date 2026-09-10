#include "RTSSelectionSubsystem.h"
#include "GenericTeamAgentInterface.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/RTSCommandProgressProvider.h"
#include "RTSInputPanelSettings.h"
#include "RTSSelectable.h"
#include "RTSCommandSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Interfaces/RTSCommandInterface.h"
#include "Data/RTSCommandGridAsset.h"
#include "Data/RTSCommandButton.h"
#include "Data/RTSCmd_SubMenu.h"
#include "Commands/RTSCityCommands.h"
#include "Commands/RTSUnitCommands.h"
#include "Components/MassBattleAgentComponent.h"
#include "DataAssets/MassBattleAgentConfigDataAsset.h"
#include "Fragments/Health.h"
#include "Fragments/Attack.h"
#include "Fragments/Damage.h"
#include "Fragments/Defence.h"
#include "Fragments/Network.h"
#include "Fragments/Select.h"
#include "Fragments/SubType.h"
#include "Fragments/Team.h"
#include "Fragments/Transform.h"
#include "MassAPIFuncLib.h"
#include "Tasks/MassBattleBPTaskAgentsMoveTo.h"
#include "Tasks/MassBattleBPTaskAgentsChaseAttack.h"
#include "Interfaces/MassBattleAgentInterface.h"
#include "FuncLibs/MassBattleFuncLib.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Algo/AllOf.h"

DEFINE_LOG_CATEGORY(LogORTSSelection);

void URTSSelectionSubsystem::RequestCommandRefresh()
{
	if (bCommandRefreshInProgress)
	{
		return;
	}

	TGuardValue<bool> DispatchGuard(bCommandRefreshInProgress, true);
	OnCommandRefreshRequested.Broadcast();
}

void URTSSelectionSubsystem::NotifyCommandProgressChanged(
	AActor* ProgressProvider)
{
	if (!ProgressProvider)
	{
		return;
	}

	if (bCommandProgressNotificationInProgress)
	{
		return;
	}

	TGuardValue<bool> DispatchGuard(
		bCommandProgressNotificationInProgress,
		true);
	OnCommandProgressChanged.Broadcast(ProgressProvider);
}

namespace
{
	constexpr int32 MaxSynchronousFormationEntities = 512;
	constexpr int32 MaxControlGroupFocusSamples = 32;
	constexpr float ControlGroupFocusRetainedFraction = 0.60f;
	constexpr int32 MinControlGroupIndex = 0;
	constexpr int32 MaxControlGroupIndex = 9;

	const int32 ControlGroupDisplayOrder[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };

	void AddSelectionTag(FGameplayTagContainer& Tags, const TCHAR* TagName)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TagName), false);
		if (Tag.IsValid())
		{
			Tags.AddTag(Tag);
		}
	}

	bool ContainsAny(const FString& SearchText, std::initializer_list<const TCHAR*> Terms)
	{
		for (const TCHAR* Term : Terms)
		{
			if (SearchText.Contains(Term))
			{
				return true;
			}
		}
		return false;
	}

	bool MatchesRequiredSelectionTag(
		const FRTSSelectionQuery& Query,
		const FGameplayTagContainer& SelectionTags)
	{
		if (Query.ExcludedSelectionTag.IsValid()
			&& SelectionTags.HasTag(Query.ExcludedSelectionTag))
		{
			return false;
		}

		const FGameplayTag& RequiredSelectionTag = Query.RequiredSelectionTag;
		if (!RequiredSelectionTag.IsValid())
		{
			return true;
		}

		if (!SelectionTags.HasTag(RequiredSelectionTag))
		{
			return false;
		}

		const FGameplayTag ArmyRootTag = FGameplayTag::RequestGameplayTag(
			FName(TEXT("RTS.Selection.Army")), false);
		const FGameplayTag StructureRootTag = FGameplayTag::RequestGameplayTag(
			FName(TEXT("RTS.Selection.Structure")), false);
		const bool bArmyQuery = ArmyRootTag.IsValid() && RequiredSelectionTag.MatchesTag(ArmyRootTag);
		const bool bStructure = StructureRootTag.IsValid() && SelectionTags.HasTag(StructureRootTag);
		return !bArmyQuery || !bStructure;
	}

	FString GetActorGroupKey(const AActor* Actor)
	{
		if (!Actor)
		{
			return FString();
		}

		if (const URTSSelectable* Selectable = Actor->FindComponentByClass<URTSSelectable>())
		{
			if (!Selectable->SelectionGroupKey.IsEmpty())
			{
				return Selectable->SelectionGroupKey;
			}
		}

		return Actor->GetClass() ? Actor->GetClass()->GetName() : Actor->GetName();
	}

	FString GetSelectionUnitGroupKey(const FRTSUnitData& Data)
	{
		return Data.GroupKey.IsEmpty() ? Data.Name : Data.GroupKey;
	}

	FString MakeMassSubtypeGroupKey(int32 SubTypeIndex)
	{
		return FString::Printf(TEXT("MassUnit.SubType.%02d"), SubTypeIndex);
	}

	bool TryParseMassSubtypeGroupKey(const FString& GroupKey, int32& OutSubTypeIndex)
	{
		const FString Prefix = TEXT("MassUnit.SubType.");
		if (!GroupKey.StartsWith(Prefix))
		{
			return false;
		}

		return LexTryParseString(OutSubTypeIndex, *GroupKey.RightChop(Prefix.Len()));
	}

	int32 GetProtocolSubTypeIndex(const FRTSMassUnitTypeProtocol& Protocol, int32 ArrayIndex)
	{
		return Protocol.SubTypeIndex != INDEX_NONE ? Protocol.SubTypeIndex : ArrayIndex;
	}

	const FRTSMassUnitTypeProtocol* FindMassUnitTypeProtocolByIndex(const URTSInputPanelSettings* Settings, int32 SubTypeIndex)
	{
		if (!Settings)
		{
			return nullptr;
		}

		for (int32 i = 0; i < Settings->MassUnitTypeProtocols.Num(); ++i)
		{
			const FRTSMassUnitTypeProtocol& Protocol = Settings->MassUnitTypeProtocols[i];
			if (GetProtocolSubTypeIndex(Protocol, i) == SubTypeIndex)
			{
				return &Protocol;
			}
		}

		return nullptr;
	}

	const FRTSMassUnitTypeProtocol* FindMassUnitTypeProtocolForEntity(
		const URTSInputPanelSettings* Settings,
		const FMassEntityManager& EntityManager,
		const FMassEntityHandle Entity,
		const int32 SubTypeIndex)
	{
		const FNetworking* Networking = EntityManager.GetFragmentDataPtr<FNetworking>(Entity);
		return RTSUnitTypeProtocol::FindByNetworkKeyOrSubType(
			Settings,
			Networking ? Networking->Key : NAME_None,
			SubTypeIndex);
	}

	const FRTSCommandLoadoutDefinition* FindCommandLoadoutById(const URTSInputPanelSettings* Settings, FName LoadoutId)
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

	const FRTSMassUnitTypeProtocol* FindMassUnitTypeProtocolByKey(const URTSInputPanelSettings* Settings, const FString& TypeKey, int32& OutSubTypeIndex)
	{
		if (!Settings || TypeKey.IsEmpty())
		{
			return nullptr;
		}

		for (int32 i = 0; i < Settings->MassUnitTypeProtocols.Num(); ++i)
		{
			const FRTSMassUnitTypeProtocol& Protocol = Settings->MassUnitTypeProtocols[i];
			const int32 ProtocolIndex = GetProtocolSubTypeIndex(Protocol, i);
			const FString ConfiguredKey = Protocol.TypeKey.TrimStartAndEnd();
			const FString FallbackKey = MakeMassSubtypeGroupKey(ProtocolIndex);
			if (ConfiguredKey == TypeKey || FallbackKey == TypeKey)
			{
				OutSubTypeIndex = ProtocolIndex;
				return &Protocol;
			}
		}

		return nullptr;
	}

	const FRTSMassUnitTypeProtocol* ResolveMassUnitTypeProtocol(const URTSInputPanelSettings* Settings, const FString& TypeKey, int32& OutSubTypeIndex)
	{
		if (TryParseMassSubtypeGroupKey(TypeKey, OutSubTypeIndex))
		{
			return FindMassUnitTypeProtocolByIndex(Settings, OutSubTypeIndex);
		}

		return FindMassUnitTypeProtocolByKey(Settings, TypeKey, OutSubTypeIndex);
	}

	FString GetMassProtocolTypeKey(const FRTSMassUnitTypeProtocol* Protocol, int32 SubTypeIndex)
	{
		if (Protocol)
		{
			const FString ConfiguredKey = Protocol->TypeKey.TrimStartAndEnd();
			if (!ConfiguredKey.IsEmpty())
			{
				return ConfiguredKey;
			}
		}

		return MakeMassSubtypeGroupKey(SubTypeIndex);
	}

	FString GetDefaultMassSubtypeDisplayName(int32 SubTypeIndex)
	{
		static const TCHAR* DefaultNames[] =
		{
			TEXT("Rifle Section"),
			TEXT("Assault Section"),
			TEXT("Machine Gun Team"),
			TEXT("Mortar Team"),
			TEXT("Anti-Tank Team"),
			TEXT("Engineer Squad"),
			TEXT("Radio Operator"),
			TEXT("Field Medic"),
			TEXT("Scout Team"),
			TEXT("Sniper Team"),
			TEXT("Light Tank"),
			TEXT("Medium Tank"),
			TEXT("Heavy Tank"),
			TEXT("Armored Car"),
			TEXT("Halftrack"),
			TEXT("Artillery Crew"),
			TEXT("Anti-Air Crew"),
			TEXT("Command Squad"),
			TEXT("Naval Infantry"),
			TEXT("Mountain Troops"),
			TEXT("Cavalry Patrol"),
			TEXT("Flame Team"),
			TEXT("Recon Platoon"),
			TEXT("Supply Detail"),
			TEXT("Transport Convoy"),
			TEXT("Tank Destroyer"),
			TEXT("Rocket Battery"),
			TEXT("Airborne Squad"),
			TEXT("Security Detail"),
			TEXT("Veteran Squad"),
			TEXT("Reserve Squad"),
			TEXT("Headquarters")
		};

		if (SubTypeIndex >= 0 && SubTypeIndex < UE_ARRAY_COUNT(DefaultNames))
		{
			return DefaultNames[SubTypeIndex];
		}

		return FString::Printf(TEXT("SubType %02d"), SubTypeIndex);
	}

	FString GetDefaultMassSubtypeRole(int32 SubTypeIndex)
	{
		static const TCHAR* DefaultRoles[] =
		{
			TEXT("Line Infantry"),
			TEXT("Shock Infantry"),
			TEXT("Suppression Team"),
			TEXT("Indirect Fire"),
			TEXT("Anti Armor"),
			TEXT("Combat Engineer"),
			TEXT("Command Support"),
			TEXT("Medical Support"),
			TEXT("Recon"),
			TEXT("Precision Infantry"),
			TEXT("Light Armor"),
			TEXT("Battle Tank"),
			TEXT("Heavy Armor"),
			TEXT("Recon Vehicle"),
			TEXT("Transport"),
			TEXT("Indirect Fire"),
			TEXT("Air Defense"),
			TEXT("Command"),
			TEXT("Amphibious Infantry"),
			TEXT("Rough Terrain Infantry"),
			TEXT("Fast Recon"),
			TEXT("Close Assault"),
			TEXT("Recon"),
			TEXT("Logistics"),
			TEXT("Logistics"),
			TEXT("Anti Armor"),
			TEXT("Rocket Artillery"),
			TEXT("Elite Infantry"),
			TEXT("Garrison"),
			TEXT("Elite Infantry"),
			TEXT("Militia"),
			TEXT("Command Node")
		};

		if (SubTypeIndex >= 0 && SubTypeIndex < UE_ARRAY_COUNT(DefaultRoles))
		{
			return DefaultRoles[SubTypeIndex];
		}

		return TEXT("Combat Unit");
	}

	FName MakeDefaultAnnouncerId(int32 SubTypeIndex, const FString& DisplayName)
	{
		FString Sanitized = DisplayName;
		Sanitized.ReplaceInline(TEXT(" "), TEXT(""));
		Sanitized.ReplaceInline(TEXT("-"), TEXT(""));
		Sanitized.ReplaceInline(TEXT("."), TEXT(""));
		Sanitized.ReplaceInline(TEXT("_"), TEXT(""));

		if (Sanitized.IsEmpty())
		{
			Sanitized = FString::Printf(TEXT("SubType%02d"), SubTypeIndex);
		}

		return FName(*FString::Printf(TEXT("Unit.%s"), *Sanitized));
	}

	UTexture2D* LoadDefaultUnitPanelIconBySeed(uint32 Seed)
	{
		static const TCHAR* DefaultIconFiles[] =
		{
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_01.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_02.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_03.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_04.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_05.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_06.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_07.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_08.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_09.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_10.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_11.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_12.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_13.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_14.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_15.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_16.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_17.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_18.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_19.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_20.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_21.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_22.png"),
			TEXT("UnitIcons/Germany/Germany_Unit_Icon_23.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_01.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_02.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_03.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_04.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_05.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_06.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_07.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_08.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_09.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_10.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_11.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_12.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_13.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_14.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_15.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_16.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_17.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_18.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_19.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_20.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_21.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_22.png"),
			TEXT("UnitIcons/Japan/Japan_Unit_Icon_23.png"),
		};

		static TMap<int32, UTexture2D*> IconCache;

		int32 IconIndex = INDEX_NONE;
		if (Seed >= 3201u && Seed <= 3221u)
		{
			// MassBattle's adopted WW2 infantry table numbers German portraits from 3201.
			IconIndex = static_cast<int32>(Seed - 3201u);
		}
		else if (Seed >= 3222u && Seed <= 3244u)
		{
			// Japanese portraits start at 3222 and occupy the second 23-image bank.
			IconIndex = 23 + static_cast<int32>(Seed - 3222u);
		}
		else
		{
			IconIndex = static_cast<int32>(Seed % UE_ARRAY_COUNT(DefaultIconFiles));
		}
		if (UTexture2D** CachedTexture = IconCache.Find(IconIndex))
		{
			if (IsValid(*CachedTexture))
			{
				return *CachedTexture;
			}
			IconCache.Remove(IconIndex);
		}

		const FString IconPath = FPaths::Combine(
			FPaths::ProjectPluginsDir(),
			TEXT("RTSInputSystem"),
			TEXT("Content"),
			TEXT("Portraits"),
			TEXT("Source"),
			DefaultIconFiles[IconIndex]
		);

		UTexture2D* Texture = nullptr;
		if (IFileManager::Get().FileExists(*IconPath))
		{
			Texture = FImageUtils::ImportFileAsTexture2D(IconPath);
			if (Texture)
			{
				Texture->AddToRoot();
				Texture->SRGB = true;
			}
		}

		// A temporarily missing source file must not poison the entire editor
		// session. Cache successful imports only so a later selection can retry.
		if (Texture)
		{
			IconCache.Add(IconIndex, Texture);
		}
		return Texture;
	}

	UTexture2D* LoadDefaultUnitAvatar()
	{
		static UTexture2D* CachedAvatar = nullptr;

		if (IsValid(CachedAvatar))
		{
			return CachedAvatar;
		}

		const FString AvatarPath = FPaths::Combine(
			FPaths::ProjectPluginsDir(),
			TEXT("RTSInputSystem"),
			TEXT("Content"),
			TEXT("Portraits"),
			TEXT("Source"),
			TEXT("RTS_UnitAvatar_Placeholder_256x512.png")
		);

		if (IFileManager::Get().FileExists(*AvatarPath))
		{
			CachedAvatar = FImageUtils::ImportFileAsTexture2D(AvatarPath);
			if (CachedAvatar)
			{
				CachedAvatar->AddToRoot();
				CachedAvatar->SRGB = true;
			}
		}

		return CachedAvatar;
	}

	UTexture2D* LoadDefaultUnitAvatarBySeed(uint32 Seed)
	{
		(void)Seed;
		return LoadDefaultUnitAvatar();
	}

	UTexture2D* LoadConfiguredTexture(const TSoftObjectPtr<UTexture2D>& Texture)
	{
		return Texture.IsNull() ? nullptr : Texture.LoadSynchronous();
	}

	void ReadCombatStats(FRTSUnitData& Data, const FAttack* Attack, const FDamage* Damage, const FDefence* Defence)
	{
		Data.bHasWeapon = Attack && Attack->bEnable;
		if (Data.bHasWeapon)
		{
			Data.WeaponDamage = Damage ? Damage->Damage : 0.0f;
			Data.WeaponRange = Attack->Range;
			Data.WeaponPeriod = Attack->CoolDown;
		}
		Data.ArmorReduction = Defence && Defence->bEnable ? Defence->NormalDmgImmune : 0.0f;
	}

	void EnsureSelectionDataDefaults(FRTSUnitData& Data, int32 SubTypeIndex, uint32 IconSeed)
	{
		if (Data.Name.TrimStartAndEnd().IsEmpty())
		{
			Data.Name = SubTypeIndex != INDEX_NONE
				? GetDefaultMassSubtypeDisplayName(SubTypeIndex)
				: TEXT("Mass Unit");
		}

		if (Data.GroupKey.TrimStartAndEnd().IsEmpty())
		{
			Data.GroupKey = SubTypeIndex != INDEX_NONE
				? MakeMassSubtypeGroupKey(SubTypeIndex)
				: FString::Printf(TEXT("MassUnit.Entity.%u"), IconSeed);
		}

		if (Data.TypeKey.TrimStartAndEnd().IsEmpty())
		{
			Data.TypeKey = Data.GroupKey;
		}

		if (Data.Role.TrimStartAndEnd().IsEmpty())
		{
			Data.Role = SubTypeIndex != INDEX_NONE
				? GetDefaultMassSubtypeRole(SubTypeIndex)
				: TEXT("Combat Unit");
		}

		const TPair<const TCHAR*, const TCHAR*> Categories[] = {
			{TEXT("RTS.Selection.Army.Ground.Armor"), TEXT("坦克")},
			{TEXT("RTS.Selection.Army.Ground.Infantry"), TEXT("步兵")},
			{TEXT("RTS.Selection.Army.Ground.Artillery"), TEXT("火炮")},
			{TEXT("RTS.Selection.Army.Air"), TEXT("飞机")},
			{TEXT("RTS.Selection.Army.Naval"), TEXT("舰船")},
			{TEXT("RTS.Selection.Structure"), TEXT("建筑")}
		};
		Data.UnitCategory = Data.Role;
		for (const auto& Category : Categories)
		{
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(Category.Key), false);
			if (Tag.IsValid() && Data.SelectionTags.HasTag(Tag))
			{ Data.UnitCategory = Category.Value; break; }
		}
		const FGameplayTag Army = FGameplayTag::RequestGameplayTag(TEXT("RTS.Selection.Army"), false);
		if (Data.OrganizationLabel.IsEmpty() && Army.IsValid() && Data.SelectionTags.HasTag(Army))
			Data.OrganizationLabel = TEXT("单兵");

		if (Data.AnnouncerId.IsNone())
		{
			Data.AnnouncerId = MakeDefaultAnnouncerId(SubTypeIndex, Data.Name);
		}

		if (!Data.Icon)
		{
			Data.Icon = LoadDefaultUnitPanelIconBySeed(IconSeed);
		}
		if (!Data.Portrait)
		{
			Data.Portrait = Data.Icon;
		}
	}

	void ApplyMassProtocolToUnitData(FRTSUnitData& Data, const FRTSMassUnitTypeProtocol* Protocol, int32 SubTypeIndex)
	{
		if (!Protocol)
		{
			return;
		}

		Data.TypeKey = GetMassProtocolTypeKey(Protocol, SubTypeIndex);
		Data.UnitAssetPath = Protocol->UnitAssetPath;
		Data.UnitTypeTag = Protocol->UnitTypeTag;
		const FString ProtocolRole = Protocol->Role.TrimStartAndEnd();
		if (!ProtocolRole.IsEmpty())
		{
			Data.Role = ProtocolRole;
		}
		Data.AnnouncerId = Protocol->AnnouncerId;
		Data.SelectionSound = Protocol->SelectionSound;
		Data.ConfirmationSound = Protocol->ConfirmationSound;
		Data.CommandGrid = Protocol->CommandGrid;

		if (UTexture2D* ProtocolIcon = LoadConfiguredTexture(Protocol->Icon))
		{
			Data.Icon = ProtocolIcon;
		}
		if (UTexture2D* ProtocolPortrait = LoadConfiguredTexture(Protocol->Portrait))
		{
			Data.Portrait = ProtocolPortrait;
		}
	}

	ERTSCommandTargetType ResolveCommandSlotTargetType(const FRTSMassUnitCommandSlotDefinition& Slot, const FGameplayTag& CommandTag)
	{
		if (Slot.TargetType != ERTSCommandTargetType::Instant)
		{
			return Slot.TargetType;
		}

		const FName TagName = CommandTag.GetTagName();
		if (TagName == FName(TEXT("RTS.Command.Move")) || TagName == FName(TEXT("RTS.Command.Patrol")))
		{
			return ERTSCommandTargetType::Location;
		}

		if (TagName == FName(TEXT("RTS.Command.Attack")))
		{
			return ERTSCommandTargetType::LocationOrTarget;
		}

		if (TagName.ToString().StartsWith(TEXT("RTS.Command.Build.")))
		{
			return ERTSCommandTargetType::Location;
		}

		return ERTSCommandTargetType::Instant;
	}

	bool IsComposableContextCommand(const FGameplayTag& CommandTag)
	{
		const FName TagName = CommandTag.GetTagName();
		return TagName == FName(TEXT("RTS.Command.Move"))
			|| TagName == FName(TEXT("RTS.Command.Attack"))
			|| TagName == FName(TEXT("RTS.Command.Patrol"));
	}

	bool ClearsTaskVisualization(const FGameplayTag& CommandTag)
	{
		const FName TagName = CommandTag.GetTagName();
		return TagName == FName(TEXT("RTS.Command.Stop"))
			|| TagName == FName(TEXT("RTS.Command.Hold"));
	}

	URTSCommandButton* CreateDefaultCommandButtonForTag(UObject* Outer, const FGameplayTag& CommandTag)
	{
		const FName TagName = CommandTag.GetTagName();
		if (TagName == FName(TEXT("RTS.Command.Move")))
		{
			return NewObject<URTSCmd_Move>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Attack")))
		{
			return NewObject<URTSCmd_Attack>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Stop")))
		{
			return NewObject<URTSCmd_Stop>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Hold")))
		{
			return NewObject<URTSCmd_HoldPosition>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Patrol")))
		{
			return NewObject<URTSCmd_Patrol>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.Factory")))
		{
			return NewObject<URTSCmd_BuildFactory>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.University")))
		{
			return NewObject<URTSCmd_BuildUniversity>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.Airfield")))
		{
			return NewObject<URTSCmd_BuildAirfield>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.Shipyard")))
		{
			return NewObject<URTSCmd_BuildShipyard>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.ResearchCenter")))
		{
			return NewObject<URTSCmd_BuildResearchCenter>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.IndustrialPark")))
		{
			return NewObject<URTSCmd_BuildIndustrialPark>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.Refinery")))
		{
			return NewObject<URTSCmd_BuildRefinery>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.Barracks")))
		{
			return NewObject<URTSCmd_BuildBarracks>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.TankFactory")))
		{
			return NewObject<URTSCmd_BuildTankFactory>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.VehicleFactory")))
		{
			return NewObject<URTSCmd_BuildVehicleFactory>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.Defense")))
		{
			return NewObject<URTSCmd_BuildDefense>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.GarrisonBunker")))
		{
			return NewObject<URTSCmd_BuildGarrisonBunker>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.MortarBunker")))
		{
			return NewObject<URTSCmd_BuildMortarBunker>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.CoastalBattery")))
		{
			return NewObject<URTSCmd_BuildCoastalBattery>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.FieldCover")))
		{
			return NewObject<URTSCmd_BuildFieldCover>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.MachineGunBunker")))
		{
			return NewObject<URTSCmd_BuildMachineGunBunker>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.AntiTankBunker")))
		{
			return NewObject<URTSCmd_BuildAntiTankBunker>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.AntiAircraftEmplacement")))
		{
			return NewObject<URTSCmd_BuildAntiAircraftEmplacement>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.AntiTankObstacle")))
		{
			return NewObject<URTSCmd_BuildAntiTankObstacle>(Outer);
		}
		if (TagName == FName(TEXT("RTS.Command.Build.BarbedWire")))
		{
			return NewObject<URTSCmd_BuildBarbedWire>(Outer);
		}

		URTSCommandButton* Button = NewObject<URTSCommandButton>(Outer);
		Button->CommandTag = CommandTag;
		Button->TargetType = TagName.ToString().StartsWith(TEXT("RTS.Command.Build."))
			? ERTSCommandTargetType::Location
			: ERTSCommandTargetType::Instant;

		FString Label = TagName.ToString();
		Label.RemoveFromStart(TEXT("RTS.Command."));
		Label.ReplaceInline(TEXT("."), TEXT(" "));
		Button->DisplayName = FText::FromString(Label.IsEmpty() ? TEXT("Command") : Label);
		Button->Description = FText::FromString(FString::Printf(TEXT("Execute %s."), *Button->DisplayName.ToString()));
		return Button;
	}

	void AddDefaultUnitCommands(URTSCommandGridAsset* Grid)
	{
		if (!Grid)
		{
			return;
		}

		Grid->Buttons.Add(NewObject<URTSCmd_Move>(Grid));
		Grid->Buttons.Add(NewObject<URTSCmd_Stop>(Grid));
		Grid->Buttons.Add(NewObject<URTSCmd_HoldPosition>(Grid));
		Grid->Buttons.Add(NewObject<URTSCmd_Patrol>(Grid));
		Grid->Buttons.Add(NewObject<URTSCmd_Attack>(Grid));
	}

	bool ResolveCoreCommandPresentation(
		const FGameplayTag& CommandTag,
		int32& OutSlotIndex,
		FKey& OutHotkey)
	{
		const FName TagName = CommandTag.GetTagName();
		if (TagName == FName(TEXT("RTS.Command.Move")))
		{
			OutSlotIndex = 0;
			OutHotkey = EKeys::Q;
			return true;
		}
		if (TagName == FName(TEXT("RTS.Command.Stop")))
		{
			OutSlotIndex = 1;
			OutHotkey = EKeys::W;
			return true;
		}
		if (TagName == FName(TEXT("RTS.Command.Hold")))
		{
			OutSlotIndex = 2;
			OutHotkey = EKeys::E;
			return true;
		}
		if (TagName == FName(TEXT("RTS.Command.Patrol")))
		{
			OutSlotIndex = 3;
			OutHotkey = EKeys::R;
			return true;
		}
		if (TagName == FName(TEXT("RTS.Command.Attack")))
		{
			OutSlotIndex = 4;
			OutHotkey = EKeys::T;
			return true;
		}
		return false;
	}

	int32 ResolveCommandSlotIndex(
		const FGameplayTag& CommandTag,
		const int32 ConfiguredSlotIndex)
	{
		int32 CanonicalSlotIndex = ConfiguredSlotIndex;
		FKey CanonicalHotkey;
		ResolveCoreCommandPresentation(
			CommandTag,
			CanonicalSlotIndex,
			CanonicalHotkey);
		return CanonicalSlotIndex;
	}

	void RemoveCommandAtSlot(URTSCommandGridAsset* Grid, int32 SlotIndex)
	{
		if (!Grid)
		{
			return;
		}

		Grid->Buttons.RemoveAll([SlotIndex](const TObjectPtr<URTSCommandButton>& Button)
		{
			return Button && Button->PreferredIndex == SlotIndex;
		});
	}

	void ApplyBuiltInDefenseGridLayout(URTSCommandGridAsset* Grid)
	{
		if (!Grid)
		{
			return;
		}

		for (URTSCommandButton* Button : Grid->GetAllButtons())
		{
			if (!Button)
			{
				continue;
			}

			const FName CommandName = Button->CommandTag.GetTagName();
			if (CommandName == FName(TEXT("RTS.Command.Build.FieldCover")))
			{
				Button->PreferredIndex = 10;
				Button->DisplayName = FText::FromString(TEXT("沙袋"));
				Button->Hotkey = EKeys::Z;
			}
			else if (CommandName == FName(TEXT("RTS.Command.Build.MachineGunBunker")))
			{
				Button->PreferredIndex = 1;
				Button->DisplayName = FText::FromString(TEXT("机枪碉堡"));
				Button->Hotkey = EKeys::W;
			}
			else if (CommandName == FName(TEXT("RTS.Command.Build.AntiTankBunker")))
			{
				Button->PreferredIndex = 2;
				Button->DisplayName = FText::FromString(TEXT("反坦克碉堡"));
				Button->Hotkey = EKeys::E;
			}
			else if (CommandName == FName(TEXT("RTS.Command.Build.AntiAircraftEmplacement")))
			{
				Button->PreferredIndex = 5;
				Button->DisplayName = FText::FromString(TEXT("防空"));
				Button->Hotkey = EKeys::A;
			}
			else if (CommandName == FName(TEXT("RTS.Command.Build.AntiTankObstacle")))
			{
				Button->PreferredIndex = 12;
				Button->DisplayName = FText::FromString(TEXT("反坦克陷阱"));
				Button->Hotkey = EKeys::C;
			}
			else if (CommandName == FName(TEXT("RTS.Command.Build.BarbedWire")))
			{
				Button->PreferredIndex = 11;
				Button->Hotkey = EKeys::X;
			}
		}
	}

	void ApplyCommandSlotPresentation(URTSCommandButton* Button, const FRTSMassUnitCommandSlotDefinition& Slot, const FGameplayTag& CommandTag)
	{
		if (!Button)
		{
			return;
		}

		Button->CommandTag = CommandTag;
		Button->TargetType = ResolveCommandSlotTargetType(Slot, CommandTag);
		int32 PresentationSlotIndex = Slot.SlotIndex;
		FKey PresentationHotkey = Slot.Hotkey.IsNone()
			? Button->Hotkey
			: FKey(Slot.Hotkey);
		const bool bCoreCommand = ResolveCoreCommandPresentation(
			CommandTag,
			PresentationSlotIndex,
			PresentationHotkey);
		Button->PreferredIndex = PresentationSlotIndex;
		Button->bHideIfUnavailable = Slot.bHideIfUnavailable;

		if (!Slot.DisplayName.TrimStartAndEnd().IsEmpty())
		{
			Button->DisplayName = FText::FromString(Slot.DisplayName);
		}

		if (!Slot.Description.TrimStartAndEnd().IsEmpty())
		{
			Button->Description = FText::FromString(Slot.Description);
		}

		if (bCoreCommand || !Slot.Hotkey.IsNone())
		{
			Button->Hotkey = PresentationHotkey;
		}

		if (UTexture2D* SlotIcon = LoadConfiguredTexture(Slot.Icon))
		{
			Button->Icon = SlotIcon;
		}
	}
}

void URTSSelectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

    // C++ Auto-Config Grid (Transient)
    // If no grid is provided, use the built-in MassBattle unit grid.
    if (DefaultEntityGrid.IsNull())
    {
        UE_LOG(LogORTSSelection, Log, TEXT("Selection: Auto-configuring transient default grid."));
        URTSUnitCommandGrid* TransientGrid = NewObject<URTSUnitCommandGrid>(this, TEXT("TransientDefaultUnitCommandGrid"));

        DefaultEntityGrid = TransientGrid;
        DefaultGridNative = TransientGrid; // Keep it alive and accessible
    }
}

void URTSSelectionSubsystem::Deinitialize()
{
	OnCommandFeedbackIssued.Clear();
	ControlGroups.Reset();
	MassProtocolGridCache.Reset();
	ActiveControlGroupIndex = INDEX_NONE;
	PreferredActiveControlGroupIndex = INDEX_NONE;
	Super::Deinitialize();
}

FString URTSSelectionSubsystem::GetMassSubtypeDisplayName(int32 SubTypeIndex) const
{
	const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();
	if (const FRTSMassUnitTypeProtocol* Protocol = FindMassUnitTypeProtocolByIndex(Settings, SubTypeIndex))
	{
		const FString ProtocolName = Protocol->DisplayName.TrimStartAndEnd();
		if (!ProtocolName.IsEmpty())
		{
			return ProtocolName;
		}
	}

	if (Settings && Settings->MassUnitAvatars.IsValidIndex(SubTypeIndex))
	{
		const FString ConfiguredName = Settings->MassUnitAvatars[SubTypeIndex].DisplayName.TrimStartAndEnd();
		if (!ConfiguredName.IsEmpty())
		{
			return ConfiguredName;
		}
	}

	return GetDefaultMassSubtypeDisplayName(SubTypeIndex);
}

UTexture2D* URTSSelectionSubsystem::GetMassUnitPortrait(
	FName UnitAssetKey,
	int32 SubTypeIndex) const
{
	const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();
	const FRTSMassUnitTypeProtocol* Protocol =
		RTSUnitTypeProtocol::FindByNetworkKeyOrSubType(
			Settings,
			UnitAssetKey,
			SubTypeIndex);
	int32 ResolvedSubTypeIndex = SubTypeIndex;
	if (Protocol && ResolvedSubTypeIndex == INDEX_NONE && Settings)
	{
		const int32 ProtocolArrayIndex = static_cast<int32>(
			Protocol - Settings->MassUnitTypeProtocols.GetData());
		if (Settings->MassUnitTypeProtocols.IsValidIndex(ProtocolArrayIndex))
		{
			ResolvedSubTypeIndex = GetProtocolSubTypeIndex(
				*Protocol,
				ProtocolArrayIndex);
		}
	}
	if (Protocol)
	{
		if (UTexture2D* ProtocolPortrait = LoadConfiguredTexture(Protocol->Portrait))
		{
			return ProtocolPortrait;
		}
		if (UTexture2D* ProtocolIcon = LoadConfiguredTexture(Protocol->Icon))
		{
			return ProtocolIcon;
		}
	}

	if (Settings && Settings->MassUnitAvatars.IsValidIndex(ResolvedSubTypeIndex))
	{
		if (UTexture2D* Avatar = LoadConfiguredTexture(
			Settings->MassUnitAvatars[ResolvedSubTypeIndex].Avatar))
		{
			return Avatar;
		}
	}

	if (ResolvedSubTypeIndex == INDEX_NONE)
	{
		return nullptr;
	}

	return LoadDefaultUnitPanelIconBySeed(static_cast<uint32>(ResolvedSubTypeIndex));
}

UTexture2D* URTSSelectionSubsystem::GetMassSubtypeUnitPanelIcon(int32 SubTypeIndex) const
{
	const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();
	if (const FRTSMassUnitTypeProtocol* Protocol =
		RTSUnitTypeProtocol::FindBySubType(Settings, SubTypeIndex))
	{
		if (UTexture2D* ProtocolIcon = LoadConfiguredTexture(Protocol->Icon))
		{
			return ProtocolIcon;
		}
		if (UTexture2D* ProtocolPortrait = LoadConfiguredTexture(Protocol->Portrait))
		{
			return ProtocolPortrait;
		}
	}
	return LoadDefaultUnitPanelIconBySeed(static_cast<uint32>(SubTypeIndex));
}

UTexture2D* URTSSelectionSubsystem::GetMassSubtypeUnitAvatar(int32 SubTypeIndex) const
{
	return GetMassUnitPortrait(NAME_None, SubTypeIndex);
}

void URTSSelectionSubsystem::SetSelectedUnits(const TArray<AActor*>& InActors, const TArray<FEntityHandle>& InEntities, ERTSSelectionModifier Modifier)
{
    TArray<AActor*> FinalActors;
    TArray<FEntityHandle> FinalEntities;

	for (AActor* Actor : InActors)
	{
		if (IsValid(Actor) && IsActorControllable(Actor))
		{
			FinalActors.AddUnique(Actor);
		}
	}

	for (const FEntityHandle& Handle : InEntities)
	{
		if (IsEntityControllable(Handle))
		{
			FinalEntities.AddUnique(Handle);
		}
	}

    // Strategic Resolution: Convert Actors to Entities if they are Proxies
    for (int32 i = FinalActors.Num() - 1; i >= 0; i--)
    {
        AActor* Actor = FinalActors[i];
        if (Actor)
        {
            if (UMassBattleAgentComponent* MassAgent = Actor->FindComponentByClass<UMassBattleAgentComponent>())
            {
                FEntityHandle ProxiedEntity = MassAgent->GetEntityHandle();
                if (IsEntityControllable(ProxiedEntity))
                {
                    FinalEntities.AddUnique(ProxiedEntity);
                    FinalActors.RemoveAt(i);
                }
            }
        }
    }

	// 1. Update Internal State
	if (Modifier == ERTSSelectionModifier::Replace)
	{
		if (SelectedEntities.Num() > 0)
		{
			UMassBattleFuncLib::DeselectAgents(this, SelectedEntities, ESelectState::All);
		}
		
		SelectedActors = FinalActors;
		SelectedEntities = FinalEntities;
		
		if (SelectedEntities.Num() > 0)
		{
			UMassBattleFuncLib::SelectAgents(this, SelectedEntities, ESelectState::Selected);
		}
	}
	else if (Modifier == ERTSSelectionModifier::Add)
	{
		for (AActor* Actor : FinalActors) SelectedActors.AddUnique(Actor);
		for (const FEntityHandle& Handle : FinalEntities) SelectedEntities.AddUnique(Handle);
		if (FinalEntities.Num() > 0)
		{
			UMassBattleFuncLib::SelectAgents(this, FinalEntities, ESelectState::Selected);
		}
	}
	else if (Modifier == ERTSSelectionModifier::Remove)
	{
		for (AActor* Actor : FinalActors) SelectedActors.Remove(Actor);
		for (const FEntityHandle& Handle : FinalEntities) SelectedEntities.Remove(Handle);
		if (FinalEntities.Num() > 0)
		{
			UMassBattleFuncLib::DeselectAgents(this, FinalEntities, ESelectState::All);
		}
	}

	// A changed selection has no explicit Tab priority yet. BuildSelectionView
	// chooses the most numerous unit-type group as the new default.
	CurrentGroupIndex = INDEX_NONE;
	const FRTSSelectionView View = BuildSelectionView();
	UpdateActiveControlGroupIndex();
	BroadcastSelectionViewAndGrid(View);
	BroadcastControlGroupsView();

    UE_LOG(LogORTSSelection, Log, TEXT("Selection: Modifier=%d Actors=%d Entities=%d ActiveKey=%s"),
        (int32)Modifier, SelectedActors.Num(), SelectedEntities.Num(), *View.ActiveGroupKey);
}

int32 URTSSelectionSubsystem::GetPlayerTeamIndex() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const APlayerController* PlayerController = LocalPlayer
		? LocalPlayer->GetPlayerController(GetWorld())
		: nullptr;
	const APlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<APlayerState>()
		: nullptr;
	const IGenericTeamAgentInterface* TeamProvider =
		Cast<IGenericTeamAgentInterface>(PlayerState);
	if (!TeamProvider)
	{
		return INDEX_NONE;
	}

	const uint8 TeamId = TeamProvider->GetGenericTeamId().GetId();
	return TeamId == FGenericTeamId::NoTeam.GetId()
		? INDEX_NONE
		: static_cast<int32>(TeamId);
}

bool URTSSelectionSubsystem::IsEntityControllable(const FEntityHandle& Handle) const
{
	if (Handle.Index == 0)
	{
		return false;
	}

	UWorld* World = GetWorld();
	UMassEntitySubsystem* MassSubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!MassSubsystem)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle(Handle.Index, Handle.Serial);
	if (!EntityManager.IsEntityActive(NativeHandle))
	{
		return false;
	}

	const int32 PlayerTeamIndex = GetPlayerTeamIndex();
	if (PlayerTeamIndex == INDEX_NONE)
	{
		return false;
	}

	const FTeam* Team = EntityManager.GetFragmentDataPtr<FTeam>(NativeHandle);
	const FSelect* Select = EntityManager.GetFragmentDataPtr<FSelect>(NativeHandle);
	return Team && Team->index == PlayerTeamIndex && (!Select || Select->bEnable);
}

bool URTSSelectionSubsystem::IsActorControllable(const AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return false;
	}

	const int32 PlayerTeamIndex = GetPlayerTeamIndex();
	if (PlayerTeamIndex == INDEX_NONE)
	{
		return false;
	}

	if (const UMassBattleAgentComponent* MassAgent = Actor->FindComponentByClass<UMassBattleAgentComponent>())
	{
		const FEntityHandle Handle = MassAgent->GetEntityHandle();
		return Handle.Index != 0
			? IsEntityControllable(Handle)
			: MassAgent->TeamIndex == PlayerTeamIndex;
	}

	if (const URTSSelectable* Selectable = Actor->FindComponentByClass<URTSSelectable>())
	{
		return Selectable->TeamIndex == PlayerTeamIndex;
	}

	return false;
}

TArray<FEntityHandle> URTSSelectionSubsystem::GetControllableSelectedEntities() const
{
	TArray<FEntityHandle> Result;
	Result.Reserve(SelectedEntities.Num());
	for (const FEntityHandle& Handle : SelectedEntities)
	{
		if (IsEntityControllable(Handle))
		{
			Result.Add(Handle);
		}
	}
	return Result;
}

FRTSSelectionView URTSSelectionSubsystem::BuildSelectionView()
{
	FRTSSelectionView View;
	int32 TotalCount = SelectedActors.Num() + SelectedEntities.Num();
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const int32 SummaryThreshold = Settings
		? FMath::Max(1, Settings->SelectionSummaryThreshold)
		: 16;

	if (TotalCount == 0)
	{
		View.Mode = ERTSSelectionMode::Empty;
	}
	else if (TotalCount == 1)
	{
		View.Mode = ERTSSelectionMode::Single;
		if (SelectedActors.Num() > 0) View.SingleUnit = CreateUnitDataFromActor(SelectedActors[0]);
		else View.SingleUnit = CreateUnitDataFromEntity(SelectedEntities[0]);
		View.Items.Add(View.SingleUnit);
	}
	else if (TotalCount <= SummaryThreshold)
	{
		View.Mode = ERTSSelectionMode::List;
		for (AActor* Actor : SelectedActors) View.Items.Add(CreateUnitDataFromActor(Actor));
		for (const FEntityHandle& Handle : SelectedEntities) View.Items.Add(CreateUnitDataFromEntity(Handle));
		View.Items.Sort([](const FRTSUnitData& A, const FRTSUnitData& B)
		{
			const int32 NameCompare = A.Name.Compare(B.Name);
			return NameCompare == 0 ? GetSelectionUnitGroupKey(A) < GetSelectionUnitGroupKey(B) : NameCompare < 0;
		});
	}
	else
	{
		View.Mode = ERTSSelectionMode::Summary;
		TMap<FString, FRTSUnitData> GroupMap;

		for (AActor* Actor : SelectedActors)
		{
			FRTSUnitData Data = CreateUnitDataFromActor(Actor);
			AddOrUpdateSummaryGroup(GroupMap, Data);
		}

		for (const FEntityHandle& Handle : SelectedEntities)
		{
			FRTSUnitData Data = CreateUnitDataFromEntity(Handle);
			AddOrUpdateSummaryGroup(GroupMap, Data);
		}

		for (auto& Pair : GroupMap) View.Items.Add(Pair.Value);
		View.Items.Sort([](const FRTSUnitData& A, const FRTSUnitData& B)
		{
			const int32 NameCompare = A.Name.Compare(B.Name);
			return NameCompare == 0 ? GetSelectionUnitGroupKey(A) < GetSelectionUnitGroupKey(B) : NameCompare < 0;
		});
	}

	// --- Tab Cycling ---
	const FString PreviousActiveKey = AvailableGroupKeys.IsValidIndex(CurrentGroupIndex)
		? AvailableGroupKeys[CurrentGroupIndex]
		: FString();

	AvailableGroupKeys.Reset();
	TMap<FString, int32> GroupCounts;
	for (const auto& Item : View.Items)
	{
		const FString GroupKey = GetSelectionUnitGroupKey(Item);
		AvailableGroupKeys.AddUnique(GroupKey);
		GroupCounts.FindOrAdd(GroupKey) += FMath::Max(1, Item.Count);
	}
	AvailableGroupKeys.Sort();

	bool bPreservedActiveGroup = false;
	if (!PreviousActiveKey.IsEmpty())
	{
		const int32 PreservedIndex = AvailableGroupKeys.IndexOfByKey(PreviousActiveKey);
		if (PreservedIndex != INDEX_NONE)
		{
			CurrentGroupIndex = PreservedIndex;
			bPreservedActiveGroup = true;
		}
	}

	if (AvailableGroupKeys.IsEmpty())
	{
		CurrentGroupIndex = INDEX_NONE;
		return View;
	}

	if (!bPreservedActiveGroup)
	{
		int32 LargestGroupCount = INDEX_NONE;
		FString LargestGroupKey;
		for (const FString& GroupKey : AvailableGroupKeys)
		{
			const int32 GroupCount = GroupCounts.FindRef(GroupKey);
			// AvailableGroupKeys is sorted, so equal counts keep a deterministic key.
			if (GroupCount > LargestGroupCount)
			{
				LargestGroupCount = GroupCount;
				LargestGroupKey = GroupKey;
			}
		}
		CurrentGroupIndex = AvailableGroupKeys.IndexOfByKey(LargestGroupKey);
	}

	if (AvailableGroupKeys.IsValidIndex(CurrentGroupIndex)) View.ActiveGroupKey = AvailableGroupKeys[CurrentGroupIndex];

	return View;
}

void URTSSelectionSubsystem::AddOrUpdateSummaryGroup(TMap<FString, FRTSUnitData>& GroupMap, const FRTSUnitData& Data)
{
	const FString GroupKey = GetSelectionUnitGroupKey(Data);
	if (FRTSUnitData* ExistingGroup = GroupMap.Find(GroupKey))
	{
		const int32 PreviousCount = ExistingGroup->Count;
		ExistingGroup->Count++;
		ExistingGroup->SelectionTags.AppendTags(Data.SelectionTags);

		if (Data.bHasProductionCapacity)
		{
			ExistingGroup->bHasProductionCapacity = true;
			ExistingGroup->ProductionBusyLanes += Data.ProductionBusyLanes;
			ExistingGroup->ProductionTotalLanes += Data.ProductionTotalLanes;
			ExistingGroup->ProductionQueuedOrders += Data.ProductionQueuedOrders;
		}

		if (Data.bHasActivity)
		{
			if (!ExistingGroup->bHasActivity)
			{
				ExistingGroup->bHasActivity = true;
				ExistingGroup->ActivityLabel = Data.ActivityLabel;
				ExistingGroup->ActivityProgress = Data.ActivityProgress;
				ExistingGroup->ActivityRemainingSeconds = Data.ActivityRemainingSeconds;
				ExistingGroup->ActivityDurationSeconds = Data.ActivityDurationSeconds;
				ExistingGroup->ActivityQueueCount = Data.ActivityQueueCount;
			}
			else
			{
				const float NewCount = static_cast<float>(PreviousCount + 1);
				ExistingGroup->ActivityProgress =
					(ExistingGroup->ActivityProgress * PreviousCount + Data.ActivityProgress) / NewCount;
				ExistingGroup->ActivityRemainingSeconds =
					(ExistingGroup->ActivityRemainingSeconds * PreviousCount + Data.ActivityRemainingSeconds) / NewCount;
				ExistingGroup->ActivityDurationSeconds =
					(ExistingGroup->ActivityDurationSeconds * PreviousCount + Data.ActivityDurationSeconds) / NewCount;
				ExistingGroup->ActivityQueueCount += Data.ActivityQueueCount;
				if (!ExistingGroup->ActivityLabel.EqualTo(Data.ActivityLabel))
				{
					ExistingGroup->ActivityLabel = FText::FromString(TEXT("多个生产项目"));
				}
			}
		}
		return;
	}

	FRTSUnitData NewGroup = Data;
	NewGroup.GroupKey = GroupKey;
	NewGroup.Count = 1;
	GroupMap.Add(GroupKey, NewGroup);
}

FRTSExternalMassCommandGridResolver& URTSSelectionSubsystem::OnResolveMassCommandGrid()
{
	static FRTSExternalMassCommandGridResolver Resolver;
	return Resolver;
}

FRTSExternalMassUnitDataEnricher& URTSSelectionSubsystem::OnEnrichMassUnitData()
{
	static FRTSExternalMassUnitDataEnricher Enricher;
	return Enricher;
}

FRTSExternalQuickSelectionResolver& URTSSelectionSubsystem::OnResolveQuickSelection()
{
	static FRTSExternalQuickSelectionResolver Resolver;
	return Resolver;
}

FRTSExternalMassInstantCommandHandler& URTSSelectionSubsystem::OnHandleMassInstantCommand()
{
	static FRTSExternalMassInstantCommandHandler Handler;
	return Handler;
}

FRTSExternalMassLocationCommandHandler& URTSSelectionSubsystem::OnHandleMassLocationCommand()
{
	static FRTSExternalMassLocationCommandHandler Handler;
	return Handler;
}

FRTSExternalMassTargetCommandHandler& URTSSelectionSubsystem::OnHandleMassTargetCommand()
{
	static FRTSExternalMassTargetCommandHandler Handler;
	return Handler;
}

FRTSExternalBuildPlacementValidator& URTSSelectionSubsystem::OnValidateBuildPlacement()
{
	static FRTSExternalBuildPlacementValidator Validator;
	return Validator;
}

void URTSSelectionSubsystem::RequestSelectionRefresh()
{
	OnSelectionChanged.Broadcast(BuildSelectionView());
	BroadcastControlGroupsView();
}

void URTSSelectionSubsystem::BroadcastSelectionViewAndGrid(const FRTSSelectionView& View)
{
	OnSelectionChanged.Broadcast(View);

    // --- Grid Synchronization ---
    // ActiveGroupKey is the stable group id; UI may display a friendlier unit name.
    URTSCommandGridAsset* NewGrid = nullptr;
    const FString& ActiveKey = View.ActiveGroupKey;

    if (!ActiveKey.IsEmpty())
    {
        // 路径A: Actor 组 —— 在选中 Actor 里找 ActiveKey 对应的 Actor，取其 Grid
        for (AActor* Actor : SelectedActors)
        {
            if (Actor && GetActorGroupKey(Actor) == ActiveKey
                && Actor->Implements<URTSCommandInterface>())
            {
                NewGrid = IRTSCommandInterface::Execute_GetCommandGrid(Actor);
                break;
            }
		}

		// 路径B: 外部插件可先为纯 Mass 实体提供命令面板。
		if (!NewGrid)
		{
			OnResolveMassCommandGrid().Broadcast(this, ActiveKey, View, NewGrid);
		}

		if (NewGrid)
		{
			OnCommandNavigationRequested.Broadcast(NewGrid);
			UE_LOG(LogORTSSelection, Log, TEXT("Selection: Grid sync ActiveKey=%s Grid=>%s"),
				*ActiveKey, *NewGrid->GetName());
			return;
		}

		// 路径C: Mass Entity 组 —— 用 type protocol 推送专属/稀疏技能面板。
		bool bMassProtocolHandledGrid = false;
		if (!NewGrid)
		{
			bMassProtocolHandledGrid = ResolveMassProtocolCommandGrid(ActiveKey, NewGrid);
		}

		if (bMassProtocolHandledGrid)
		{
			OnCommandNavigationRequested.Broadcast(NewGrid);
			UE_LOG(LogORTSSelection, Log, TEXT("Selection: Grid sync ActiveKey=%s Grid=>%s"),
				*ActiveKey, NewGrid ? *NewGrid->GetName() : TEXT("NULL"));
			return;
		}
    }

    // 路径D: 兜底默认 Grid（士兵移动/攻击/停止）
    if (!NewGrid && !DefaultEntityGrid.IsNull() && (SelectedActors.Num() > 0 || SelectedEntities.Num() > 0))
    {
        NewGrid = DefaultEntityGrid.LoadSynchronous();
    }

    OnCommandNavigationRequested.Broadcast(NewGrid);
    UE_LOG(LogORTSSelection, Log, TEXT("Selection: Grid sync ActiveKey=%s Grid=>%s"),
        *ActiveKey, NewGrid ? *NewGrid->GetName() : TEXT("NULL"));
}

bool URTSSelectionSubsystem::ResolveMassProtocolCommandGrid(const FString& ActiveKey, URTSCommandGridAsset*& OutGrid)
{
	OutGrid = nullptr;

	const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();
	int32 SubTypeIndex = INDEX_NONE;
	const FRTSMassUnitTypeProtocol* Protocol = ResolveMassUnitTypeProtocol(Settings, ActiveKey, SubTypeIndex);
	if (!Protocol)
	{
		return false;
	}

	const FRTSCommandLoadoutDefinition* Loadout = FindCommandLoadoutById(
		Settings,
		RTSUnitTypeProtocol::ResolveCommandLoadoutId(*Protocol));
	if (Loadout)
	{
		OutGrid = ResolveCommandLoadoutGrid(Settings, *Loadout);
		return OutGrid != nullptr;
	}

	const TSoftObjectPtr<URTSCommandGridAsset>* AuthoredGrid = &Protocol->CommandGrid;
	const TArray<FRTSMassUnitCommandSlotDefinition>* CommandSlots = &Protocol->CommandSlots;
	const bool bIncludeDefaultCommands = Protocol->bUseDefaultCommandGrid;
	const FName CacheKey(*FString::Printf(TEXT("LegacyMassType_%d"), SubTypeIndex));

	if (AuthoredGrid && !AuthoredGrid->IsNull())
	{
		if (URTSCommandGridAsset* LoadedGrid = AuthoredGrid->LoadSynchronous())
		{
			if (LoadedGrid->GetAllButtons().Num() > 0)
			{
				OutGrid = LoadedGrid;
				return true;
			}
		}
	}

	if ((!CommandSlots || CommandSlots->Num() == 0) && !bIncludeDefaultCommands)
	{
		return false;
	}

	if (TObjectPtr<URTSCommandGridAsset>* CachedGrid = MassProtocolGridCache.Find(CacheKey))
	{
		OutGrid = CachedGrid->Get();
		return OutGrid != nullptr;
	}

	const FName GridName(*FString::Printf(TEXT("CommandLoadout_%s"), *CacheKey.ToString()));
	URTSCommandGridAsset* TransientGrid = NewObject<URTSCommandGridAsset>(this, GridName);
	if (bIncludeDefaultCommands)
	{
		AddDefaultUnitCommands(TransientGrid);
	}

	if (CommandSlots)
	{
		for (const FRTSMassUnitCommandSlotDefinition& Slot : *CommandSlots)
		{
			FGameplayTag ResolvedCommandTag = Slot.CommandTag;
			if (!ResolvedCommandTag.IsValid() && !Slot.CommandTagName.IsNone())
			{
				ResolvedCommandTag = FGameplayTag::RequestGameplayTag(Slot.CommandTagName, false);
			}

			const int32 ResolvedSlotIndex =
				ResolveCommandSlotIndex(ResolvedCommandTag, Slot.SlotIndex);
			if (!ResolvedCommandTag.IsValid() || ResolvedSlotIndex < 0 || ResolvedSlotIndex > 14)
			{
				continue;
			}

			RemoveCommandAtSlot(TransientGrid, ResolvedSlotIndex);
			URTSCommandButton* Button = CreateDefaultCommandButtonForTag(TransientGrid, ResolvedCommandTag);
			ApplyCommandSlotPresentation(Button, Slot, ResolvedCommandTag);
			TransientGrid->Buttons.Add(Button);
		}
	}

	if (TransientGrid->Buttons.Num() > 0)
	{
		MassProtocolGridCache.Add(CacheKey, TransientGrid);
		OutGrid = TransientGrid;
		return true;
	}

	return false;
}

URTSCommandGridAsset* URTSSelectionSubsystem::ResolveCommandLoadoutGrid(
	const URTSInputPanelSettings* Settings,
	const FRTSCommandLoadoutDefinition& Loadout)
{
	if (!Settings || Loadout.LoadoutId.IsNone())
	{
		return nullptr;
	}

	// A LocalPlayer subsystem can outlive several PIE selections, so update an
	// already-created defense submenu in place instead of leaving the old flat row.
	if (Loadout.LoadoutId == FName(TEXT("Builder")))
	{
		const TObjectPtr<URTSCommandGridAsset>* CachedDefense = MassProtocolGridCache.Find(
			FName(TEXT("BuilderDefense")));
		if (CachedDefense)
		{
			ApplyBuiltInDefenseGridLayout(CachedDefense->Get());
		}
	}

	if (!Loadout.CommandGrid.IsNull())
	{
		if (URTSCommandGridAsset* LoadedGrid = Loadout.CommandGrid.LoadSynchronous())
		{
			if (LoadedGrid->GetAllButtons().Num() > 0)
			{
				return LoadedGrid;
			}
		}
	}

	if (TObjectPtr<URTSCommandGridAsset>* CachedGrid = MassProtocolGridCache.Find(Loadout.LoadoutId))
	{
		return CachedGrid->Get();
	}

	if (Loadout.CommandSlots.Num() == 0 && !Loadout.bIncludeDefaultUnitCommands && Loadout.BackToLoadoutId.IsNone())
	{
		return nullptr;
	}

	const FName GridName(*FString::Printf(TEXT("CommandLoadout_%s"), *Loadout.LoadoutId.ToString()));
	URTSCommandGridAsset* TransientGrid = NewObject<URTSCommandGridAsset>(this, GridName);

	// Cache before resolving child/parent menus so cyclic navigation (parent <-> child) is safe.
	MassProtocolGridCache.Add(Loadout.LoadoutId, TransientGrid);

	if (Loadout.bIncludeDefaultUnitCommands)
	{
		AddDefaultUnitCommands(TransientGrid);
	}

	auto AddConfiguredButton = [this, Settings, TransientGrid](const FRTSMassUnitCommandSlotDefinition& Slot)
	{
		FGameplayTag ResolvedCommandTag = Slot.CommandTag;
		if (!ResolvedCommandTag.IsValid() && !Slot.CommandTagName.IsNone())
		{
			ResolvedCommandTag = FGameplayTag::RequestGameplayTag(Slot.CommandTagName, false);
		}

		const int32 ResolvedSlotIndex =
			ResolveCommandSlotIndex(ResolvedCommandTag, Slot.SlotIndex);
		if (!ResolvedCommandTag.IsValid() || ResolvedSlotIndex < 0 || ResolvedSlotIndex > 14)
		{
			return;
		}

		URTSCommandButton* Button = nullptr;
		if (!Slot.SubMenuLoadoutId.IsNone())
		{
			const FRTSCommandLoadoutDefinition* TargetLoadout = FindCommandLoadoutById(Settings, Slot.SubMenuLoadoutId);
			URTSCommandGridAsset* TargetGrid = TargetLoadout
				? ResolveCommandLoadoutGrid(Settings, *TargetLoadout)
				: nullptr;

			if (!TargetGrid)
			{
				UE_LOG(LogORTSSelection, Warning,
					TEXT("Selection: Command loadout submenu '%s' could not be resolved."),
					*Slot.SubMenuLoadoutId.ToString());
				return;
			}

			URTSCmd_SubMenu* SubMenuButton = NewObject<URTSCmd_SubMenu>(TransientGrid);
			SubMenuButton->TargetGrid = TargetGrid;
			Button = SubMenuButton;
		}
		else
		{
			Button = CreateDefaultCommandButtonForTag(TransientGrid, ResolvedCommandTag);
		}

		RemoveCommandAtSlot(TransientGrid, ResolvedSlotIndex);
		ApplyCommandSlotPresentation(Button, Slot, ResolvedCommandTag);
		if (!Slot.SubMenuLoadoutId.IsNone())
		{
			Button->TargetType = ERTSCommandTargetType::Instant;
		}
		TransientGrid->Buttons.Add(Button);
	};

	for (const FRTSMassUnitCommandSlotDefinition& Slot : Loadout.CommandSlots)
	{
		AddConfiguredButton(Slot);
	}

	if (!Loadout.BackToLoadoutId.IsNone())
	{
		FRTSMassUnitCommandSlotDefinition BackSlot;
		BackSlot.SlotIndex = Loadout.BackButtonSlotIndex;
		BackSlot.CommandTagName = FName(TEXT("RTS.Command.Menu.Back"));
		BackSlot.SubMenuLoadoutId = Loadout.BackToLoadoutId;
		BackSlot.DisplayName = Loadout.BackButtonDisplayName;
		BackSlot.Description = Loadout.BackButtonDescription;
		BackSlot.Hotkey = Loadout.BackButtonHotkey;
		AddConfiguredButton(BackSlot);
	}

	if (TransientGrid->Buttons.Num() == 0)
	{
		MassProtocolGridCache.Remove(Loadout.LoadoutId);
		return nullptr;
	}

	return TransientGrid;
}


void URTSSelectionSubsystem::ClearSelection()
{
	SetSelectedUnits(TArray<AActor*>(), TArray<FEntityHandle>(), ERTSSelectionModifier::Replace);
}

bool URTSSelectionSubsystem::IsValidControlGroupIndex(int32 GroupIndex) const
{
	return GroupIndex >= MinControlGroupIndex && GroupIndex <= MaxControlGroupIndex;
}

void URTSSelectionSubsystem::PruneControlGroup(FRTSControlGroupState& Group)
{
	Group.Actors.RemoveAll([this](const TWeakObjectPtr<AActor>& Actor)
	{
		return !Actor.IsValid() || !IsActorControllable(Actor.Get());
	});

	Group.Entities.RemoveAll([this](const FEntityHandle& Handle)
	{
		return !IsEntityControllable(Handle);
	});

	for (int32 ActorIndex = Group.Actors.Num() - 1; ActorIndex >= 0; --ActorIndex)
	{
		for (int32 PreviousIndex = 0; PreviousIndex < ActorIndex; ++PreviousIndex)
		{
			if (Group.Actors[PreviousIndex] == Group.Actors[ActorIndex])
			{
				Group.Actors.RemoveAt(ActorIndex);
				break;
			}
		}
	}

	for (int32 EntityIndex = Group.Entities.Num() - 1; EntityIndex >= 0; --EntityIndex)
	{
		if (Group.Entities.Find(Group.Entities[EntityIndex]) != EntityIndex)
		{
			Group.Entities.RemoveAt(EntityIndex);
		}
	}
}

void URTSSelectionSubsystem::PruneAllControlGroups()
{
	for (auto It = ControlGroups.CreateIterator(); It; ++It)
	{
		PruneControlGroup(It.Value());
		if (It.Value().Actors.IsEmpty() && It.Value().Entities.IsEmpty())
		{
			It.RemoveCurrent();
		}
	}
}

bool URTSSelectionSubsystem::DoesControlGroupMatchCurrentSelection(const FRTSControlGroupState& Group) const
{
	if (Group.Actors.Num() != SelectedActors.Num() || Group.Entities.Num() != SelectedEntities.Num())
	{
		return false;
	}

	for (AActor* Actor : SelectedActors)
	{
		if (!Group.Actors.ContainsByPredicate([Actor](const TWeakObjectPtr<AActor>& Entry)
		{
			return Entry.Get() == Actor;
		}))
		{
			return false;
		}
	}

	for (const FEntityHandle& Handle : SelectedEntities)
	{
		if (!Group.Entities.Contains(Handle))
		{
			return false;
		}
	}

	return Group.Actors.Num() + Group.Entities.Num() > 0;
}

void URTSSelectionSubsystem::UpdateActiveControlGroupIndex()
{
	PruneAllControlGroups();

	auto IsMatchingGroup = [this](int32 GroupIndex)
	{
		const FRTSControlGroupState* Group = ControlGroups.Find(GroupIndex);
		return Group && DoesControlGroupMatchCurrentSelection(*Group);
	};

	if (IsValidControlGroupIndex(PreferredActiveControlGroupIndex)
		&& IsMatchingGroup(PreferredActiveControlGroupIndex))
	{
		ActiveControlGroupIndex = PreferredActiveControlGroupIndex;
		PreferredActiveControlGroupIndex = INDEX_NONE;
		return;
	}
	PreferredActiveControlGroupIndex = INDEX_NONE;

	if (IsValidControlGroupIndex(ActiveControlGroupIndex) && IsMatchingGroup(ActiveControlGroupIndex))
	{
		return;
	}

	ActiveControlGroupIndex = INDEX_NONE;
	for (const int32 GroupIndex : ControlGroupDisplayOrder)
	{
		if (IsMatchingGroup(GroupIndex))
		{
			ActiveControlGroupIndex = GroupIndex;
			break;
		}
	}
}

bool URTSSelectionSubsystem::AssignCurrentSelectionToControlGroup(int32 GroupIndex, ERTSControlGroupAssignmentMode AssignmentMode)
{
	if (!IsValidControlGroupIndex(GroupIndex))
	{
		return false;
	}

	PruneAllControlGroups();

	if (AssignmentMode == ERTSControlGroupAssignmentMode::StealAndReplace)
	{
		for (auto& Pair : ControlGroups)
		{
			FRTSControlGroupState& ExistingGroup = Pair.Value;
			ExistingGroup.Actors.RemoveAll([this](const TWeakObjectPtr<AActor>& Actor)
			{
				return SelectedActors.Contains(Actor.Get());
			});
			ExistingGroup.Entities.RemoveAll([this](const FEntityHandle& Handle)
			{
				return SelectedEntities.Contains(Handle);
			});
		}
	}

	FRTSControlGroupState& Group = ControlGroups.FindOrAdd(GroupIndex);

	if (AssignmentMode == ERTSControlGroupAssignmentMode::ToggleMembership)
	{
		if (SelectedActors.IsEmpty() && SelectedEntities.IsEmpty())
		{
			return false;
		}

		const bool bContainsEverySelectedActor = Algo::AllOf(SelectedActors, [&Group](AActor* Actor)
		{
			return Group.Actors.ContainsByPredicate([Actor](const TWeakObjectPtr<AActor>& Entry)
			{
				return Entry.Get() == Actor;
			});
		});
		const bool bContainsEverySelectedEntity = Algo::AllOf(SelectedEntities, [&Group](const FEntityHandle& Handle)
		{
			return Group.Entities.Contains(Handle);
		});

		if (bContainsEverySelectedActor && bContainsEverySelectedEntity)
		{
			Group.Actors.RemoveAll([this](const TWeakObjectPtr<AActor>& Actor)
			{
				return SelectedActors.Contains(Actor.Get());
			});
			Group.Entities.RemoveAll([this](const FEntityHandle& Handle)
			{
				return SelectedEntities.Contains(Handle);
			});
		}
		else
		{
			for (AActor* Actor : SelectedActors)
			{
				Group.Actors.AddUnique(Actor);
			}
			for (const FEntityHandle& Handle : SelectedEntities)
			{
				Group.Entities.AddUnique(Handle);
			}
		}
	}
	else
	{
		Group.Actors.Reset();
		Group.Entities = SelectedEntities;
		for (AActor* Actor : SelectedActors)
		{
			Group.Actors.Add(Actor);
		}
	}

	PruneAllControlGroups();
	PreferredActiveControlGroupIndex = GroupIndex;
	UpdateActiveControlGroupIndex();
	BroadcastControlGroupsView();

	UE_LOG(LogORTSSelection, Log, TEXT("ControlGroup %d assigned: mode=%d actors=%d entities=%d"),
		GroupIndex,
		static_cast<int32>(AssignmentMode),
		SelectedActors.Num(),
		SelectedEntities.Num());
	return true;
}

bool URTSSelectionSubsystem::RecallControlGroup(int32 GroupIndex, bool bAddToSelection)
{
	if (!IsValidControlGroupIndex(GroupIndex))
	{
		return false;
	}

	FRTSControlGroupState* Group = ControlGroups.Find(GroupIndex);
	if (!Group)
	{
		return false;
	}

	PruneControlGroup(*Group);
	if (Group->Actors.IsEmpty() && Group->Entities.IsEmpty())
	{
		ControlGroups.Remove(GroupIndex);
		BroadcastControlGroupsView();
		return false;
	}

	TArray<AActor*> Actors;
	Actors.Reserve(Group->Actors.Num());
	for (const TWeakObjectPtr<AActor>& Actor : Group->Actors)
	{
		if (Actor.IsValid())
		{
			Actors.Add(Actor.Get());
		}
	}

	const TArray<FEntityHandle> Entities = Group->Entities;
	PreferredActiveControlGroupIndex = bAddToSelection ? INDEX_NONE : GroupIndex;
	SetSelectedUnits(Actors, Entities, bAddToSelection ? ERTSSelectionModifier::Add : ERTSSelectionModifier::Replace);
	return true;
}

void URTSSelectionSubsystem::ClearControlGroup(int32 GroupIndex)
{
	if (!IsValidControlGroupIndex(GroupIndex))
	{
		return;
	}

	ControlGroups.Remove(GroupIndex);
	if (ActiveControlGroupIndex == GroupIndex)
	{
		ActiveControlGroupIndex = INDEX_NONE;
	}
	UpdateActiveControlGroupIndex();
	BroadcastControlGroupsView();
}

FRTSControlGroupView URTSSelectionSubsystem::BuildControlGroupView(int32 GroupIndex, const FRTSControlGroupState* Group) const
{
	FRTSControlGroupView View;
	View.GroupIndex = GroupIndex;
	View.bActive = GroupIndex == ActiveControlGroupIndex;

	if (!Group)
	{
		return View;
	}

	TMap<FString, FRTSControlGroupComposition> CompositionByKey;
	auto AddData = [&CompositionByKey](const FRTSUnitData& UnitData)
	{
		const FString Key = GetSelectionUnitGroupKey(UnitData);
		if (FRTSControlGroupComposition* Existing = CompositionByKey.Find(Key))
		{
			++Existing->Count;
			Existing->UnitType.Count = Existing->Count;
			Existing->UnitType.SelectionTags.AppendTags(UnitData.SelectionTags);
			return;
		}

		FRTSControlGroupComposition Entry;
		Entry.UnitType = UnitData;
		Entry.UnitType.ActorPtr = nullptr;
		Entry.UnitType.EntityHandle.Reset();
		Entry.UnitType.Count = 1;
		Entry.Count = 1;
		CompositionByKey.Add(Key, MoveTemp(Entry));
	};

	for (const TWeakObjectPtr<AActor>& Actor : Group->Actors)
	{
		if (Actor.IsValid())
		{
			AddData(CreateUnitDataFromActor(Actor.Get()));
			++View.UnitCount;
		}
	}
	for (const FEntityHandle& Handle : Group->Entities)
	{
		if (IsEntityControllable(Handle))
		{
			AddData(CreateUnitDataFromEntity(Handle));
			++View.UnitCount;
		}
	}

	CompositionByKey.GenerateValueArray(View.Composition);
	View.Composition.Sort([](const FRTSControlGroupComposition& A, const FRTSControlGroupComposition& B)
	{
		if (A.Count != B.Count)
		{
			return A.Count > B.Count;
		}
		return A.UnitType.Name < B.UnitType.Name;
	});

	View.bAssigned = View.UnitCount > 0;
	if (!View.Composition.IsEmpty())
	{
		View.RepresentativeUnit = View.Composition[0].UnitType;
		View.RepresentativeUnit.Count = View.UnitCount;
	}
	return View;
}

FRTSControlGroupsView URTSSelectionSubsystem::GetControlGroupsView()
{
	PruneAllControlGroups();
	UpdateActiveControlGroupIndex();

	FRTSControlGroupsView View;
	View.ActiveGroupIndex = ActiveControlGroupIndex;
	View.Groups.Reserve(UE_ARRAY_COUNT(ControlGroupDisplayOrder));
	for (const int32 GroupIndex : ControlGroupDisplayOrder)
	{
		View.Groups.Add(BuildControlGroupView(GroupIndex, ControlGroups.Find(GroupIndex)));
	}
	return View;
}

void URTSSelectionSubsystem::BroadcastControlGroupsView()
{
	OnControlGroupsChanged.Broadcast(GetControlGroupsView());
}

void URTSSelectionSubsystem::RequestControlGroupsRefresh()
{
	BroadcastControlGroupsView();
}

bool URTSSelectionSubsystem::GetControlGroupFocusLocation(int32 GroupIndex, FVector& OutWorldCenter)
{
	FRTSControlGroupState* Group = ControlGroups.Find(GroupIndex);
	if (!Group)
	{
		return false;
	}

	PruneControlGroup(*Group);
	TArray<FVector> UnitLocations;
	UnitLocations.Reserve(Group->Entities.Num() + Group->Actors.Num());

	// Mass is the primary path. FLocating gives us an actual live-unit position,
	// so focus can never land in the empty midpoint between separated formations.
	if (UWorld* World = GetWorld())
	{
		if (UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>())
		{
			FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
			for (const FEntityHandle& Handle : Group->Entities)
			{
				const FMassEntityHandle NativeHandle(Handle.Index, Handle.Serial);
				if (EntityManager.IsEntityActive(NativeHandle))
				{
					if (const FLocating* Locating = EntityManager.GetFragmentDataPtr<FLocating>(NativeHandle))
					{
						UnitLocations.Add(Locating->Location);
					}
				}
			}
		}
	}

	// Actor positions are retained only for explicitly enabled legacy selections.
	for (const TWeakObjectPtr<AActor>& Actor : Group->Actors)
	{
		if (Actor.IsValid())
		{
			UnitLocations.Add(Actor->GetActorLocation());
		}
	}

	if (UnitLocations.IsEmpty())
	{
		return false;
	}

	// A bounded deterministic reservoir sample represents large Mass groups without
	// an O(N^2) scan. Sampling is uniform, so populous clusters contribute more
	// candidates; the trimmed medoid then chooses a real unit inside the dominant
	// dense cluster while ignoring remote split-group outliers.
	TArray<FVector> Samples;
	Samples.Reserve(FMath::Min(UnitLocations.Num(), MaxControlGroupFocusSamples));
	const int32 InitialSampleCount = FMath::Min(UnitLocations.Num(), MaxControlGroupFocusSamples);
	for (int32 Index = 0; Index < InitialSampleCount; ++Index)
	{
		Samples.Add(UnitLocations[Index]);
	}

	if (UnitLocations.Num() > MaxControlGroupFocusSamples)
	{
		const int32 Seed = static_cast<int32>(HashCombine(GetTypeHash(GroupIndex), GetTypeHash(UnitLocations.Num())));
		FRandomStream RandomStream(Seed);
		for (int32 Index = MaxControlGroupFocusSamples; Index < UnitLocations.Num(); ++Index)
		{
			const int32 ReplacementIndex = RandomStream.RandRange(0, Index);
			if (ReplacementIndex < MaxControlGroupFocusSamples)
			{
				Samples[ReplacementIndex] = UnitLocations[Index];
			}
		}
	}

	const int32 RetainedDistanceCount = FMath::Clamp(
		FMath::CeilToInt(Samples.Num() * ControlGroupFocusRetainedFraction),
		1,
		Samples.Num());
	int32 BestSampleIndex = 0;
	double BestDensityScore = TNumericLimits<double>::Max();
	TArray<double> Distances;
	Distances.Reserve(Samples.Num());

	for (int32 CandidateIndex = 0; CandidateIndex < Samples.Num(); ++CandidateIndex)
	{
		Distances.Reset();
		for (const FVector& OtherLocation : Samples)
		{
			Distances.Add(FVector::DistSquared2D(Samples[CandidateIndex], OtherLocation));
		}
		Distances.Sort();

		double DensityScore = 0.0;
		for (int32 DistanceIndex = 0; DistanceIndex < RetainedDistanceCount; ++DistanceIndex)
		{
			DensityScore += Distances[DistanceIndex];
		}
		if (DensityScore < BestDensityScore)
		{
			BestDensityScore = DensityScore;
			BestSampleIndex = CandidateIndex;
		}
	}

	OutWorldCenter = Samples[BestSampleIndex];
	return true;
}

bool URTSSelectionSubsystem::RequestControlGroupFocus(int32 GroupIndex)
{
	FVector WorldCenter = FVector::ZeroVector;
	if (!IsValidControlGroupIndex(GroupIndex) || !GetControlGroupFocusLocation(GroupIndex, WorldCenter))
	{
		return false;
	}

	OnControlGroupFocusRequested.Broadcast(GroupIndex, WorldCenter);
	return true;
}

FGameplayTagContainer URTSSelectionSubsystem::BuildSelectionTags(
	const FString& TypeKey,
	const FString& Role,
	const FGameplayTagContainer& ExplicitTags,
	const FGameplayTag UnitTypeTag) const
{
	FGameplayTagContainer Tags = ExplicitTags;
	const FString SearchText = (TypeKey + TEXT(" ") + Role).ToLower();
	const FGameplayTag InfantryType = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.UnitClass.Infantry")), false);
	const FGameplayTag ArmorType = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.UnitClass.Armor")), false);
	const FGameplayTag ArtilleryType = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.UnitClass.Artillery")), false);
	const FGameplayTag AirType = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.UnitClass.Air")), false);
	const FGameplayTag NavalType = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.UnitClass.Naval")), false);
	const FGameplayTag OfficerType = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.UnitClass.Officer")), false);
	const FGameplayTag StructureType = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.UnitClass.Structure")), false);
	const FGameplayTag StructureRootTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("RTS.Selection.Structure")), false);
	const bool bTypedStructure = StructureType.IsValid()
		&& UnitTypeTag.MatchesTagExact(StructureType);
	const bool bExplicitStructure = (StructureRootTag.IsValid() && Tags.HasTag(StructureRootTag))
		|| bTypedStructure;

	if (InfantryType.IsValid() && UnitTypeTag.MatchesTagExact(InfantryType))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Ground.Infantry"));
		return Tags;
	}
	if (ArmorType.IsValid() && UnitTypeTag.MatchesTagExact(ArmorType))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Ground.Armor"));
		return Tags;
	}
	if (ArtilleryType.IsValid() && UnitTypeTag.MatchesTagExact(ArtilleryType))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Ground.Artillery"));
		return Tags;
	}
	if (AirType.IsValid() && UnitTypeTag.MatchesTagExact(AirType))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Air"));
		return Tags;
	}
	if (NavalType.IsValid() && UnitTypeTag.MatchesTagExact(NavalType))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Naval"));
		return Tags;
	}
	if (OfficerType.IsValid() && UnitTypeTag.MatchesTagExact(OfficerType))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Ground.Engineer"));
		AddSelectionTag(Tags, TEXT("RTS.Selection.Worker"));
		return Tags;
	}

	const bool bStructure = bExplicitStructure || ContainsAny(SearchText,
		{ TEXT("structure"), TEXT("defence"), TEXT("defense"), TEXT("bunker"), TEXT("building"), TEXT("production"),
		  TEXT("barracks"), TEXT("factory"), TEXT("shipyard"), TEXT("airfield"), TEXT("airport"), TEXT("government"),
		  TEXT("research"), TEXT("university"), TEXT("city"), TEXT("port"), TEXT("headquarters"), TEXT("camp"),
		  TEXT("建筑"), TEXT("碉堡"), TEXT("城市"), TEXT("大学"), TEXT("科研"), TEXT("政府"), TEXT("港口"),
		  TEXT("机场"), TEXT("兵营"), TEXT("军营") });

	if (bStructure)
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Structure"));
		if (ContainsAny(SearchText, { TEXT("defence"), TEXT("defense"), TEXT("bunker"), TEXT("防御"), TEXT("碉堡") }))
		{
			AddSelectionTag(Tags, TEXT("RTS.Selection.Structure.Defense"));
		}
		if (ContainsAny(SearchText, { TEXT("city"), TEXT("城市") })) AddSelectionTag(Tags, TEXT("RTS.Selection.Structure.City"));
		if (ContainsAny(SearchText, { TEXT("university"), TEXT("大学") })) AddSelectionTag(Tags, TEXT("RTS.Selection.Structure.University"));
		if (ContainsAny(SearchText, { TEXT("research"), TEXT("科研") })) AddSelectionTag(Tags, TEXT("RTS.Selection.Structure.Research"));
		if (ContainsAny(SearchText, { TEXT("government"), TEXT("政府") })) AddSelectionTag(Tags, TEXT("RTS.Selection.Structure.Government"));
		if (ContainsAny(SearchText, { TEXT("shipyard"), TEXT("port"), TEXT("港口"), TEXT("船厂") })) AddSelectionTag(Tags, TEXT("RTS.Selection.Structure.Port"));
		if (ContainsAny(SearchText, { TEXT("airfield"), TEXT("airport"), TEXT("机场") })) AddSelectionTag(Tags, TEXT("RTS.Selection.Structure.Airport"));
		if (ContainsAny(SearchText, { TEXT("barracks"), TEXT("兵营") })) AddSelectionTag(Tags, TEXT("RTS.Selection.Structure.Barracks"));
		if (ContainsAny(SearchText, { TEXT("camp"), TEXT("base"), TEXT("headquarters"), TEXT("军营"), TEXT("基地") })) AddSelectionTag(Tags, TEXT("RTS.Selection.Structure.MilitaryCamp"));
		return Tags;
	}

	// A subtype without a protocol has no trustworthy semantic class. Defaulting every
	// unknown entity to army previously made newly-authored buildings enter army queries.
	// Such entities remain manually selectable until their country implementation is mapped.
	if (TypeKey.StartsWith(TEXT("MassUnit.SubType.")))
	{
		return Tags;
	}

	AddSelectionTag(Tags, TEXT("RTS.Selection.Army"));
	if (ContainsAny(SearchText, { TEXT("aircraft"), TEXT("airplane"), TEXT("fighter"), TEXT("bomber"), TEXT("helicopter"), TEXT("飞机"), TEXT("空军") }))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Air"));
		return Tags;
	}
	if (ContainsAny(SearchText, { TEXT("navy"), TEXT("naval."), TEXT("ship"), TEXT("vessel"), TEXT("海军"), TEXT("舰") })
		&& !SearchText.Contains(TEXT("navalinfantry")))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Naval"));
		return Tags;
	}

	AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Ground"));
	if (ContainsAny(SearchText, { TEXT("engineer"), TEXT("builder"), TEXT("工兵"), TEXT("工程") }))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Ground.Engineer"));
		AddSelectionTag(Tags, TEXT("RTS.Selection.Worker"));
	}
	else if (ContainsAny(SearchText, { TEXT("artillery"), TEXT("mortar"), TEXT("rocketbattery"), TEXT("anti-tank gun"), TEXT("antitankgun"), TEXT("火炮"), TEXT("迫击炮"), TEXT("火箭炮") }))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Ground.Artillery"));
	}
	else if (ContainsAny(SearchText, { TEXT("armor"), TEXT("armour"), TEXT("tank"), TEXT("vehicle"), TEXT("halftrack"), TEXT("armoredcar"), TEXT("装甲"), TEXT("坦克"), TEXT("战车") }))
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Ground.Armor"));
	}
	else
	{
		AddSelectionTag(Tags, TEXT("RTS.Selection.Army.Ground.Infantry"));
	}

	return Tags;
}

bool URTSSelectionSubsystem::IsMassEntityIdle(const FEntityHandle& Handle) const
{
	UWorld* World = GetWorld();
	UMassEntitySubsystem* MassSubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!MassSubsystem)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	const FMassEntityHandle NativeHandle(Handle.Index, Handle.Serial);
	if (!EntityManager.IsEntityActive(NativeHandle))
	{
		return false;
	}

	const FEntityFlagFragment* Flags = EntityManager.GetFragmentDataPtr<FEntityFlagFragment>(NativeHandle);
	return Flags && Flags->HasFlagByName(FName(TEXT("Idle")));
}

void URTSSelectionSubsystem::CollectUnitsMatchingQuery(
	const FRTSSelectionQuery& Query,
	TArray<AActor*>& OutActors,
	TArray<FEntityHandle>& OutEntities) const
{
	OutActors.Reset();
	OutEntities.Reset();
	bool bHandledByIndexedResolver = false;
	OnResolveQuickSelection().Broadcast(
		this,
		Query,
		OutActors,
		OutEntities,
		bHandledByIndexedResolver);
	if (bHandledByIndexedResolver)
	{
		return;
	}

	// Mass is the primary runtime path. It queries only the minimal identity/team
	// fragments, then resolves the RTS category protocol without touching proxies.
	if (Query.bIncludeMassEntities)
	{
		FEntityQuery EntityQuery;
		EntityQuery.All<FTeam, FSubType>();
		const TArray<FEntityHandle> Candidates = UMassAPIFuncLib::GetMatchingEntities(this, EntityQuery);
		const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();

		UWorld* World = GetWorld();
		UMassEntitySubsystem* MassSubsystem = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
		FMassEntityManager* EntityManager = MassSubsystem ? &MassSubsystem->GetMutableEntityManager() : nullptr;
		for (const FEntityHandle& Handle : Candidates)
		{
			if (!IsEntityControllable(Handle) || (Query.bIdleOnly && !IsMassEntityIdle(Handle)) || !EntityManager)
			{
				continue;
			}

			const FMassEntityHandle NativeHandle(Handle.Index, Handle.Serial);
			const FSubType* SubType = EntityManager->GetFragmentDataPtr<FSubType>(NativeHandle);
			if (!SubType)
			{
				continue;
			}

			const FRTSMassUnitTypeProtocol* Protocol = FindMassUnitTypeProtocolForEntity(
				Settings, *EntityManager, NativeHandle, SubType->Index);
			const FString TypeKey = GetMassProtocolTypeKey(Protocol, SubType->Index);
			const FString Role = Protocol ? Protocol->Role : FString();
			const FGameplayTagContainer ExplicitTags = Protocol ? Protocol->SelectionTags : FGameplayTagContainer();
			const FGameplayTagContainer SelectionTags = BuildSelectionTags(
				TypeKey,
				Role,
				ExplicitTags,
				Protocol ? Protocol->UnitTypeTag : FGameplayTag());
			if (MatchesRequiredSelectionTag(Query, SelectionTags))
			{
				OutEntities.Add(Handle);
			}
		}
	}

	// Actor-backed units are retained as a compatibility path for authored actors.
	if (Query.bIncludeActorUnits)
	{
		UWorld* World = GetWorld();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			URTSSelectable* Selectable = Actor ? Actor->FindComponentByClass<URTSSelectable>() : nullptr;
			if (!Selectable || !IsActorControllable(Actor) || (Query.bIdleOnly && !Selectable->bIsIdle))
			{
				continue;
			}

			// A visual proxy must not be counted a second time beside its Mass entity.
			if (Query.bIncludeMassEntities)
			{
				if (const UMassBattleAgentComponent* MassAgent = Actor->FindComponentByClass<UMassBattleAgentComponent>())
				{
					if (MassAgent->GetEntityHandle().IsSet())
					{
						continue;
					}
				}
			}

			const FString TypeKey = GetActorGroupKey(Actor);
			const FString Role = Actor->GetClass() ? Actor->GetClass()->GetName() : Actor->GetName();
			const FGameplayTagContainer SelectionTags = BuildSelectionTags(TypeKey, Role, Selectable->SelectionTags);
			if (MatchesRequiredSelectionTag(Query, SelectionTags))
			{
				OutActors.Add(Actor);
			}
		}
	}
}

int32 URTSSelectionSubsystem::SelectUnitsByQuery(const FRTSSelectionQuery& Query, ERTSSelectionModifier Modifier)
{
	TArray<AActor*> Actors;
	TArray<FEntityHandle> Entities;
	CollectUnitsMatchingQuery(Query, Actors, Entities);

	if (Actors.IsEmpty() && Entities.IsEmpty() && Modifier != ERTSSelectionModifier::Replace)
	{
		return 0;
	}

	SetSelectedUnits(Actors, Entities, Modifier);
	return Actors.Num() + Entities.Num();
}

int32 URTSSelectionSubsystem::CountUnitsByQuery(const FRTSSelectionQuery& Query) const
{
	TArray<AActor*> Actors;
	TArray<FEntityHandle> Entities;
	CollectUnitsMatchingQuery(Query, Actors, Entities);
	return Actors.Num() + Entities.Num();
}

void URTSSelectionSubsystem::CycleGroup()
{
	if (AvailableGroupKeys.Num() <= 1) return;

	CurrentGroupIndex++;
	if (CurrentGroupIndex >= AvailableGroupKeys.Num()) CurrentGroupIndex = 0;

	const FRTSSelectionView View = BuildSelectionView();
	BroadcastSelectionViewAndGrid(View);
}

void URTSSelectionSubsystem::RemoveUnit(const FRTSUnitData& UnitData)
{
	TArray<AActor*> ActorsToRemove;
	TArray<FEntityHandle> EntitiesToRemove;

	if (UnitData.Count <= 1 && UnitData.ActorPtr) ActorsToRemove.Add(UnitData.ActorPtr);
	else if (UnitData.Count <= 1 && UnitData.EntityHandle.Index != 0) EntitiesToRemove.Add(UnitData.EntityHandle);
	else
	{
		const FString UnitKey = GetSelectionUnitGroupKey(UnitData);
		for (AActor* Act : SelectedActors) if (Act && GetActorGroupKey(Act) == UnitKey) ActorsToRemove.Add(Act);
		for (const FEntityHandle& Handle : SelectedEntities)
		{
			const FRTSUnitData Data = CreateUnitDataFromEntity(Handle);
			if (GetSelectionUnitGroupKey(Data) == UnitKey)
			{
				EntitiesToRemove.Add(Handle);
			}
		}
	}

	SetSelectedUnits(ActorsToRemove, EntitiesToRemove, ERTSSelectionModifier::Remove);
}

void URTSSelectionSubsystem::SelectGroup(const FString& GroupKey)
{
	TArray<AActor*> NewActors;
	TArray<FEntityHandle> NewEntities;

	for (AActor* Act : SelectedActors) if (Act && GetActorGroupKey(Act) == GroupKey) NewActors.Add(Act);
	for (const FEntityHandle& Handle : SelectedEntities)
    {
        FRTSUnitData Data = CreateUnitDataFromEntity(Handle);
        if (GetSelectionUnitGroupKey(Data) == GroupKey) NewEntities.Add(Handle);
    }

	SetSelectedUnits(NewActors, NewEntities, ERTSSelectionModifier::Replace);
}

FRTSUnitData URTSSelectionSubsystem::CreateUnitDataFromActor(AActor* Actor) const
{
	FRTSUnitData Data;
	if (Actor)
	{
		Data.GroupKey = GetActorGroupKey(Actor);
		Data.TypeKey = Data.GroupKey;
		Data.Name = Data.GroupKey;
		Data.ActorPtr = Actor;
		Data.bIsMassEntity = false;
		if (const UMassBattleAgentComponent* MassAgent =
			Actor->FindComponentByClass<UMassBattleAgentComponent>())
		{
			if (MassAgent->AgentConfigAsset)
			{
				// The avatar consumes the unit type asset and creates its own single
				// representative entity; it never captures this battlefield actor.
				Data.UnitAssetPath = MassAgent->AgentConfigAsset->GetPathName();
				ReadCombatStats(Data, &MassAgent->AgentConfigAsset->Attack,
					&MassAgent->AgentConfigAsset->Damage, &MassAgent->AgentConfigAsset->Defence);
			}
		}

		if (auto Selectable = Actor->FindComponentByClass<URTSSelectable>())
		{
			Data.Icon = Selectable->Icon;
			Data.Portrait = Selectable->Avatar
				? Selectable->Avatar
				: Selectable->Icon;
			Data.Health = Selectable->Health;
			Data.MaxHealth = Selectable->MaxHealth;
			Data.Energy = Selectable->Energy;
			Data.MaxEnergy = Selectable->MaxEnergy;
			Data.Shield = Selectable->Shield;
			Data.MaxShield = Selectable->MaxShield;
			const FString ActorRole = Actor->GetClass() ? Actor->GetClass()->GetName() : Actor->GetName();
			Data.SelectionTags = BuildSelectionTags(Data.TypeKey, ActorRole, Selectable->SelectionTags);
		}
		else
		{
			Data.SelectionTags = BuildSelectionTags(Data.TypeKey, FString(), FGameplayTagContainer());
		}

		if (!Data.Icon)
		{
			Data.Icon = LoadDefaultUnitPanelIconBySeed(GetTypeHash(Data.Name));
		}
		if (Actor->Implements<URTSCommandProgressProvider>())
		{
			IRTSCommandProgressProvider::Execute_GetCommandProgressItems(
				Actor,
				Data.CommandProgressItems);
			if (!Data.CommandProgressItems.IsEmpty())
			{
				const FRTSTimedCommandInstance& First =
					Data.CommandProgressItems[0];
				Data.bHasActivity = true;
				Data.ActivityLabel = First.CommandButton
					? First.CommandButton->DisplayName
					: (!First.DisplayName.IsEmpty()
						? First.DisplayName
						: FText::FromName(First.PayloadId));
				Data.ActivityProgress = First.GetProgress01();
				Data.ActivityRemainingSeconds =
					First.GetRemainingSeconds();
				Data.ActivityDurationSeconds = First.DurationSeconds;
				Data.ActivityQueueCount = Data.CommandProgressItems.Num();
			}
		}
		EnsureSelectionDataDefaults(Data, INDEX_NONE, GetTypeHash(Data.Name));
	}
	return Data;
}

FRTSUnitData URTSSelectionSubsystem::CreateUnitDataFromEntity(const FEntityHandle& Handle) const
{
	FRTSUnitData Data;
	Data.bIsMassEntity = true;
	Data.EntityHandle = Handle;

    UWorld* World = GetWorld();
    if (!World) return Data;

    // 普通 Mass 单位 —— 读取 FSubType.Index 作为分组 Key
    if (UMassEntitySubsystem* MassSys = World->GetSubsystem<UMassEntitySubsystem>())
    {
        FMassEntityManager& EM = MassSys->GetMutableEntityManager();
        if (Handle.Index > 0)
        {
            FMassEntityHandle NativeHandle(Handle.Index, Handle.Serial);
            if (EM.IsEntityActive(NativeHandle))
            {
				if (const FSubType* SubFrag = EM.GetFragmentDataPtr<FSubType>(NativeHandle))
				{
					const int32 SubTypeIndex = SubFrag->Index;
					const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();
					const FRTSMassUnitTypeProtocol* Protocol = FindMassUnitTypeProtocolForEntity(
						Settings, EM, NativeHandle, SubTypeIndex);
					const FNetworking* Networking =
						EM.GetFragmentDataPtr<FNetworking>(NativeHandle);
					const FString RuntimeUnitAssetPath = Networking
						&& !Networking->Key.IsNone()
						? Networking->Key.ToString()
						: (Protocol ? Protocol->UnitAssetPath : FString());
					Data.SubTypeIndex = SubTypeIndex;
					Data.TypeKey = GetMassProtocolTypeKey(Protocol, SubTypeIndex);
					Data.GroupKey = Data.TypeKey;
                    Data.Name = GetMassSubtypeDisplayName(SubTypeIndex);
					const FName UnitAssetKey = !RuntimeUnitAssetPath.IsEmpty()
						? FName(*RuntimeUnitAssetPath)
						: NAME_None;
					Data.Icon = GetMassSubtypeUnitPanelIcon(SubTypeIndex);
					Data.Portrait = GetMassUnitPortrait(UnitAssetKey, SubTypeIndex);
					ApplyMassProtocolToUnitData(Data, Protocol, SubTypeIndex);
					Data.UnitAssetPath = RuntimeUnitAssetPath;
					Data.SelectionTags = BuildSelectionTags(
						Data.TypeKey,
						Data.Role,
						Protocol ? Protocol->SelectionTags : FGameplayTagContainer(),
						Protocol ? Protocol->UnitTypeTag : FGameplayTag());

					if (const FHealth* Health = EM.GetFragmentDataPtr<FHealth>(NativeHandle))
					{
						Data.Health = Health->Current;
						Data.MaxHealth = Health->Maximum;
					}

					ReadCombatStats(Data, EM.GetFragmentDataPtr<FAttack>(NativeHandle),
						EM.GetFragmentDataPtr<FDamage>(NativeHandle), EM.GetFragmentDataPtr<FDefence>(NativeHandle));
					EnsureSelectionDataDefaults(Data, SubTypeIndex, static_cast<uint32>(SubTypeIndex));
					OnEnrichMassUnitData().Broadcast(World, Handle, Data);
                    return Data;
                }
            }
        }
    }

    Data.Name = TEXT("Mass Unit");
	Data.GroupKey = FString::Printf(TEXT("MassUnit.Entity.%d"), Handle.Index);
	Data.TypeKey = Data.GroupKey;
	Data.SelectionTags = BuildSelectionTags(Data.TypeKey, FString(), FGameplayTagContainer());
	Data.Icon = LoadDefaultUnitPanelIconBySeed(static_cast<uint32>(Handle.Index));
	EnsureSelectionDataDefaults(Data, INDEX_NONE, static_cast<uint32>(Handle.Index));
	OnEnrichMassUnitData().Broadcast(World, Handle, Data);
	return Data;
}


void URTSSelectionSubsystem::IssueCommand(FGameplayTag CommandTag)
{
    UE_LOG(LogTemp, Log, TEXT("RTSSelectionSubsystem: Command %s Issued to Current Selection."), *CommandTag.ToString());
	const bool bHadSelection = !SelectedEntities.IsEmpty() || !SelectedActors.IsEmpty();

    if (SelectedEntities.Num() > 0)
    {
		bool bHandledByExternalMassSystem = false;
		const FRTSSelectionView View = BuildSelectionView();
		OnHandleMassInstantCommand().Broadcast(this, CommandTag, View, bHandledByExternalMassSystem);

        if (ULocalPlayer* LP = GetLocalPlayer())
        {
            if (!bHandledByExternalMassSystem)
            {
                if (URTSCommandSubsystem* SignalHub = LP->GetSubsystem<URTSCommandSubsystem>())
                {
                    SignalHub->IssueCommand(CommandTag, nullptr);
                }
            }
        }
    }

	for (AActor* Actor : SelectedActors)
	{
		if (Actor && Actor->Implements<URTSCommandInterface>())
		{
			IRTSCommandInterface::Execute_ExecuteCommand(Actor, CommandTag);
			if (ClearsTaskVisualization(CommandTag))
			{
				if (URTSSelectable* Selectable = Actor->FindComponentByClass<URTSSelectable>())
				{
					Selectable->ClearCurrentTaskVisualization();
				}
			}
		}
	}

	if (bHadSelection)
	{
		OnCommandFeedbackIssued.Broadcast(
			CommandTag,
			FVector::ZeroVector,
			false,
			false);
	}
}

void URTSSelectionSubsystem::IssueCommandWithLocation(
	FGameplayTag CommandTag,
	FVector Location,
	bool bQueue,
	bool bForceStrategicNavigation)
{
    UE_LOG(LogTemp, Log, TEXT("RTSSelectionSubsystem: Command %s Issued with Location %s (Queue=%d)"),
		*CommandTag.ToString(), *Location.ToString(), bQueue);
	const bool bHadSelection = !SelectedEntities.IsEmpty() || !SelectedActors.IsEmpty();

    if (SelectedEntities.Num() > 0)
    {
		bool bHandledByExternalMassSystem = false;
		const bool bComposableCommand = IsComposableContextCommand(CommandTag);
		const FRTSSelectionView View = BuildSelectionView();
		{
			// Optional systems (for example production rally points) may consume their
			// compatible slice without hiding the rest of a mixed selection.
			TGuardValue<bool> ExposeAllSelectedGuard(
				bExposeAllSelectedMassForComposableCommand,
				bComposableCommand);
			OnHandleMassLocationCommand().Broadcast(
				this,
				CommandTag,
				Location,
				View,
				bHandledByExternalMassSystem);
		}

        if (ULocalPlayer* LP = GetLocalPlayer())
        {
            if (!bHandledByExternalMassSystem || bComposableCommand)
            {
                if (URTSCommandSubsystem* SignalHub = LP->GetSubsystem<URTSCommandSubsystem>())
                {
                    SignalHub->IssueCommandWithLocation(
						CommandTag,
						Location,
						bQueue,
						bForceStrategicNavigation);
                }
            }
        }
    }

	for (AActor* Actor : SelectedActors)
	{
		if (Actor && Actor->Implements<URTSCommandInterface>())
		{
			IRTSCommandInterface::Execute_ExecuteCommandWithLocation(Actor, CommandTag, Location);
			if (IsComposableContextCommand(CommandTag))
			{
				if (URTSSelectable* Selectable = Actor->FindComponentByClass<URTSSelectable>())
				{
					Selectable->SetCurrentTaskVisualization(CommandTag, Location);
				}
			}
		}
	}

	if (bHadSelection)
	{
		OnCommandFeedbackIssued.Broadcast(CommandTag, Location, true, bQueue);
	}
}

void URTSSelectionSubsystem::IssueCommandWithTarget(FGameplayTag CommandTag, AActor* TargetActor)
{
    UE_LOG(LogTemp, Log, TEXT("RTSSelectionSubsystem: Command %s Issued with TargetActor %s"), *CommandTag.ToString(), TargetActor ? *TargetActor->GetName() : TEXT("NULL"));
	const bool bHadSelection = !SelectedEntities.IsEmpty() || !SelectedActors.IsEmpty();

    if (SelectedEntities.Num() > 0)
    {
		bool bHandledByExternalMassSystem = false;
		const bool bComposableCommand = IsComposableContextCommand(CommandTag);
		const FRTSSelectionView View = BuildSelectionView();
		{
			TGuardValue<bool> ExposeAllSelectedGuard(
				bExposeAllSelectedMassForComposableCommand,
				bComposableCommand);
			OnHandleMassTargetCommand().Broadcast(
				this,
				CommandTag,
				TargetActor,
				View,
				bHandledByExternalMassSystem);
		}

        if (ULocalPlayer* LP = GetLocalPlayer())
        {
            if (!bHandledByExternalMassSystem || bComposableCommand)
            {
                if (URTSCommandSubsystem* SignalHub = LP->GetSubsystem<URTSCommandSubsystem>())
                {
                    SignalHub->IssueCommandWithTarget(CommandTag, TargetActor);
                }
            }
        }
    }

	for (AActor* Actor : SelectedActors)
	{
		if (Actor && Actor->Implements<URTSCommandInterface>())
		{
			IRTSCommandInterface::Execute_ExecuteCommandWithTarget(Actor, CommandTag, TargetActor);
			if (TargetActor && IsComposableContextCommand(CommandTag))
			{
				if (URTSSelectable* Selectable = Actor->FindComponentByClass<URTSSelectable>())
				{
					Selectable->SetCurrentTaskVisualization(CommandTag, TargetActor->GetActorLocation());
				}
			}
		}
	}

	if (bHadSelection && TargetActor)
	{
		OnCommandFeedbackIssued.Broadcast(
			CommandTag,
			TargetActor->GetActorLocation(),
			true,
			false);
	}
}

FString URTSSelectionSubsystem::GetActiveGroupKey() const
{
	if (AvailableGroupKeys.IsValidIndex(CurrentGroupIndex))
	{
		return AvailableGroupKeys[CurrentGroupIndex];
	}

	return FString();
}

TArray<FEntityHandle> URTSSelectionSubsystem::GetActiveMassEntities() const
{
	if (bExposeAllSelectedMassForComposableCommand)
	{
		return SelectedEntities;
	}

	const FString ActiveKey = GetActiveGroupKey();
	if (ActiveKey.IsEmpty())
	{
		return SelectedEntities;
	}

	TArray<FEntityHandle> Result;
	for (const FEntityHandle& Handle : SelectedEntities)
	{
		FRTSUnitData Data = CreateUnitDataFromEntity(Handle);
		if (GetSelectionUnitGroupKey(Data) == ActiveKey)
		{
			Result.Add(Handle);
		}
	}

	if (Result.Num() > 0)
	{
		return Result;
	}

	// A mixed selection can expose an Actor group (for example a building)
	// alongside Mass unit groups. When that Actor group is active, returning all
	// selected Mass entities would make a command intended for the building also
	// move the army. An active Actor group deliberately has no active Mass units.
	for (AActor* Actor : SelectedActors)
	{
		if (Actor && GetActorGroupKey(Actor) == ActiveKey)
		{
			return {};
		}
	}

	// Keep the legacy fallback only for stale/malformed group keys so an ordinary
	// Mass-only selection is not left without a command target.
	return SelectedEntities;
}

AActor* URTSSelectionSubsystem::GetActiveActor() const
{
    if (SelectedActors.Num() == 0) return nullptr;
    const FString ActiveKey = GetActiveGroupKey();
    if (!ActiveKey.IsEmpty())
    {
        for (AActor* Actor : SelectedActors)
        {
            if (Actor && GetActorGroupKey(Actor) == ActiveKey) return Actor;
        }
    }
    return SelectedActors[0];
}
