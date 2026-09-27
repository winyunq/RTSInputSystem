#include "rtsinputpanelsettings.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

double URTSInputPanelSettings::GetPlayerSettingNumber(
	const FName GetterFunctionName,
	const double DefaultValue) const
{
	UGameUserSettings* GameUserSettings =
			GEngine ? GEngine->GetGameUserSettings() : nullptr;
		FString PropertyNameString = GetterFunctionName.ToString();
			PropertyNameString.RemoveFromStart(TEXT("Get"));
			PropertyNameString.RemoveFromEnd(TEXT("Index"));
			const UClass* SettingsClass = GameUserSettings ? GameUserSettings->GetClass() : nullptr;
			const FProperty* Property = SettingsClass
				? SettingsClass->FindPropertyByName(FName(*PropertyNameString))
				: nullptr;
			if (!Property && SettingsClass)
			{
				const FName BoolPropertyName(*FString::Printf(TEXT("b%s"), *PropertyNameString));
				Property = SettingsClass->FindPropertyByName(BoolPropertyName);
			}
		if (!GameUserSettings || !Property)
		{
			return DefaultValue;
		}
		const void* Value = Property->ContainerPtrToValuePtr<void>(GameUserSettings);
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			return BoolProperty->GetPropertyValue(Value) ? 1.0 : 0.0;
		}
		if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
		{
			return DoubleProperty->GetPropertyValue(Value);
		}
		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			return FloatProperty->GetPropertyValue(Value);
		}
		if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			return IntProperty->GetPropertyValue(Value);
		}
		return DefaultValue;
}
