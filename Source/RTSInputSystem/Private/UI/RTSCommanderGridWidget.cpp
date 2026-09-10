#include "UI/RTSCommanderGridWidget.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/UniformGridSlot.h"
#include "Components/InputComponent.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "RTSSelectionSubsystem.h" 
#include "RTSInputPanelSettings.h"
#include "Interfaces/RTSCommandInterface.h" 
#include "RTSSelector.h"
#include "RTSCommandSubsystem.h"
#include "UI/RTSMinimapJumpWidget.h"
#include "UI/RTSTooltipWidget.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"

namespace
{
	constexpr int32 CommandGridColumns = 5;
	constexpr int32 CommandGridRows = 3;
	constexpr int32 CommandGridSlotCount = CommandGridColumns * CommandGridRows;

	ULocalPlayer* ResolveCommanderGridLocalPlayer(const UUserWidget* Widget)
	{
		if (!Widget)
		{
			return nullptr;
		}

		if (ULocalPlayer* LP = Widget->GetOwningLocalPlayer())
		{
			return LP;
		}

		if (APlayerController* PC = Widget->GetOwningPlayer())
		{
			return PC->GetLocalPlayer();
		}

		if (UWorld* World = Widget->GetWorld())
		{
			return World->GetFirstLocalPlayerFromController();
		}

		return nullptr;
	}

	FKey GetDefaultCommandPanelKey(int32 SlotIndex)
	{
		static const FKey DefaultKeys[] =
		{
			EKeys::Q, EKeys::W, EKeys::E, EKeys::R, EKeys::T,
			EKeys::A, EKeys::S, EKeys::D, EKeys::F, EKeys::G,
			EKeys::Z, EKeys::X, EKeys::C, EKeys::V, EKeys::B
		};

		return SlotIndex >= 0 && SlotIndex < UE_ARRAY_COUNT(DefaultKeys)
			? DefaultKeys[SlotIndex]
			: FKey();
	}

	FKey MakeCommandPanelKeyFromName(const FName KeyName)
	{
		static const TMap<FName, FKey> NamedKeys =
		{
			{ FName(TEXT("Q")), EKeys::Q },
			{ FName(TEXT("W")), EKeys::W },
			{ FName(TEXT("E")), EKeys::E },
			{ FName(TEXT("R")), EKeys::R },
			{ FName(TEXT("T")), EKeys::T },
			{ FName(TEXT("A")), EKeys::A },
			{ FName(TEXT("S")), EKeys::S },
			{ FName(TEXT("D")), EKeys::D },
			{ FName(TEXT("F")), EKeys::F },
			{ FName(TEXT("G")), EKeys::G },
			{ FName(TEXT("Z")), EKeys::Z },
			{ FName(TEXT("X")), EKeys::X },
			{ FName(TEXT("C")), EKeys::C },
			{ FName(TEXT("V")), EKeys::V },
			{ FName(TEXT("B")), EKeys::B },
		};

		if (const FKey* ExplicitKey = NamedKeys.Find(KeyName))
		{
			return *ExplicitKey;
		}

		const FKey ConfiguredKey(KeyName);
		return ConfiguredKey.IsValid() ? ConfiguredKey : FKey();
	}

	FKey GetCommandPanelKey(int32 SlotIndex)
	{
		const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
		if (Settings && Settings->CommandPanelSlots.IsValidIndex(SlotIndex))
		{
			const FName KeyName = Settings->CommandPanelSlots[SlotIndex].Hotkey;
			if (!KeyName.IsNone())
			{
				const FKey ConfiguredKey = MakeCommandPanelKeyFromName(KeyName);
				if (ConfiguredKey.IsValid())
				{
					return ConfiguredKey;
				}
			}
		}

		return GetDefaultCommandPanelKey(SlotIndex);
	}

	FKey GetEffectiveCommandPanelKey(const URTSCommandButton* Button, int32 SlotIndex)
	{
		const FKey SlotKey = GetCommandPanelKey(SlotIndex);
		return SlotKey.IsValid()
			? SlotKey
			: (Button ? Button->Hotkey : FKey());
	}

	bool AreCommandPanelHotkeysEnabled()
	{
		const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
		return !Settings || Settings->bEnableCommandPanelHotkeys;
	}

