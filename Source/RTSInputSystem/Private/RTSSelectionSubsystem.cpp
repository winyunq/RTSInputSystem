/**
 * @file RTSSelectionSubsystem.cpp
 * @brief 实现选区分组、活动面板选择和命令下发。
 * @details 分组时保存一次实体句柄。执行阶段从命令索引取组，原生多组命令拼接一次句柄数组。
 * @code{.unparsed}
 * 选中实体 → HUD 资料 + 共享组句柄
 * 面板命令 + 组类型协议 → 按钮映射 / 响应组映射
 * 执行输入 → 按标签取组 → 业务动作或原生发令 → 确认反馈
 * @endcode
 */
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
#include "Fragments/Render.h"
#include "Rendering/MassBattleAgentRenderer.h"
#include "Subsystems/MassBattleSubsystem.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
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
#include "Engine/ObjectLibrary.h"
#include "UObject/StrongObjectPtr.h"
#include <algorithm>
#include "GameFramework/GameStateBase.h"

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
	UObject* ProgressProvider, FName SourceId, FGuid ResolvedId, bool bCancelled)
{
	if (ProgressProvider) OnCommandProgressChanged.Broadcast(ProgressProvider, SourceId, ResolvedId, bCancelled);
}

void URTSSelectionSubsystem::NotifyUnitHealthChanged(
	const FEntityHandle& Entity, float CurrentHealth, float MaximumHealth)
{
	OnUnitHealthChanged.Broadcast(Entity, CurrentHealth, MaximumHealth);
}

UObject* FRTSUnitData::GetProgressProvider() const
{
	return ProgressProvider ? ProgressProvider.Get() : (CommandContext ? CommandContext.Get() : ActorPtr);
}

bool FRTSUnitData::MatchesProgressSource(UObject* Provider, FName SourceId) const
{
	return Provider && GetProgressProvider() == Provider && ProgressSourceId == SourceId;
}

