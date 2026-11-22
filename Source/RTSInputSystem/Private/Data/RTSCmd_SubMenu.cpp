// Copyright 2026 Winyunq. All Rights Reserved.
#include "Data/RTSCmd_SubMenu.h"
#include "RTSCommandSubsystem.h"
#include "RTSSelectionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

void URTSCmd_SubMenu::Execute(FEntityHandle Target)
{
    if (!TargetLoadoutId.IsNone()) CommandSelection->NavigateCommandLoadout(TargetLoadoutId);
    else if (TargetGrid) CommandSelection->GetLocalPlayer()->GetSubsystem<URTSCommandSubsystem>()->RequestNavigation(TargetGrid, nullptr);
}