	FVector2D GetViewportSizeInSlateUnits(const UObject* WorldContextObject)
	{
		const FVector2D PixelSize = UWidgetLayoutLibrary::GetViewportSize(WorldContextObject);
		const float ViewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(WorldContextObject), KINDA_SMALL_NUMBER);
		return PixelSize / ViewportScale;
	}

	FVector2D ClampTooltipPosition(const UObject* WorldContextObject, const FVector2D& Position, const FVector2D& Size, float Margin)
	{
		const FVector2D ViewportSize = GetViewportSizeInSlateUnits(WorldContextObject);
		const FVector2D MaxPosition(
			FMath::Max(Margin, ViewportSize.X - Size.X - Margin),
			FMath::Max(Margin, ViewportSize.Y - Size.Y - Margin)
		);

		return FVector2D(
			FMath::Clamp(Position.X, Margin, MaxPosition.X),
			FMath::Clamp(Position.Y, Margin, MaxPosition.Y)
		);
	}

	FVector2D GetTooltipDesiredSize(UUserWidget* Tooltip, const FVector2D& FallbackSize)
	{
		if (!Tooltip)
		{
			return FallbackSize;
		}

		Tooltip->ForceLayoutPrepass();
		const FVector2D DesiredSize = Tooltip->GetDesiredSize();
		return DesiredSize.IsNearlyZero() ? FallbackSize : DesiredSize;
	}

	const TCHAR* GetDefaultCommandIconFileName(const FGameplayTag& CommandTag)
	{
		const FName TagName = CommandTag.GetTagName();
		if (TagName == FName(TEXT("RTS.Command.Move")))
		{
			return TEXT("RTS_Command_Move.png");
		}
		if (TagName == FName(TEXT("RTS.Command.Attack")))
		{
			return TEXT("RTS_Command_Attack.png");
		}
		if (TagName == FName(TEXT("RTS.Command.Stop")))
		{
			return TEXT("RTS_Command_Stop.png");
		}
		if (TagName == FName(TEXT("RTS.Command.Hold")))
		{
			return TEXT("RTS_Command_Hold.png");
		}
		if (TagName == FName(TEXT("RTS.Command.Patrol")))
		{
			return TEXT("RTS_Command_Patrol.png");
		}

		// Authored icon-only government, city and production-menu artwork.
		static const TMap<FName, const TCHAR*> AuthoredIcons = {
			{ FName(TEXT("RTS.Command.Air.OpenProduction")), TEXT("T_WW2_Buildings_Airfield.png") },
			{ FName(TEXT("RTS.Command.Build.Airfield")), TEXT("T_WW2_Buildings_Airfield.png") },
			{ FName(TEXT("RTS.Command.Build.AntiAircraftEmplacement")), TEXT("T_WW2_Units_Germany_20.png") },
			{ FName(TEXT("RTS.Command.Build.AntiTankBunker")), TEXT("T_WW2_Units_Germany_18.png") },
			{ FName(TEXT("RTS.Command.Build.AntiTankObstacle")), TEXT("T_WW2_Buildings_TankTrap.png") },
			{ FName(TEXT("RTS.Command.Build.BarbedWire")), TEXT("T_WW2_Buildings_BarbedWire.png") },
			{ FName(TEXT("RTS.Command.Build.Barracks")), TEXT("T_WW2_Buildings_Barracks.png") },
			{ FName(TEXT("RTS.Command.Build.CoastalBattery")), TEXT("T_WW2_Units_Germany_22.png") },
			{ FName(TEXT("RTS.Command.Build.Factory")), TEXT("T_WW2_Buildings_Factory.png") },
			{ FName(TEXT("RTS.Command.Build.FieldCover")), TEXT("T_WW2_Buildings_Sandbags.png") },
			{ FName(TEXT("RTS.Command.Build.GarrisonBunker")), TEXT("T_WW2_Buildings_Bunker.png") },
			{ FName(TEXT("RTS.Command.Build.IndustrialPark")), TEXT("T_WW2_Buildings_IndustrialPark.png") },
			{ FName(TEXT("RTS.Command.Build.MachineGunBunker")), TEXT("T_WW2_Units_Germany_03.png") },
			{ FName(TEXT("RTS.Command.Build.MortarBunker")), TEXT("T_WW2_Units_Germany_19.png") },
			{ FName(TEXT("RTS.Command.Build.Refinery")), TEXT("T_WW2_Buildings_Refinery.png") },
			{ FName(TEXT("RTS.Command.Build.ResearchCenter")), TEXT("T_WW2_Buildings_ResearchCenter.png") },
			{ FName(TEXT("RTS.Command.Build.Shipyard")), TEXT("T_WW2_Buildings_Shipyard.png") },
			{ FName(TEXT("RTS.Command.Build.TankFactory")), TEXT("T_WW2_Buildings_TankFactory.png") },
			{ FName(TEXT("RTS.Command.Build.University")), TEXT("T_WW2_Buildings_University.png") },
			{ FName(TEXT("RTS.Command.Build.VehicleDepot")), TEXT("T_WW2_Buildings_VehicleFactory.png") },
			{ FName(TEXT("RTS.Command.Build.VehicleFactory")), TEXT("T_WW2_Buildings_VehicleFactory.png") },
			{ FName(TEXT("RTS.Command.City.EstablishCapital")), TEXT("T_WW2_Government_Capital.png") },
			{ FName(TEXT("RTS.Command.City.Transfer")), TEXT("T_WW2_Government_TransferCity.png") },
			{ FName(TEXT("RTS.Command.Decision.OpenPanel")), TEXT("T_WW2_Government_Decision.png") },
			{ FName(TEXT("RTS.Command.Headquarters.Advisor.Enterprise")), TEXT("T_WW2_Government_EconomicAdvisor.png") },
			{ FName(TEXT("RTS.Command.Headquarters.Advisor.Military")), TEXT("T_WW2_Government_MilitaryAdvisor.png") },
			{ FName(TEXT("RTS.Command.Headquarters.Advisor.Political")), TEXT("T_WW2_Government_PoliticalAdvisor.png") },
			{ FName(TEXT("RTS.Command.Headquarters.Diplomacy.Bad")), TEXT("T_WW2_Government_DeclareWar.png") },
			{ FName(TEXT("RTS.Command.Headquarters.Diplomacy.CycleTarget")), TEXT("T_WW2_Government_DiplomaticTarget.png") },
			{ FName(TEXT("RTS.Command.Headquarters.Diplomacy.Good")), TEXT("T_WW2_Government_Alliance.png") },
			{ FName(TEXT("RTS.Command.Headquarters.Focus.Menu")), TEXT("T_WW2_Government_Decision.png") },
			{ FName(TEXT("RTS.Command.Headquarters.Focus.MilitaryTechnology.Menu")), TEXT("T_WW2_Government_MilitaryFocus.png") },
			{ FName(TEXT("RTS.Command.Headquarters.Focus.PoliticalEconomy.Menu")), TEXT("T_WW2_Government_PoliticalFocus.png") },
			{ FName(TEXT("RTS.Command.Headquarters.PlayerSkill")), TEXT("T_WW2_Government_GovernmentSkill.png") },
			{ FName(TEXT("RTS.Command.Headquarters.SelectCities")), TEXT("T_WW2_Government_Cities.png") },
			{ FName(TEXT("RTS.Command.Headquarters.SelectFactories")), TEXT("T_WW2_Government_Factories.png") },
			{ FName(TEXT("RTS.Command.Headquarters.TechAcceleration")), TEXT("T_WW2_Government_TechAcceleration.png") },
			{ FName(TEXT("RTS.Command.Market.OpenForeignUnits")), TEXT("T_WW2_Buildings_ArmsMarket.png") },
			{ FName(TEXT("RTS.Command.Menu.Back")), TEXT("T_WW2_Commands_Back.png") },
			{ FName(TEXT("RTS.Command.Menu.Build.Defense")), TEXT("T_WW2_Buildings_Bunker.png") },
			{ FName(TEXT("RTS.Command.Menu.Build.Production")), TEXT("T_WW2_Buildings_TankFactory.png") },
			{ FName(TEXT("RTS.Command.Navy.OpenProduction")), TEXT("T_WW2_Buildings_Shipyard.png") },
			{ FName(TEXT("RTS.Command.Structure.Upgrade")), TEXT("T_WW2_Government_TechAcceleration.png") },
			{ FName(TEXT("RTS.Command.Train.AntiAirI")), TEXT("T_WW2_Units_Japan_22.png") },
			{ FName(TEXT("RTS.Command.Train.AntiAirII")), TEXT("T_WW2_Units_Japan_22.png") },
			{ FName(TEXT("RTS.Command.Train.AntiTankGunI")), TEXT("T_WW2_Units_Germany_18.png") },
			{ FName(TEXT("RTS.Command.Train.AntiTankGunII")), TEXT("T_WW2_Units_Germany_18.png") },
			{ FName(TEXT("RTS.Command.Train.AntiTankGunIII")), TEXT("T_WW2_Units_Germany_18.png") },
			{ FName(TEXT("RTS.Command.Train.ArmoredPersonnelCarrier")), TEXT("T_WW2_Units_Germany_14.png") },
			{ FName(TEXT("RTS.Command.Train.ArmoredVehicle")), TEXT("T_WW2_Units_Germany_13.png") },
			{ FName(TEXT("RTS.Command.Train.AssaultGun")), TEXT("T_WW2_Units_Germany_17.png") },
			{ FName(TEXT("RTS.Command.Train.HeavyTankI")), TEXT("T_WW2_Units_Germany_16.png") },
			{ FName(TEXT("RTS.Command.Train.HeavyTankII")), TEXT("T_WW2_Units_Germany_16.png") },
			{ FName(TEXT("RTS.Command.Train.HeavyTankIII")), TEXT("T_WW2_Units_Germany_16.png") },
			{ FName(TEXT("RTS.Command.Train.LandingVehicle")), TEXT("T_WW2_Units_Germany_14.png") },
			{ FName(TEXT("RTS.Command.Train.MachineGunTank")), TEXT("T_WW2_Units_Japan_05.png") },
			{ FName(TEXT("RTS.Command.Train.MainBattleTank")), TEXT("T_WW2_Units_Germany_15.png") },
			{ FName(TEXT("RTS.Command.Train.MediumTankI")), TEXT("T_WW2_Units_Japan_06.png") },
			{ FName(TEXT("RTS.Command.Train.MediumTankII")), TEXT("T_WW2_Units_Germany_15.png") },
			{ FName(TEXT("RTS.Command.Train.MediumTankIII")), TEXT("T_WW2_Units_Germany_15.png") },
			{ FName(TEXT("RTS.Command.Train.Militia")), TEXT("T_WW2_Units_Germany_01.png") },
			{ FName(TEXT("RTS.Command.Train.MissileVehicle")), TEXT("T_WW2_Units_Germany_23.png") },
			{ FName(TEXT("RTS.Command.Train.Officer")), TEXT("T_WW2_Units_Germany_11.png") },
			{ FName(TEXT("RTS.Command.Train.OldTank")), TEXT("T_WW2_Units_Japan_05.png") },
			{ FName(TEXT("RTS.Command.Train.RocketArtilleryI")), TEXT("T_WW2_Units_Germany_23.png") },
			{ FName(TEXT("RTS.Command.Train.RocketArtilleryII")), TEXT("T_WW2_Units_Japan_09.png") },
			{ FName(TEXT("RTS.Command.Train.SelfPropelledArtillery105")), TEXT("T_WW2_Units_Germany_17.png") },
			{ FName(TEXT("RTS.Command.Train.SelfPropelledArtillery125")), TEXT("T_WW2_Units_Germany_17.png") },
			{ FName(TEXT("RTS.Command.Train.SelfPropelledArtillery155")), TEXT("T_WW2_Units_Germany_17.png") },
			{ FName(TEXT("RTS.Command.Train.SuperHeavyTank")), TEXT("T_WW2_Units_Germany_16.png") },
			{ FName(TEXT("RTS.Command.Train.TankDestroyerI")), TEXT("T_WW2_Units_Japan_07.png") },
			{ FName(TEXT("RTS.Command.Train.TankDestroyerII")), TEXT("T_WW2_Units_Germany_17.png") },
			{ FName(TEXT("RTS.Command.Train.TankDestroyerIII")), TEXT("T_WW2_Units_Germany_17.png") },
		};
		if (const TCHAR* const* FileName = AuthoredIcons.Find(TagName))
		{
			return *FileName;
		}
		const FString TagString = TagName.ToString();
		if (TagString.StartsWith(TEXT("RTS.Command.Headquarters.Focus.Page.PoliticalEconomy.")))
		{
			return TEXT("T_WW2_Government_PoliticalFocus.png");
		}
		if (TagString.StartsWith(TEXT("RTS.Command.Headquarters.Focus.Page.MilitaryTechnology.")))
		{
			return TEXT("T_WW2_Government_MilitaryFocus.png");
		}
		if (TagString.StartsWith(TEXT("RTS.Command.Headquarters.Focus.Page.")))
		{
			return TEXT("T_WW2_Government_Decision.png");
		}

		// Unknown commands deliberately have no substitute artwork. Their button uses
		// DisplayName as a temporary face until a real icon is authored.
		return nullptr;
	}

	UTexture2D* LoadDefaultCommandIcon(const FGameplayTag& CommandTag)
	{
		static TMap<FName, UTexture2D*> IconCache;

		const TCHAR* IconFileName = GetDefaultCommandIconFileName(CommandTag);
		if (!IconFileName)
		{
			return nullptr;
		}

		const FName CacheKey(IconFileName);
		if (UTexture2D** CachedTexture = IconCache.Find(CacheKey))
		{
			if (IsValid(*CachedTexture))
			{
				return *CachedTexture;
			}
			IconCache.Remove(CacheKey);
		}

		const FString IconPath = FPaths::Combine(
			FPaths::ProjectPluginsDir(),
			TEXT("RTSInputSystem"),
			TEXT("Content"),
			TEXT("CommandIcons"),
			TEXT("Source"),
			IconFileName
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

		if (Texture)
		{
			IconCache.Add(CacheKey, Texture);
		}
		return Texture;
	}

	FText MakeCommandLabelFromTag(const FGameplayTag& CommandTag)
	{
		FString Label = CommandTag.IsValid()
			? CommandTag.GetTagName().ToString()
			: TEXT("Command");
		Label.RemoveFromStart(TEXT("RTS.Command."));
		Label.ReplaceInline(TEXT("."), TEXT(" "));
		return FText::FromString(Label.IsEmpty() ? TEXT("Command") : Label);
	}

	void EnsureCommandButtonPresentation(URTSCommandButton* Button)
	{
		if (!Button)
		{
			return;
		}

		if (!Button->Icon)
		{
			Button->Icon = LoadDefaultCommandIcon(Button->CommandTag);
		}

		if (Button->DisplayName.IsEmpty())
		{
			Button->DisplayName = MakeCommandLabelFromTag(Button->CommandTag);
		}

		if (Button->Description.IsEmpty())
		{
			Button->Description = FText::FromString(FString::Printf(TEXT("Execute %s."), *Button->DisplayName.ToString()));
		}
	}

}