bool FRTSUnitData::RefreshCommandProgress()
{
	UObject* Provider = GetProgressProvider();
	if (!IsValid(Provider) || !Provider->Implements<URTSCommandProgressProvider>()) return false;
	IRTSCommandProgressProvider::Execute_GetCommandProgressItems(Provider, CommandProgressItems, ProgressSourceId);
	const FRTSTimedCommandInstance* First = CommandProgressItems.FindByPredicate(
		[](const FRTSTimedCommandInstance& Item) { return Item.State != ERTSTimedCommandState::Queued; });
	bHasActivity = First != nullptr;
	ActivityLabel = First
		? (First->CommandButton ? First->CommandButton->DisplayName
			: (!First->DisplayName.IsEmpty() ? First->DisplayName : FText::FromName(First->PayloadId)))
		: FText::GetEmpty();
	ActivityProgress = First ? First->GetProgress01() : 0.0f;
	ActivityRemainingSeconds = First ? First->GetRemainingSeconds() : 0.0f;
	ActivityDurationSeconds = First ? First->DurationSeconds : 0.0f;
	ActivityQueueCount = CommandProgressItems.Num();
	if (bHasProductionCapacity)
	{
		ProductionQueuedOrders = CommandProgressItems.FilterByPredicate(
			[](const FRTSTimedCommandInstance& Item) { return Item.State == ERTSTimedCommandState::Queued; }).Num();
		ProductionBusyLanes = CommandProgressItems.Num() - ProductionQueuedOrders;
	}
	return true;
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

		const FString RelativePath = FPaths::ChangeExtension(DefaultIconFiles[IconIndex], TEXT(""));
		const FString AssetName = FPaths::GetBaseFilename(RelativePath);
		const FString AssetPath = FString::Printf(
			TEXT("/RTSInputSystem/Portraits/%s.%s"), *RelativePath, *AssetName);
		UTexture2D* Texture = FindObject<UTexture2D>(nullptr, *AssetPath);

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
		return FindObject<UTexture2D>(nullptr,
			TEXT("/RTSInputSystem/Portraits/RTS_UnitAvatar_Placeholder_256x512.RTS_UnitAvatar_Placeholder_256x512"));
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

	

	bool ClearsTaskVisualization(const FGameplayTag& CommandTag)
	{
		const FName TagName = CommandTag.GetTagName();
		return TagName == FName(TEXT("RTS.Command.Stop"))
			|| TagName == FName(TEXT("RTS.Command.Hold"));
	}

	URTSCommandButton* CreateDefaultCommandButtonForTag(UObject* Outer, const FGameplayTag& CommandTag)
	{
		URTSCommandButton* Button = NewObject<URTSCommandButton>(Outer);
		Button->CommandTag = CommandTag;
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
		Button->bIsResearch = Slot.bIsResearch;
		Button->bRepeatableResearch = Slot.bRepeatableResearch;
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
	Super::Initialize(Collection );
	
		static TStrongObjectPtr<UObjectLibrary> UIAssets(
			UObjectLibrary::CreateLibrary(UTexture2D::StaticClass(), false, false));
		UIAssets->LoadAssetsFromPaths({
			TEXT("/RTSInputSystem/CommandIcons"), TEXT("/RTSInputSystem/Portraits")} );

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
	OnUnitHealthChanged.Clear();
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
	 const int32 TotalCount = SelectedActors.Num() + SelectedEntities.Num();
	const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
	const int32 SummaryThreshold = Settings
		? FMath::Max(1, Settings->SelectionSummaryThreshold)
		: 16;

	
		View.Mode =  TotalCount == 0 ? ERTSSelectionMode::Empty:TotalCount == 1 ? ERTSSelectionMode::Single
	        : TotalCount <= SummaryThreshold ? ERTSSelectionMode::List : ERTSSelectionMode::Summary;
	    TArray<TSharedPtr<FRTSSelectionView>> Groups;
	        int32 NewActiveIndex = INDEX_NONE;
	    
	    TMap<FString, int32> GroupIndices;
	    TArray<TArray<FEntityHandle>> GroupEntities;
	    TMap<FString, FRTSUnitData> Summaries;
	    const auto AddUnit = [&](FRTSUnitData Data )
	{
		 if ( View .Mode == ERTSSelectionMode::Single) View.SingleUnit = Data;
		        if (View.Mode == ERTSSelectionMode::Summary) AddOrUpdateSummaryGroup(Summaries, Data);
		        else View.Items.Add(Data);
		        if (!Data.bIsMassEntity) return;
		        const FString Key = GetSelectionUnitGroupKey(Data);
		        int32* Existing = GroupIndices.Find(Key);
		        const int32 Index = Existing ? *Existing : Groups.Num();
		        if (!Existing)
		        {
		            GroupIndices.Add(Key, Index);
		            const auto Group = MakeShared<FRTSSelectionView>();
		             Groups.Add( Group );
		            		            Group->Mode = ERTSSelectionMode::Single;
		Group->ActiveGroupKey = Key;
		            Group->SingleUnit = Data;
		            Group->CommandPlayer = GetLocalPlayer();
		GroupEntities.AddDefaulted();
		}
		        GroupEntities[Index].Add(Data.EntityHandle);
		        if (!Data.ProgressSourceId.IsNone()) Groups[Index]->CommandSourceIds.Add(Data.ProgressSourceId);
	}
	;
		for (AActor* Actor : SelectedActors) AddUnit(CreateUnitDataFromActor(Actor));
		for (const FEntityHandle& Entity : SelectedEntities ) AddUnit(CreateUnitDataFromEntity(Entity));
		    for (int32 Index = 0; Index < Groups.Num(); ++Index)
		        Groups[Index]->CommandEntities = MakeShared<const TArray<FEntityHandle>>(MoveTemp(GroupEntities[Index]));
		    if (View.Mode == ERTSSelectionMode::Summary)
		        for (auto& Pair : Summaries ) View.Items.Add(MoveTemp(Pair.Value));
		View.Items.Sort([](const FRTSUnitData& A, const FRTSUnitData& B)
		{
			const int32 Compare = A.Name.Compare(B.Name);
			return Compare == 0 ? GetSelectionUnitGroupKey(A) < GetSelectionUnitGroupKey(B) : Compare < 0;
		});
	

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
		 CommandGroups = MoveTemp(Groups);
		    ActiveCommandGroupIndex = NewActiveIndex;
		    CommandGroupIndex.Reset(); return  CurrentSelectionView = MoveTemp( View ) ;
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

	 if (const int32* Active = GroupIndices.Find(View.ActiveGroupKey)) NewActiveIndex = *Active ; CommandGroups = MoveTemp(Groups);
	     ActiveCommandGroupIndex = NewActiveIndex;
	     CommandGroupIndex.Reset() ; return  CurrentSelectionView = MoveTemp( View ) ;
}

TArray<FRTSUnitData> URTSSelectionSubsystem::GetUnitGroupMembers(const FString& GroupKey) const
{
    const auto Sources = CommandGroups;
    TArray<FRTSUnitData> Members;
    for (const auto& Source : Sources)
    {
        if (!GroupKey.IsEmpty() && Source->ActiveGroupKey != GroupKey) continue;
        for (const FEntityHandle& Entity : *Source->CommandEntities) Members.Add(CreateUnitDataFromEntity(Entity));
    }
    return Members;
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
    const FRTSSelectionView View = BuildSelectionView();
    RebuildCommandIndex(IndexedButtons);
    OnSelectionChanged.Broadcast(View);
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
			 ActiveCommandGrid = NewGrid;
			            CommandGroupIndex.Reset();  RebuildCommandIndex(NewGrid ? NewGrid->GetAllButtons() : TArray<URTSCommandButton*>()); OnCommandNavigationRequested.Broadcast(NewGrid);
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
			 ActiveCommandGrid = NewGrid;
			            CommandGroupIndex.Reset( ); RebuildCommandIndex(NewGrid ? NewGrid->GetAllButtons() : TArray<URTSCommandButton*>() ); OnCommandNavigationRequested.Broadcast(NewGrid);
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

     ActiveCommandGrid = NewGrid;
                CommandGroupIndex.Reset();  RebuildCommandIndex(NewGrid ? NewGrid->GetAllButtons() : TArray<URTSCommandButton*>()); OnCommandNavigationRequested.Broadcast(NewGrid);
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

URTSCommandButton* URTSSelectionSubsystem::FindCommandButton(FGameplayTag CommandTag)
{
	return CommandDefinitions.FindRef ( CommandTag) ;
		
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

	// Cache this panel; submenu commands retain only their target loadout IDs until opened.
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
			if (! FindCommandLoadoutById(Settings, Slot.SubMenuLoadoutId))
				
				return;
			

			URTSCmd_SubMenu* SubMenuButton = NewObject<URTSCmd_SubMenu>(TransientGrid);
			SubMenuButton->TargetLoadoutId = Slot.SubMenuLoadoutId;
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
    CurrentGroupIndex = (CurrentGroupIndex + 1) % AvailableGroupKeys.Num();
    CurrentSelectionView.ActiveGroupKey = AvailableGroupKeys[CurrentGroupIndex];
    ActiveCommandGroupIndex = CommandGroups.IndexOfByPredicate([this](const auto& Group)
    { return Group->ActiveGroupKey == CurrentSelectionView.ActiveGroupKey; });
    CommandGroupIndex.Reset();
    BroadcastSelectionViewAndGrid(CurrentSelectionView);
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
					
					
					ApplyMassProtocolToUnitData(Data, Protocol, SubTypeIndex);
					if (!Data.Icon)
					{
						Data.Icon = GetMassSubtypeUnitPanelIcon(SubTypeIndex);
					}
					if (!Data.Portrait)
					{
						Data.Portrait = GetMassUnitPortrait(UnitAssetKey, SubTypeIndex);
					}
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
    if (URTSCommandButton* Command = FindCommandButton(CommandTag);
            Command && Command->TargetType == ERTSCommandTargetType::Instant)
        {
            Command->Execute();
        }
				
}

void URTSSelectionSubsystem::IssueCommandWithLocation(
	FGameplayTag CommandTag,
	FVector Location,
	bool bQueue,
	bool bForceStrategicNavigation)
{
    SubmitCommandContext(CommandTag, TOptional<bool>(bQueue), &Location, FEntityHandle(), bForceStrategicNavigation);
}

void URTSSelectionSubsystem::IssueCommandWithTarget(FGameplayTag CommandTag, const FEntityHandle& TargetEntity)
{
    IssueCommandWithEntityTarget(CommandTag, TargetEntity);
                
}

FString URTSSelectionSubsystem::GetActiveGroupKey() const
{
	if (AvailableGroupKeys.IsValidIndex(CurrentGroupIndex))
	{
		return AvailableGroupKeys[CurrentGroupIndex];
	}

	return FString();
}

 const TArray<FEntityHandle>  & URTSSelectionSubsystem::GetActiveMassEntities() const
{
	
		return  ActiveCommandGroupIndex != INDEX_NONE ? *CommandGroups[ActiveCommandGroupIndex]->CommandEntities : SelectedEntities;
	
}

UObject* URTSSelectionSubsystem::GetActiveCommandContext() const
{
	const TArray<FEntityHandle> Entities = GetActiveMassEntities();
	if (!Entities.IsEmpty()) return CreateUnitDataFromEntity(Entities[0]).CommandContext.Get();
	if (AActor* Actor = GetActiveActor())
	{
		const FRTSUnitData Data = CreateUnitDataFromActor(Actor);
		return Data.CommandContext ? Data.CommandContext.Get() : Actor;
	}
	return nullptr;
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

void URTSSelectionSubsystem::IssueCommandWithEntityTarget(FGameplayTag CommandTag, const FEntityHandle& TargetEntity)
{
	URTSCommandButton* Command = FindCommandButton(CommandTag);
	    if (!Command) return;
	
	    const ERTSCommandTargetType Type = Command->TargetType;
	    const bool bMatches = TargetEntity.IsSet()
	        ? Type == ERTSCommandTargetType::TargetActor || Type == ERTSCommandTargetType::LocationOrTarget
	        : Type == ERTSCommandTargetType::Instant;
	    if (bMatches) Command->Execute(TargetEntity);
	            
}

 const FRTSSelectionView  & URTSSelectionSubsystem::GetSelectionView()
{
	return CurrentSelectionView;
}

void URTSSelectionSubsystem::NotifyCommandProgressChanged(UObject* ProgressProvider, const FEntityHandle& Entity,
    FName SourceId, FGuid ResolvedId, bool bCancelled)
{
    TArray<FRTSTimedCommandInstance> Items;
    IRTSCommandProgressProvider::Execute_GetCommandProgressItems(ProgressProvider, Items, SourceId);
    const FRTSTimedCommandInstance* First = Items.FindByPredicate(
        [](const FRTSTimedCommandInstance& Item) { return Item.State != ERTSTimedCommandState::Queued; });

    UWorld* World = GetWorld();
    UMassEntitySubsystem* Mass = World ? World->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    FMassEntityManager* Manager = Mass ? &Mass->GetMutableEntityManager() : nullptr;
    const FMassEntityHandle Handle = static_cast<FMassEntityHandle>(Entity);
    const FVisualizing* Visual = Manager && Manager->IsEntityValid(Handle)
        ? Manager->GetFragmentDataPtr<FVisualizing>(Handle) : nullptr;
    AMassBattleAgentRenderer* Renderer = Visual ? Cast<AMassBattleAgentRenderer>(Visual->RendererActor) : nullptr;
    FAgentRenderBatchData* Batch = Renderer ? Renderer->SpawnedRenderBatches.Find(Visual->RenderBatchId) : nullptr;
    if (Batch && Batch->SpawnedNiagaraSystem && Visual->InstanceId >= 0)
    {
        FVector4 Progress(0, 0, 0, 0);
        if (First)
        {
            const double CurrentTime = IRTSCommandProgressProvider::Execute_GetCommandProgressTimeSeconds(ProgressProvider);
                        const double Duration = First->EndTimeSeconds - First->BeginTimeSeconds;
                        const double Rate = First->State == ERTSTimedCommandState::Active && Duration > 0.0 ? 1.0 / Duration : 0.0;
                        Progress = FVector4(World->GetTimeSeconds(), Rate, First->GetProgress01AtTime(CurrentTime), Visual->InstanceId + 1);
        }
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4Value(
            Batch->SpawnedNiagaraSystem, TEXT("User.CommandProgressArray"), Visual->InstanceId, Progress, true);
    }

    NotifyCommandProgressChanged(ProgressProvider, SourceId, ResolvedId, bCancelled);
}



void URTSSelectionSubsystem::SubmitCommandContext(FGameplayTag CommandTag ,
    TOptional<bool> QueueOverride ,
    const FVector* Location, FEntityHandle TargetEntity, bool  bForceStrategicNavigation)
{
    const auto Sources = GetCommandGroups(CommandTag);
    if (Sources.IsEmpty()) return;
    
    bool bQueue = QueueOverride.Get(false);
    if (!QueueOverride.IsSet())
    {
        const APlayerController* Controller = GetLocalPlayer()->GetPlayerController(GetWorld());
        bQueue = Controller && (Controller->IsInputKeyDown(EKeys::LeftShift) || Controller->IsInputKeyDown(EKeys::RightShift));
    }
    const bool bHasTarget = TargetEntity.IsSet();
    bool bAccepted = false;
    const ERTSMoveNavigationScale Scale = bForceStrategicNavigation
        ? ERTSMoveNavigationScale::Strategic : ERTSMoveNavigationScale::Auto;
    TOptional<bool> ConstructionResult;
    if (Location && ConfirmConstructionPlacement)
    {
        static const FGameplayTag BuildTag = FGameplayTag::RequestGameplayTag(FName(TEXT("RTS.Command.Build")), false);
        if (CommandTag.MatchesTag(BuildTag))
        {
            ConstructionResult = ConfirmConstructionPlacement(this, CommandTag, *Location, Sources, bQueue);
        }
    }
    if (ConstructionResult.IsSet())
    {
        bAccepted = ConstructionResult.GetValue();
    }
    else if (Sources.Num() == 1)
    {
        bAccepted = URTSCommandSubsystem::IssueCommandForEntities(
            this, CommandTag, *Sources[0]->CommandEntities, Location, TargetEntity, bQueue, Scale);
    }
    else
    {
        TArray<FEntityHandle> Entities;
        int32 Count = 0;
        for (const auto& Source : Sources) Count += Source->CommandEntities->Num();
        Entities.Reserve(Count);
        for (const auto& Source : Sources) Entities.Append(*Source->CommandEntities);
        bAccepted = URTSCommandSubsystem::IssueCommandForEntities(
            this, CommandTag, Entities, Location, TargetEntity, bQueue, Scale);
    }
    if (!bAccepted) return;
    FVector FeedbackLocation = Location ? *Location : FVector::ZeroVector;
    if (bHasTarget)
    {
        FVector PreviousLocation, InitialLocation;
        UMassBattleFuncLib::GetAgentLocation(this, TargetEntity, FeedbackLocation, PreviousLocation, InitialLocation);
    }
    OnCommandFeedbackIssued.Broadcast(*Sources[0], CommandTag, FeedbackLocation, Location != nullptr || bHasTarget, bQueue);
}

void URTSSelectionSubsystem::RebuildCommandIndex(const TArray<URTSCommandButton*>& PanelButtons)
{
    if (!CommandGroupIndex.IsEmpty() && IndexedButtons == PanelButtons) return;
    IndexedButtons = PanelButtons;
    CommandGroupIndex.Reset();
    CommandDefinitions.Reset();
    TArray<URTSCommandButton*> Buttons = ActiveCommandGrid ? ActiveCommandGrid->GetAllButtons() : TArray<URTSCommandButton*>();
    Buttons.Append(PanelButtons);
    const int32 ActiveIndex = ActiveCommandGroupIndex;
    for (URTSCommandButton* Button : Buttons)
        if (Button)
        {
            CommandDefinitions.Add(Button->CommandTag, Button);
            Button->CommandSelection = this;
        }
    for (const auto& Pair : CommandDefinitions)
    {
        TArray<int32>& Groups = CommandGroupIndex.Add(Pair.Key);
        if (ActiveIndex != INDEX_NONE) Groups.Add(ActiveIndex);
    }
    const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();
    for (int32 Index = 0; Index < CommandGroups.Num(); ++Index)
    {
        if (Index == ActiveIndex) continue;
        const FRTSUnitData& Unit = CommandGroups[Index]->SingleUnit;
        int32 SubType = Unit.SubTypeIndex;
        if (const auto* Protocol = ResolveMassUnitTypeProtocol(Settings, Unit.TypeKey, SubType))
        {
            for (const auto& Pair : CommandDefinitions)
                if (RTSUnitTypeProtocol::ExposesCommandButton(Settings, *Protocol, Pair.Key))
                    CommandGroupIndex.FindChecked(Pair.Key).Add(Index);
        }
        else if (const URTSCommandGridAsset* Grid = Unit.CommandGrid.Get())
            for (const URTSCommandButton* Button : Grid->GetAllButtons())
                if (Button)
                    if (TArray<int32>* Groups = CommandGroupIndex.Find(Button->CommandTag)) Groups->Add(Index);
    }
}



void URTSSelectionSubsystem::ExecuteCommand(URTSCommandButton* Command, const FVector* Location, FEntityHandle Target)
{
    if (Location) Command->Execute(*Location);
    else Command->Execute(Target);
}

TArray<TSharedPtr<FRTSSelectionView>, TInlineAllocator<8>> URTSSelectionSubsystem::GetCommandGroups(FGameplayTag CommandTag) const
{
    TArray<TSharedPtr<FRTSSelectionView>, TInlineAllocator<8>> Sources;
    if (const TArray<int32>* Indices = CommandGroupIndex.Find(CommandTag))
        for (int32 Index : *Indices) Sources.Add(CommandGroups[Index]);
    return Sources;
}

TArray<FName> URTSSelectionSubsystem::GetCommandSourceIds(FGameplayTag CommandTag) const
{
    const auto Sources = GetCommandGroups(CommandTag);
    int32 Count = 0;
    for (const auto& Source : Sources) Count += Source->CommandSourceIds.Num();
    TArray<FName> Ids;
    Ids.Reserve(Count);
    for (const auto& Source : Sources) Ids.Append(Source->CommandSourceIds);
    return Ids;
}

void URTSSelectionSubsystem::NotifyCommandFeedback(FGameplayTag CommandTag)
{
    const TArray<int32>* Indices = CommandGroupIndex.Find(CommandTag);
    if (!Indices || Indices->IsEmpty()) return;
    const auto Source = CommandGroups[(*Indices)[0]];
    OnCommandFeedbackIssued.Broadcast(*Source, CommandTag, FVector::ZeroVector, false, false);
}

void URTSSelectionSubsystem::VisitCommandUnits(FGameplayTag CommandTag, TFunctionRef<bool(const FEntityHandle&)> Visit) const
{
    const auto Sources = GetCommandGroups(CommandTag);
    for (const auto& Source : Sources)
        for (const FEntityHandle& Entity : *Source->CommandEntities)
            if (!Visit(Entity)) return;
}

void URTSSelectionSubsystem::VisitCommandUnits(FGameplayTag CommandTag, FRandomStream& Random, TFunctionRef<bool(const FEntityHandle&)> Visit) const
{
    const auto Sources = GetCommandGroups(CommandTag);
    TArray<int32, TInlineAllocator<9>> Offsets{0};
    for (const auto& Source : Sources) Offsets.Add(Offsets.Last() + Source->CommandEntities->Num());
    TMap<int32, int32, TInlineSetAllocator<8>> Swaps;
    int32 Remaining = Offsets.Last();
    while (Remaining > 0)
    {
        const int32 Pick = Random.RandRange(0, Remaining - 1);
        const int32* Mapped = Swaps.Find(Pick);
        const int32 Unit = Mapped ? *Mapped : Pick;
        const int32 Last = --Remaining;
        const int32* MappedLast = Swaps.Find(Last);
        const int32 LastValue = MappedLast ? *MappedLast : Last;
        if (Pick != Last) Swaps.Add(Pick, LastValue);
        Swaps.Remove(Last);
        const int32 Group = static_cast<int32>(std::upper_bound(Offsets.GetData(), Offsets.GetData() + Offsets.Num(), Unit) - Offsets.GetData()) - 1;
        if (!Visit((*Sources[Group]->CommandEntities)[Unit - Offsets[Group]])) return;
    }
}

void URTSSelectionSubsystem::NavigateCommandLoadout(FName LoadoutId)
{
    const URTSInputPanelSettings* Settings = RTSUnitTypeProtocol::GetSettings();
    if (const auto* Loadout = FindCommandLoadoutById(Settings, LoadoutId))
        if (URTSCommandGridAsset* Grid = ResolveCommandLoadoutGrid(Settings, *Loadout))
            GetLocalPlayer()->GetSubsystem<URTSCommandSubsystem>()->RequestNavigation(Grid, nullptr);
}




