#include "rtsinputpanelsettings.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Sound/SoundBase.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#endif

USoundBase* URTSInputPanelSettings::ResolveCommandVoice(FName AnnouncerId, FGameplayTag CommandTag) const
{
	const FString Tag = CommandTag.ToString();
	FString Speaker = AnnouncerId.IsNone() ? FString() : AnnouncerId.ToString();
	while (!Speaker.IsEmpty())
	{
		if (const TSoftObjectPtr<USoundBase>* Clip = CommandVoiceClips.Find(FName(*(Speaker + TEXT(".") + Tag))))
			return Clip->LoadSynchronous();
		int32 Dot;
		if (!Speaker.FindLastChar(TEXT('.'), Dot)) break;
		Speaker.LeftInline(Dot);
	}
	const TSoftObjectPtr<USoundBase>* Clip = CommandVoiceClips.Find(CommandTag.GetTagName());
	return Clip ? Clip->LoadSynchronous() : nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCommandVoiceConfigurationTest,
	"Winyunq.Input.UnitPresentation.CommandVoiceConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSCommandVoiceConfigurationTest::RunTest(const FString& Parameters)
{
	URTSInputPanelSettings* Settings = GetMutableDefault<URTSInputPanelSettings>();
	FString ReloadedIni;
	FConfigCacheIni::LoadGlobalIniFile(ReloadedIni, TEXT("RTSInputSystem"), nullptr, true);
	Settings->ReloadConfig();
	const FName Speakers[] = {TEXT("Unit.Default.Test"), TEXT("Unit.China.Test"), TEXT("Unit.Germany.Test"),
		TEXT("Unit.Japan.Test"), TEXT("Structure.Japan.Test"), TEXT("Unit.Britain.Test"), TEXT("Unit.UnitedStates.Test"),
		TEXT("Unit.France.Test"), TEXT("Unit.Italy.Test"), TEXT("Unit.Soviet.Test"), TEXT("Unit.Korea.Test"),
		TEXT("Unit.Spain.Test"), TEXT("Unit.Portugal.Test"), TEXT("Unit.Arabic.Test"), TEXT("Unit.India.Test")};
	const TCHAR* Languages[] = {TEXT("zh_CN"), TEXT("zh_CN"), TEXT("de"), TEXT("ja"), TEXT("ja"),
		TEXT("en"), TEXT("en"), TEXT("fr"), TEXT("it"), TEXT("ru"), TEXT("ko"), TEXT("es"), TEXT("pt_BR"), TEXT("ar"), TEXT("hi")};
	const TCHAR* Commands[] = {TEXT("RTS.Command.Attack"), TEXT("RTS.Command.Hold"), TEXT("RTS.Command.Patrol"),
		TEXT("RTS.Command.Move"), TEXT("RTS.Command.Stop")};
	const TCHAR* Clips[] = {TEXT("01_attack"), TEXT("02_defend"), TEXT("03_patrol"), TEXT("04_move"), TEXT("05_stop")};
	for (int32 Speaker = 0; Speaker < UE_ARRAY_COUNT(Speakers); ++Speaker)
	{
		TSet<USoundBase*> Responses;
		for (int32 Command = 0; Command < UE_ARRAY_COUNT(Commands); ++Command)
		{
			USoundBase* Sound = Settings->ResolveCommandVoice(Speakers[Speaker], FGameplayTag::RequestGameplayTag(Commands[Command]));
			if (!TestNotNull(TEXT("Configured command voice loads"), Sound)) return false;
			TestTrue(TEXT("Unit country resolves to its configured language"),
				Sound->GetPathName().Contains(FString(TEXT("/Assets/")) + Languages[Speaker] + TEXT("/")));
			TestTrue(TEXT("Different orders use their matching voice clips"), Sound->GetName().StartsWith(Clips[Command]));
			Responses.Add(Sound);
		}
		TestEqual(TEXT("Five orders have five distinct responses"), Responses.Num(), 5);
	}
	for (const FRTSMassUnitTypeProtocol& Protocol : Settings->MassUnitTypeProtocols)
		if (!TestNotNull(TEXT("Unit has its generic confirmation voice"), Protocol.ConfirmationSound.LoadSynchronous())) return false;
	return true;
}
#endif

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