TSharedRef<SWidget> URTSCommanderGridWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Root = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("DefaultCommandGridFrame"));
		Root->SetBrushColor(FLinearColor(0.012f, 0.035f, 0.052f, 0.97f));
		Root->SetPadding(FMargin(6.0f));

		CommandGridPanel = WidgetTree->ConstructWidget<UUniformGridPanel>(
			UUniformGridPanel::StaticClass(), TEXT("CommandGridPanel"));
		Root->SetContent(CommandGridPanel);

		ButtonParams = URTSCommandButtonWidget::StaticClass();
		TooltipClass = URTSTooltipWidget::StaticClass();
		ButtonSize = FVector2D(112.0f, 112.0f);
		SlotPadding = FMargin(3.0f);
		WidgetTree->RootWidget = Root;
	}

	return Super::RebuildWidget();
}

void URTSCommanderGridWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
}

void URTSCommanderGridWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	InitGridSlots();
}

void URTSCommanderGridWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// The command tooltip is part of the command panel, not a mouse-following
	// tooltip. Blueprint defaults from older widget instances must not move it
	// away from the panel.
	bFixedTooltipAboveGrid = true;

	InitGridSlots();
	
    // 绑定全局通知（模块间解耦的通信枢纽）
	if (ULocalPlayer* LP = ResolveCommanderGridLocalPlayer(this))
	{
		if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
		{
			Selection->OnCommandRefreshRequested.AddUniqueDynamic(this, &URTSCommanderGridWidget::OnActorGridChanged);
			Selection->OnCommandNavigationRequested.AddUniqueDynamic(this, &URTSCommanderGridWidget::OnCommandNavigationRequested);
		}

		// 监听低层级指令系统的导航请求 (二进制导航)
		if (URTSCommandSubsystem* SignalHub = LP->GetSubsystem<URTSCommandSubsystem>())
		{
			CommandNavigationHandle = SignalHub->OnNavigationRequested.AddLambda([this](URTSCommandGridAsset* NewGrid, AActor* Context)
			{
				this->UpdateGrid(NewGrid);
			});
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RTSCommanderGridWidget: LocalPlayer not found; command navigation binding skipped."));
	}
	
	// If Debug Asset is set, load it immediately for testing
	if (DebugGridAsset)
	{
		// RefreshGrid(DebugGridAsset->GetAllButtons()); // Need better logic here for sparse array
	}

	RegisterCommandPanelHotkeys();
	UpdateCommandStateVisuals();
}

void URTSCommanderGridWidget::NativeDestruct()
{
	UnregisterCommandPanelHotkeys();

	if (ULocalPlayer* LP = ResolveCommanderGridLocalPlayer(this))
	{
		if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
		{
			Selection->OnCommandRefreshRequested.RemoveDynamic(this, &URTSCommanderGridWidget::OnActorGridChanged);
			Selection->OnCommandNavigationRequested.RemoveDynamic(this, &URTSCommanderGridWidget::OnCommandNavigationRequested);
		}

		if (URTSCommandSubsystem* SignalHub = LP->GetSubsystem<URTSCommandSubsystem>())
		{
			if (CommandNavigationHandle.IsValid())
			{
				SignalHub->OnNavigationRequested.Remove(CommandNavigationHandle);
				CommandNavigationHandle.Reset();
			}
		}
	}

	Super::NativeDestruct();
}

void URTSCommanderGridWidget::InitGridSlots()
{
	if (!CommandGridPanel)
    {
         UE_LOG(LogTemp, Warning, TEXT("RTSCommanderGridWidget: CommandGridPanel is NULL!"));
         return;
    }

    if (!ButtonParams)
    {
         UE_LOG(LogTemp, Warning, TEXT("RTSCommanderGridWidget: ButtonParams is NULL! Please assign a WBP_CommandButton class in the Widget Blueprint Details."));
         return;
    }

	CommandGridPanel->ClearChildren();
	GridButtons.Init(nullptr, CommandGridSlotCount);
	GridSlots.Reset();

	CommandGridPanel->SetSlotPadding(SlotPadding);
	CommandGridPanel->SetMinDesiredSlotWidth(FMath::Max(1.0f, ButtonSize.X));
	CommandGridPanel->SetMinDesiredSlotHeight(FMath::Max(1.0f, ButtonSize.Y));

	// StarCraft-style command card: 15 slots (3 rows x 5 columns).
	for (int32 Row = 0; Row < CommandGridRows; ++Row)
	{
		for (int32 Col = 0; Col < CommandGridColumns; ++Col)
		{
			USizeBox* Cell = WidgetTree->ConstructWidget<USizeBox>();
			Cell->SetWidthOverride(ButtonSize.X);
			Cell->SetHeightOverride(ButtonSize.Y);
			Cell->SetClipping(EWidgetClipping::ClipToBounds);
			UUniformGridSlot* GridSlot = CommandGridPanel->AddChildToUniformGrid(Cell, Row, Col);
			GridSlot->SetHorizontalAlignment(HAlign_Fill);
			GridSlot->SetVerticalAlignment(VAlign_Fill);
			GridSlots.Add(Cell);
		}
	}
}

void URTSCommanderGridWidget::OnSelectionUpdated(const FRTSSelectionView& View)
{
	Super::OnSelectionUpdated(View);
    LastSelectionView = View;

	URTSCommandGridAsset* BaseGrid = nullptr;

	if (ULocalPlayer* LP = ResolveCommanderGridLocalPlayer(this))
	{
		if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
		{
			AActor* ActiveActor = Selection->GetActiveActor();
			ActiveActorPtr = ActiveActor;
			if (ActiveActor && ActiveActor->Implements<URTSCommandInterface>())
			{
				BaseGrid = IRTSCommandInterface::Execute_GetCommandGrid(ActiveActor);
			}
		}
	}

	if (BaseGrid)
	{
		UpdateGrid(BaseGrid);
	}
	else if (View.Items.Num() == 0)
	{
		UpdateGrid(nullptr);
	}

	UpdateCommandStateVisuals();
}

void URTSCommanderGridWidget::UpdateGrid(URTSCommandGridAsset* NewGrid)
{
    // 如果是 NULL，即执行 Reset 操作
    CurrentGridAsset = NewGrid;
    
    TArray<URTSCommandButton*> SparseList;
    SparseList.Init(nullptr, CommandGridSlotCount);
    if (NewGrid)
    {
        PopulateSparseButtons(NewGrid, SparseList);
    }
    
    RefreshGrid(SparseList);
    
    if (NewGrid)
    {
        UE_LOG(LogTemp, Log, TEXT("UI-Grid: Set Grid Asset: %s"), *NewGrid->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("UI-Grid: Grid Reset (Set NULL)"));
    }
}

void URTSCommanderGridWidget::RefreshVisuals()
{
    if (!CurrentGridAsset.IsValid()) return;

    TArray<URTSCommandButton*> SparseList;
    PopulateSparseButtons(CurrentGridAsset.Get(), SparseList);
    
    RefreshGrid(SparseList);
    UE_LOG(LogTemp, Verbose, TEXT("UI-Grid: Visuals Refreshed."));
}

void URTSCommanderGridWidget::PopulateSparseButtons(URTSCommandGridAsset* Grid, TArray<URTSCommandButton*>& OutButtons)
{
    if (!Grid) return;
    OutButtons.Init(nullptr, CommandGridSlotCount);

    // 1. 获取所有按钮（支持虚函数重写，覆盖了单例面板和普通资产面板）
    TArray<URTSCommandButton*> AllButtons = Grid->GetAllButtons();

    // 2. 先尝试放入 PreferredIndex 位置
    TArray<URTSCommandButton*> Untracked;
    for (URTSCommandButton* Btn : AllButtons)
    {
        if (!Btn) continue;
        EnsureCommandButtonPresentation(Btn);

        int32 Idx = Btn->PreferredIndex;
        if (Idx >= 0 && Idx < CommandGridSlotCount && OutButtons[Idx] == nullptr)
        {
            OutButtons[Idx] = Btn;
        }
        else
        {
            Untracked.Add(Btn);
        }
    }

    // 3. 将没有固定位置（或位置冲突）的按钮放入空位
    int32 StartSearch = 0; 
    for (URTSCommandButton* Btn : Untracked)
    {
        for (int32 i = StartSearch; i < CommandGridSlotCount; ++i)
        {
            if (OutButtons[i] == nullptr)
            {
                OutButtons[i] = Btn;
                break;
            }
        }
    }
}

void URTSCommanderGridWidget::RefreshGrid(const TArray<URTSCommandButton*>& Buttons)
{
	if (Buttons.Num() != CommandGridSlotCount || GridSlots.Num() != CommandGridSlotCount) return;

	for (int32 i = 0; i < CommandGridSlotCount; ++i)
	{
		GridSlots[i]->ClearChildren();
		GridButtons[i] = nullptr;
	}
	FRTSUnitData Owner = LastSelectionView.SingleUnit;
	Owner.ActorPtr = ActiveActorPtr.Get();
	for (int32 i = 0; i < CommandGridSlotCount; ++i)
	{
		URTSCommandButtonWidget* Button = GetCommandButtonInstance(Buttons[i], Owner);
		if (!Button || Button->IsProgressItemMode()) continue;
		Button->Init(Buttons[i], Owner.ActorPtr, GetEffectiveCommandPanelKey(Buttons[i], i));
		GridSlots[i]->AddChild(Button);
		GridButtons[i] = Button;
	}

	RebuildCommandPanelHotkeys();
	UpdateCommandStateVisuals();
}

URTSCommandButtonWidget* URTSCommanderGridWidget::GetCommandButtonInstance(
	URTSCommandButton* Definition, const FRTSUnitData& Owner)
{
	if (!Definition || !ButtonParams) return nullptr;
	const bool bPerOwner = Definition->bIsResearch && !Definition->bRepeatableResearch;
	for (URTSCommandButtonWidget* Button : CommandButtonInstances)
		if (Button && Button->GetData() == Definition && (!bPerOwner
			|| (Button->CommandOwnerEntity == Owner.EntityHandle && Button->ContextActor.Get() == Owner.ActorPtr)))
			return Button;
	URTSCommandButtonWidget* Button = CreateWidget<URTSCommandButtonWidget>(this, ButtonParams);
	if (!Button) return nullptr;
	if (USizeBox* RootSize = Cast<USizeBox>(Button->WidgetTree->RootWidget))
	{ RootSize->SetWidthOverride(ButtonSize.X); RootSize->SetHeightOverride(ButtonSize.Y); }
	Button->CommandOwnerEntity = Owner.EntityHandle;
	Button->Init(Definition, Owner.ActorPtr, Definition->Hotkey);
	Button->OnCommandClicked.AddUniqueDynamic(this, &URTSCommanderGridWidget::OnGridButtonClicked);
	CommandButtonInstances.Add(Button);
	return Button;
}

URTSCommandButtonWidget* URTSCommanderGridWidget::AcquireResearchButton(
	const FRTSTimedCommandInstance& Item, const FRTSUnitData& Owner)
{
	if (!Item.InstanceId.IsValid() || !Item.CommandButton || !Item.CommandButton->bIsResearch) return nullptr;
	if (const TObjectPtr<URTSCommandButtonWidget>* Existing = ResearchButtons.Find(Item.InstanceId))
	{
		(*Existing)->InitProgressItem(Item, Owner.ActorPtr, ButtonSize.X);
		return *Existing;
	}
	URTSCommandButtonWidget* Source = GetCommandButtonInstance(Item.CommandButton, Owner);
	if (!Source) return nullptr;
	URTSCommandButtonWidget* Button = Source;
	if (Item.CommandButton->bRepeatableResearch)
	{
		Button = DuplicateObject<URTSCommandButtonWidget>(Source, this,
			MakeUniqueObjectName(this, Source->GetClass(), TEXT("ResearchCopy")));
		Button->Initialize();
		Button->Init(Source->GetData(), Owner.ActorPtr, Source->CommandHotkey);
		Button->bResearchCopy = true;
		Button->CommandHotkey = Source->CommandHotkey;
		Button->DefaultButtonStyle = Source->DefaultButtonStyle;
		Button->bHasDefaultButtonStyle = Source->bHasDefaultButtonStyle;
		Button->DefaultBackgroundColor = Source->DefaultBackgroundColor;
		Button->bHasDefaultBackgroundColor = Source->bHasDefaultBackgroundColor;
	}
	else
	{
		if (Source->IsProgressItemMode()) return nullptr;
		// Reparent this instance. Its command-card cell stays in place and empty.
		Source->RemoveFromParent();
		for (TObjectPtr<URTSCommandButtonWidget>& CardButton : GridButtons)
			if (CardButton == Source) CardButton = nullptr;
		RebuildCommandPanelHotkeys();
	}
	Button->ProgressOwnerEntity = Owner.EntityHandle;
	Button->ProgressOwnerActor = Owner.ActorPtr;
	Button->bReturnOnCancel = false;
	Button->InitProgressItem(Item, Owner.ActorPtr, ButtonSize.X);
	ResearchButtons.Add(Item.InstanceId, Button);
	return Button;
}

void URTSCommanderGridWidget::ReleaseFinishedResearchButtons(const FRTSUnitData& Owner)
{
	bool bChanged = false;
	for (auto It = ResearchButtons.CreateIterator(); It; ++It)
	{
		URTSCommandButtonWidget* Button = It.Value();
		if (Button->ProgressOwnerEntity != Owner.EntityHandle || Button->ProgressOwnerActor.Get() != Owner.ActorPtr) continue;
		if (Owner.CommandProgressItems.ContainsByPredicate([&It](const FRTSTimedCommandInstance& Item)
			{ return Item.InstanceId == It.Key(); })) continue;
		Button->RemoveFromParent();
		if (!Button->bResearchCopy && Button->bReturnOnCancel)
		{
			// Cancellation moves the original back; it never constructs a replacement.
			Button->Init(Button->GetData(), Owner.ActorPtr, Button->CommandHotkey);
		}
		else
		{
			// A queued copy is deleted. A completed one-shot has been consumed.
			CommandButtonInstances.Remove(Button);
		}
		It.RemoveCurrent();
		bChanged = true;
	}
	if (bChanged) RefreshVisuals();
}

void URTSCommanderGridWidget::OnActorGridChanged()
{
    // 该函数现在转发到 RefreshVisuals
    RefreshVisuals();
}

void URTSCommanderGridWidget::OnCommandNavigationRequested(URTSCommandGridAsset* NewGrid)
{
    // 直接从 Subsystem 拿当前激活 Actor，不走 ActiveActorPtr 中间状态
    if (ULocalPlayer* LP = ResolveCommanderGridLocalPlayer(this))
    {
        if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
        {
            ActiveActorPtr = Selection->GetActiveActor();
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("[Grid] Widget recv Navigation: Grid=%s Actor=%s"),
        NewGrid ? *NewGrid->GetName() : TEXT("NULL"),
        ActiveActorPtr.IsValid() ? *ActiveActorPtr->GetName() : TEXT("NULL"));
    UpdateGrid(NewGrid);
}

#include "Data/RTSCmd_SubMenu.h"

void URTSCommanderGridWidget::OnGridButtonClicked(const FGameplayTag& CommandTag)
{
    // 二进制核心：直接执行
    // 理由：虽然 UI 代理传回的是 Tag，但我们立即将其还原回 Button 对象，
    // 以便执行其包含完整 C++ 逻辑的回调函数（Execute），彻底废除“Actor 查找”链路。
    URTSCommandButton* ClickedData = nullptr;
    for (URTSCommandButtonWidget* BtnWidget : GridButtons)
    {
        if (BtnWidget && BtnWidget->GetVisibility() == ESlateVisibility::Visible)
        {
            if (URTSCommandButton* Data = BtnWidget->GetData())
            {
                if (Data->CommandTag.MatchesTagExact(CommandTag))
                {
                    ClickedData = Data;
                    break;
                }
            }
        }
    }

    if (!ClickedData) return;

    if (ULocalPlayer* LP = GetOwningLocalPlayer())
    {
        if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
        {
            // 对于非针对单位的逻辑（如顾问、科技），Actor 指针可能为空，
            // 但对于“兴奋剂”等单位技能，我们需要传入正确的执行者。
            AActor* ActiveActor = Selection->GetActiveActor();
            const bool bNeedsTarget = ClickedData->TargetType == ERTSCommandTargetType::Location ||
                                     ClickedData->TargetType == ERTSCommandTargetType::LocationOrTarget ||
                                     ClickedData->TargetType == ERTSCommandTargetType::TargetActor;

            // Mass-only 选择下，当前没有 ActiveActor 时不能走“直接 Execute on actor”。
            // 用于目标类命令时，进入 RTSSelector 的瞄点模式；否则直接走全局选中执行。
            if (bNeedsTarget)
            {
                if (APlayerController* PC = GetOwningPlayer())
                {
                    if (URTSSelector* Selector = PC->FindComponentByClass<URTSSelector>())
                    {
						if (ClickedData->PlacementFootprintCells.X > 0
							&& ClickedData->PlacementFootprintCells.Y > 0)
						{
							const URTSInputPanelSettings* Settings = GetDefault<URTSInputPanelSettings>();
							const float CellSize = Settings ? Settings->HashGridCellSize : 16.0f;
							Selector->BeginHashGridSelectionWithFootprintAndPreview(
								CommandTag,
								FVector2D(ClickedData->PlacementFootprintCells),
								CellSize,
								ClickedData->PlacementPreviewMesh.LoadSynchronous());
						}
						else
						{
							Selector->BeginTargetingWithType(CommandTag, ClickedData->TargetType);
						}
						UpdateCommandStateVisuals();
                        return;
                    }
                }
                UE_LOG(LogTemp, Warning, TEXT("RTSCommanderGridWidget: Targeting command %s ignored, selector missing."), *CommandTag.ToString());
            }
            else if (ActiveActor && ActiveActor->Implements<URTSCommandInterface>())
            {
                // 战术直达：按钮逻辑自决 (Pure Callback)
                ClickedData->Execute(ActiveActor);
            }
            else
            {
                // Fallback for actor-less selection (pure Mass entities). Let the button
                // asset decide first; the default button implementation still routes
                // ordinary Mass commands through URTSCommandSubsystem.
                ClickedData->Execute(GetOwningPlayer());
            }

			UpdateCommandStateVisuals();
        }
    }
}

// --- Shared Tooltip Implementation ---

void URTSCommanderGridWidget::NotifyButtonHovered(URTSCommandButtonWidget* Btn, URTSCommandButton* Data)
{
    if (!Data) return;

    // Lazy Create
    if (!SharedTooltip && TooltipClass)
    {
        SharedTooltip = CreateWidget<URTSTooltipWidget>(GetOwningPlayer(), TooltipClass);
        if (SharedTooltip)
        {
            SharedTooltip->AddToViewport(100); // High Z-Order
            SharedTooltip->SetAlignmentInViewport(FVector2D::ZeroVector);
            SharedTooltip->SetVisibility(ESlateVisibility::Collapsed);
            UE_LOG(LogTemp, Log, TEXT("Shared Tooltip Created."));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to create Shared Tooltip! Check TooltipClass is valid."));
        }
    }
    else if (!TooltipClass)
    {
         UE_LOG(LogTemp, Warning, TEXT("TooltipClass is NULL in RTSCommanderGridWidget! Please assign WBP_Tooltip."));
    }

    if (SharedTooltip)
    {
        SharedTooltip->UpdateTooltip(Data);
        SharedTooltip->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        PositionSharedTooltip();
        UE_LOG(LogTemp, Verbose, TEXT("Showing Tooltip for: %s"), *Data->DisplayName.ToString());
    }
}

void URTSCommanderGridWidget::NotifyButtonUnhovered(URTSCommandButtonWidget* Btn)
{
    if (SharedTooltip)
    {
        SharedTooltip->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void URTSCommanderGridWidget::RegisterCommandPanelHotkeys()
{
	if (!AreCommandPanelHotkeysEnabled())
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC || CommandPanelInputComponent)
	{
		return;
	}

	UWorld* InputWorld = PC->GetWorld();
	if (!InputWorld)
	{
		return;
	}

	CommandPanelInputComponent = NewObject<UInputComponent>(PC);
	if (!CommandPanelInputComponent)
	{
		return;
	}

	CommandPanelInputComponent->Priority = 5;
	CommandPanelInputComponent->bBlockInput = false;
	CommandPanelInputComponent->RegisterComponentWithWorld(InputWorld);

	PC->PushInputComponent(CommandPanelInputComponent);
	CommandPanelInputOwner = PC;
	RebuildCommandPanelHotkeys();
}

void URTSCommanderGridWidget::RebuildCommandPanelHotkeys()
{
	if (!CommandPanelInputComponent)
	{
		// Some HUD widget trees acquire their owning player after NativeConstruct.
		// A populated command card is a reliable point to retry registration.
		RegisterCommandPanelHotkeys();
		return;
	}

	ClearHeldCommandHotkeyState();
	CommandPanelInputComponent->KeyBindings.Reset();
	TSet<FKey> BoundKeys;

	for (int32 SlotIndex = 0; SlotIndex < GridButtons.Num(); ++SlotIndex)
	{
		URTSCommandButtonWidget* ButtonWidget = GridButtons[SlotIndex];
		URTSCommandButton* ButtonData = ButtonWidget ? ButtonWidget->GetData() : nullptr;
		if (!ButtonData)
		{
			continue;
		}

		const FKey Hotkey = GetEffectiveCommandPanelKey(ButtonData, SlotIndex);
		if (!Hotkey.IsValid() || BoundKeys.Contains(Hotkey))
		{
			continue;
		}
		BoundKeys.Add(Hotkey);

		FInputKeyBinding PressedBinding(FInputChord(Hotkey), IE_Pressed);
		PressedBinding.bConsumeInput = true;
		PressedBinding.KeyDelegate.GetDelegateForManualSet().BindLambda(
			[WeakThis = TWeakObjectPtr<URTSCommanderGridWidget>(this), SlotIndex, Hotkey]()
		{
			if (URTSCommanderGridWidget* Widget = WeakThis.Get())
			{
				Widget->HandleCommandPanelHotkeyPressed(SlotIndex, Hotkey);
			}
		});
		CommandPanelInputComponent->KeyBindings.Add(MoveTemp(PressedBinding));

		FInputKeyBinding RepeatBinding(FInputChord(Hotkey), IE_Repeat);
		RepeatBinding.bConsumeInput = true;
		RepeatBinding.KeyDelegate.GetDelegateForManualSet().BindLambda(
			[WeakThis = TWeakObjectPtr<URTSCommanderGridWidget>(this), SlotIndex, Hotkey]()
		{
			if (URTSCommanderGridWidget* Widget = WeakThis.Get())
			{
				Widget->HandleCommandPanelHotkeyRepeated(SlotIndex, Hotkey);
			}
		});
		CommandPanelInputComponent->KeyBindings.Add(MoveTemp(RepeatBinding));

		FInputKeyBinding ReleasedBinding(FInputChord(Hotkey), IE_Released);
		ReleasedBinding.bConsumeInput = true;
		ReleasedBinding.KeyDelegate.GetDelegateForManualSet().BindLambda(
			[WeakThis = TWeakObjectPtr<URTSCommanderGridWidget>(this), Hotkey]()
		{
			if (URTSCommanderGridWidget* Widget = WeakThis.Get())
			{
				Widget->HandleCommandPanelHotkeyReleased(Hotkey);
			}
		});
		CommandPanelInputComponent->KeyBindings.Add(MoveTemp(ReleasedBinding));
	}

}

void URTSCommanderGridWidget::UnregisterCommandPanelHotkeys()
{
	ClearHeldCommandHotkeyState();

	if (!CommandPanelInputComponent)
	{
		return;
	}

	if (CommandPanelInputOwner.IsValid())
	{
		CommandPanelInputOwner->PopInputComponent(CommandPanelInputComponent);
	}

	CommandPanelInputComponent->DestroyComponent();
	CommandPanelInputComponent = nullptr;
	CommandPanelInputOwner.Reset();
}

void URTSCommanderGridWidget::ConfirmPendingTargetWithHotkey(
	URTSSelector* Selector,
	const FKey& Hotkey,
	bool bRapidFire)
{
	if (!Selector || !Selector->bIsTargeting)
	{
		return;
	}

	if (FSlateApplication::IsInitialized())
	{
		const FVector2D CursorPosition = FSlateApplication::Get().GetCursorPos();
		TArray<UUserWidget*> MinimapWidgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
			this,
			MinimapWidgets,
			URTSMinimapJumpWidget::StaticClass(),
			false);

		for (UUserWidget* CandidateWidget : MinimapWidgets)
		{
			URTSMinimapJumpWidget* MinimapWidget = Cast<URTSMinimapJumpWidget>(CandidateWidget);
			if (MinimapWidget
				&& MinimapWidget->HandlePendingTargetConfirmationAtScreenPosition(CursorPosition))
			{
				UE_CLOG(
					!bRapidFire,
					LogTemp,
					Log,
					TEXT("RTS quick-cast hotkey %s -> confirm current minimap target"),
					*Hotkey.ToString());
				return;
			}
		}
	}

	Selector->CommitPendingTargetingAtCursor();
	UE_CLOG(
		!bRapidFire,
		LogTemp,
		Log,
		TEXT("RTS quick-cast hotkey %s -> confirm current viewport target"),
		*Hotkey.ToString());
}

void URTSCommanderGridWidget::HandleCommandPanelHotkeyPressed(int32 SlotIndex, const FKey& Hotkey)
{
	ClearHeldCommandHotkeyState();

	URTSCommandButtonWidget* ButtonWidget = GridButtons.IsValidIndex(SlotIndex)
		? GridButtons[SlotIndex]
		: nullptr;
	URTSCommandButton* ButtonData = ButtonWidget ? ButtonWidget->GetData() : nullptr;
	if (!ButtonWidget
		|| ButtonWidget->GetVisibility() != ESlateVisibility::Visible
		|| !ButtonData)
	{
		return;
	}

	HeldCommandHotkey = Hotkey;
	HeldCommandSlotIndex = SlotIndex;
	ButtonWidget->SetKeyboardPressed(true);

	bool bConfirmedPendingTarget = false;
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (URTSSelector* Selector = PC->FindComponentByClass<URTSSelector>();
			Selector
				&& Selector->bIsTargeting
				&& Selector->PendingCommandTag.MatchesTagExact(ButtonData->CommandTag))
		{
			ConfirmPendingTargetWithHotkey(Selector, Hotkey);
			bConfirmedPendingTarget = true;
		}
	}

	if (!bConfirmedPendingTarget)
	{
		UE_LOG(LogTemp, Log, TEXT("RTS command hotkey %s -> slot %d"), *Hotkey.ToString(), SlotIndex);
		ExecuteCommandPanelSlot(SlotIndex);
	}
}

void URTSCommanderGridWidget::HandleCommandPanelHotkeyRepeated(
	const int32 SlotIndex,
	const FKey& Hotkey)
{
	if (HeldCommandHotkey != Hotkey || HeldCommandSlotIndex != SlotIndex)
	{
		return;
	}

	APlayerController* PC = CommandPanelInputOwner.Get();
	URTSCommandButtonWidget* ButtonWidget = GridButtons.IsValidIndex(SlotIndex)
		? GridButtons[SlotIndex]
		: nullptr;
	URTSCommandButton* ButtonData = ButtonWidget ? ButtonWidget->GetData() : nullptr;
	URTSSelector* Selector = PC ? PC->FindComponentByClass<URTSSelector>() : nullptr;
	const bool bQueueModifierDown = FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetModifierKeys().IsShiftDown();
	if (!PC
		|| !bQueueModifierDown
		|| !ButtonWidget
		|| ButtonWidget->GetVisibility() != ESlateVisibility::Visible
		|| !ButtonData
		|| GetEffectiveCommandPanelKey(ButtonData, SlotIndex) != Hotkey
		|| !Selector
		|| !Selector->bIsTargeting
		|| !Selector->PendingCommandTag.MatchesTagExact(ButtonData->CommandTag))
	{
		return;
	}

	ConfirmPendingTargetWithHotkey(Selector, Hotkey, true);
}

void URTSCommanderGridWidget::HandleCommandPanelHotkeyReleased(const FKey& Hotkey)
{
	if (HeldCommandHotkey == Hotkey)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("RTS command hotkey %s -> released"),
			*Hotkey.ToString());
		ClearHeldCommandHotkeyState();
	}
}

void URTSCommanderGridWidget::ClearHeldCommandHotkeyState()
{
	if (GridButtons.IsValidIndex(HeldCommandSlotIndex))
	{
		if (URTSCommandButtonWidget* ButtonWidget =
			GridButtons[HeldCommandSlotIndex])
		{
			ButtonWidget->SetKeyboardPressed(false);
		}
	}
	HeldCommandHotkey = FKey();
	HeldCommandSlotIndex = INDEX_NONE;
}

void URTSCommanderGridWidget::ExecuteCommandPanelSlot(int32 SlotIndex)
{
	if (!GridButtons.IsValidIndex(SlotIndex))
	{
		return;
	}

	URTSCommandButtonWidget* ButtonWidget = GridButtons[SlotIndex];
	if (!ButtonWidget || ButtonWidget->GetVisibility() != ESlateVisibility::Visible)
	{
		return;
	}

	URTSCommandButton* ButtonData = ButtonWidget->GetData();
	if (!ButtonData)
	{
		return;
	}

	OnGridButtonClicked(ButtonData->CommandTag);
}

void URTSCommanderGridWidget::UpdateCommandStateVisuals()
{
	FGameplayTag ActiveCommandTag;
	if (ULocalPlayer* LP = ResolveCommanderGridLocalPlayer(this))
	{
		APlayerController* PC = GetOwningPlayer();
		if (!PC)
		{
			PC = LP->GetPlayerController(GetWorld());
		}

		if (URTSSelector* Selector = PC ? PC->FindComponentByClass<URTSSelector>() : nullptr;
			Selector && Selector->bIsTargeting)
		{
			ActiveCommandTag = Selector->PendingCommandTag;
		}
		else if (URTSSelectionSubsystem* Selection = LP->GetSubsystem<URTSSelectionSubsystem>())
		{
			if (URTSCommandSubsystem* Commands = LP->GetSubsystem<URTSCommandSubsystem>())
			{
				ActiveCommandTag = Commands->GetActiveCommandTag(Selection->GetActiveMassEntities());
			}
		}
	}

	for (URTSCommandButtonWidget* ButtonWidget : GridButtons)
	{
		if (!ButtonWidget)
		{
			continue;
		}

		ButtonWidget->RefreshCommandState();
		URTSCommandButton* ButtonData = ButtonWidget->GetData();
		ButtonWidget->SetCommandActive(
			ButtonData
			&& ActiveCommandTag.IsValid()
			&& ButtonData->CommandTag.MatchesTagExact(ActiveCommandTag));
	}
}

void URTSCommanderGridWidget::PositionSharedTooltip()
{
	if (!SharedTooltip
		|| SharedTooltip->GetVisibility() != ESlateVisibility::SelfHitTestInvisible)
	{
		return;
	}

	const FVector2D TooltipSize =
		GetTooltipDesiredSize(SharedTooltip, FVector2D(380.0f, 220.0f));

	if (bFixedTooltipAboveGrid)
	{
		UWidget* CommandFrame = WidgetTree
			? WidgetTree->FindWidget(TEXT("CommandFrameBox"))
			: nullptr;
		const FGeometry AnchorGeometry = CommandFrame
			? CommandFrame->GetCachedGeometry()
			: CommandGridPanel
			? CommandGridPanel->GetCachedGeometry()
			: GetCachedGeometry();
		FVector2D PixelPosition;
		FVector2D AnchorViewportPosition;
		USlateBlueprintLibrary::AbsoluteToViewport(
			this,
			AnchorGeometry.GetAbsolutePosition(),
			PixelPosition,
			AnchorViewportPosition);

		const FVector2D AnchorSize = AnchorGeometry.GetLocalSize();
		const FVector2D PanelTooltipSize(
			FMath::Max(AnchorSize.X, 1.0f),
			TooltipSize.Y);
		SharedTooltip->SetDesiredSizeInViewport(PanelTooltipSize);
		FVector2D FinalPos(
			AnchorViewportPosition.X,
			AnchorViewportPosition.Y - PanelTooltipSize.Y + TooltipYOffset);

		FinalPos = ClampTooltipPosition(
			this, FinalPos, PanelTooltipSize, TooltipViewportMargin);
		SharedTooltip->SetPositionInViewport(FinalPos, false);
		return;
	}

	const FVector2D MousePos =
		UWidgetLayoutLibrary::GetMousePositionOnViewport(this);
	const FVector2D ViewportSize = GetViewportSizeInSlateUnits(this);

	FVector2D FinalPos = MousePos + TooltipMouseOffset;
	if (FinalPos.X + TooltipSize.X > ViewportSize.X - TooltipViewportMargin)
	{
		FinalPos.X = MousePos.X - TooltipSize.X - TooltipMouseOffset.X;
	}
	if (FinalPos.Y + TooltipSize.Y > ViewportSize.Y - TooltipViewportMargin)
	{
		FinalPos.Y = MousePos.Y - TooltipSize.Y - TooltipMouseOffset.Y;
	}

	FinalPos = ClampTooltipPosition(
		this, FinalPos, TooltipSize, TooltipViewportMargin);
	SharedTooltip->SetPositionInViewport(FinalPos, false);
}
