
#include "VRGlobalSettings.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(VRGlobalSettings)

#include "Chaos/ChaosConstraintSettings.h"
#include "Grippables/GrippableSkeletalMeshComponent.h"

UVRGlobalSettings::UVRGlobalSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	bLerpHybridWithSweepGrips(true),
	bOnlyLerpHybridRotation(false),
	bHybridWithSweepUseDistanceBasedLerp(true),
	HybridWithSweepLerpDuration(0.2f),
	bUseGlobalLerpToHand(false),
	bSkipLerpToHandIfHeld(false),
	MinDistanceForLerp(10.0f),
	LerpDuration(0.25f),
	MinSpeedForLerp(100.f),
	MaxSpeedForLerp(500.f),
	LerpInterpolationMode(EVRLerpInterpolationMode::QuatInterp),
	bUseCurve(false),
	OneEuroMinCutoff(0.1f),
	OneEuroCutoffSlope(10.0f),
	OneEuroDeltaCutoff(10.0f),
	CurrentControllerProfileInUse(NAME_None),
	CurrentControllerProfileTransform(FTransform::Identity),
	bUseSeperateHandTransforms(false),
	CurrentControllerProfileTransformRight(FTransform::Identity)
{
		DefaultGrippableCharacterMeshComponentClass = UGrippableSkeletalMeshComponent::StaticClass();

		bUseCollisionModificationForCollisionIgnore = false;
		CollisionIgnoreSubsystemUpdateRate = 1.f;

		bUseChaosTranslationScalers = false;
		bSetEngineChaosScalers = false;
		LinearDriveStiffnessScale = 1.0f;// Chaos::ConstraintSettings::LinearDriveStiffnessScale();
		LinearDriveDampingScale = 1.0f;// Chaos::ConstraintSettings::LinearDriveDampingScale();
		AngularDriveStiffnessScale = 0.3f; // 1.5f// Chaos::ConstraintSettings::AngularDriveStiffnessScale();
		AngularDriveDampingScale = 0.3f; // 1.5f// Chaos::ConstraintSettings::AngularDriveDampingScale();

		// Constraint settings
		JointStiffness = 1.0f;// Chaos::ConstraintSettings::JointStiffness();
		SoftLinearStiffnessScale = 1.5f;// Chaos::ConstraintSettings::SoftLinearStiffnessScale();
		SoftLinearDampingScale = 1.2f;// Chaos::ConstraintSettings::SoftLinearDampingScale();
		SoftAngularStiffnessScale = 100000.f;// Chaos::ConstraintSettings::SoftAngularStiffnessScale();
		SoftAngularDampingScale = 1000.f;// Chaos::ConstraintSettings::SoftAngularDampingScale();
		JointLinearBreakScale = 1.0f; //Chaos::ConstraintSettings::LinearBreakScale();
		JointAngularBreakScale = 1.0f; //Chaos::ConstraintSettings::AngularBreakScale();

}

TSubclassOf<class UGrippableSkeletalMeshComponent> UVRGlobalSettings::GetDefaultGrippableCharacterMeshComponentClass()
{
	const UVRGlobalSettings* VRSettings = GetDefault<UVRGlobalSettings>();

	if (VRSettings)
	{
		// Using a getter to stay safe from bricking peoples projects if they set it to none somehow
		if (VRSettings->DefaultGrippableCharacterMeshComponentClass != nullptr)
		{
			return VRSettings->DefaultGrippableCharacterMeshComponentClass;
		}
	}

	return UGrippableSkeletalMeshComponent::StaticClass();
}

bool UVRGlobalSettings::IsGlobalLerpEnabled()
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();
	return VRSettings.bUseGlobalLerpToHand;
}

FTransform UVRGlobalSettings::AdjustTransformByControllerProfile(FName OptionalControllerProfileName, const FTransform& SocketTransform, bool bIsRightHand)
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

	if (OptionalControllerProfileName == NAME_None)
	{
		if (VRSettings.CurrentControllerProfileInUse != NAME_None)
		{
			// Use currently loaded transform
			return SocketTransform * (((bIsRightHand && VRSettings.bUseSeperateHandTransforms) ? VRSettings.CurrentControllerProfileTransformRight : VRSettings.CurrentControllerProfileTransform));
		}

		// No override and no default, return base transform back
		return SocketTransform;
	}

	// Had an override, find it if possible and use its transform
	const FBPVRControllerProfile* FoundProfile = VRSettings.ControllerProfiles.FindByPredicate([OptionalControllerProfileName](const FBPVRControllerProfile& ArrayItem)
		{
			return ArrayItem.ControllerName == OptionalControllerProfileName;
		});

	if (FoundProfile)
	{
		return SocketTransform * (((bIsRightHand && VRSettings.bUseSeperateHandTransforms) ? FoundProfile->SocketOffsetTransformRightHand : FoundProfile->SocketOffsetTransform));
	}

	// Couldn't find it, return base transform
	return SocketTransform;
}


FTransform UVRGlobalSettings::AdjustTransformByGivenControllerProfile(UPARAM(ref) FBPVRControllerProfile& ControllerProfile, const FTransform& SocketTransform, bool bIsRightHand)
{
	// Use currently loaded transform
	return SocketTransform * (((bIsRightHand && ControllerProfile.bUseSeperateHandOffsetTransforms) ? ControllerProfile.SocketOffsetTransformRightHand : ControllerProfile.SocketOffsetTransform));
}

TArray<FBPVRControllerProfile> UVRGlobalSettings::GetControllerProfiles()
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

	return VRSettings.ControllerProfiles;
}

void UVRGlobalSettings::OverwriteControllerProfile(UPARAM(ref)FBPVRControllerProfile& OverwritingProfile, bool bSaveOutToConfig)
{
	UVRGlobalSettings& VRSettings = *GetMutableDefault<UVRGlobalSettings>();

	for (int i = 0; i < VRSettings.ControllerProfiles.Num(); ++i)
	{
		if (VRSettings.ControllerProfiles[i].ControllerName == OverwritingProfile.ControllerName)
		{
			VRSettings.ControllerProfiles[i] = OverwritingProfile;
		}
	}

	if (bSaveOutToConfig)
		SaveControllerProfiles();
}

void UVRGlobalSettings::AddControllerProfile(UPARAM(ref)FBPVRControllerProfile& NewProfile, bool bSaveOutToConfig)
{
	UVRGlobalSettings& VRSettings = *GetMutableDefault<UVRGlobalSettings>();

	VRSettings.ControllerProfiles.Add(NewProfile);

	if (bSaveOutToConfig)
		SaveControllerProfiles();
}

void UVRGlobalSettings::DeleteControllerProfile(FName ControllerProfileName, bool bSaveOutToConfig)
{
	UVRGlobalSettings& VRSettings = *GetMutableDefault<UVRGlobalSettings>();

	for (int i = VRSettings.ControllerProfiles.Num() - 1; i >= 0; --i)
	{
		if (VRSettings.ControllerProfiles[i].ControllerName == ControllerProfileName)
		{
			VRSettings.ControllerProfiles.RemoveAt(i);
		}
	}

	if (bSaveOutToConfig)
		SaveControllerProfiles();
}

void UVRGlobalSettings::SaveControllerProfiles()
{
	UVRGlobalSettings& VRSettings = *GetMutableDefault<UVRGlobalSettings>();
	VRSettings.SaveConfig();

	//VRSettings.SaveConfig(CPF_Config, *VRSettings.GetGlobalUserConfigFilename());//VRSettings.GetDefaultConfigFilename());
}


FName UVRGlobalSettings::GetCurrentProfileName(bool& bHadLoadedProfile)
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

	bHadLoadedProfile = VRSettings.CurrentControllerProfileInUse != NAME_None;
	return VRSettings.CurrentControllerProfileInUse;
}

FBPVRControllerProfile UVRGlobalSettings::GetCurrentProfile(bool& bHadLoadedProfile)
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

	FName ControllerProfileName = VRSettings.CurrentControllerProfileInUse;
	const FBPVRControllerProfile* FoundProfile = VRSettings.ControllerProfiles.FindByPredicate([ControllerProfileName](const FBPVRControllerProfile& ArrayItem)
		{
			return ArrayItem.ControllerName == ControllerProfileName;
		});

	bHadLoadedProfile = FoundProfile != nullptr;

	if (bHadLoadedProfile)
	{
		return *FoundProfile;
	}
	else
		return FBPVRControllerProfile();
}

bool UVRGlobalSettings::GetControllerProfile(FName ControllerProfileName, FBPVRControllerProfile& OutProfile)
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

	const FBPVRControllerProfile* FoundProfile = VRSettings.ControllerProfiles.FindByPredicate([ControllerProfileName](const FBPVRControllerProfile& ArrayItem)
		{
			return ArrayItem.ControllerName == ControllerProfileName;
		});

	if (FoundProfile)
	{
		OutProfile = *FoundProfile;
		return true;
	}

	return false;
}

bool UVRGlobalSettings::LoadControllerProfileByName(FName ControllerProfileName, bool bSetAsCurrentProfile)
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

	const FBPVRControllerProfile* FoundProfile = VRSettings.ControllerProfiles.FindByPredicate([ControllerProfileName](const FBPVRControllerProfile& ArrayItem)
		{
			return ArrayItem.ControllerName == ControllerProfileName;
		});

	if (FoundProfile)
	{
		return LoadControllerProfile(*FoundProfile, bSetAsCurrentProfile);
	}
	

	UE_LOG(LogTemp, Warning, TEXT("Could not find controller profile!: %s"), *ControllerProfileName.ToString());
	return false;
}

bool UVRGlobalSettings::LoadControllerProfile(const FBPVRControllerProfile& ControllerProfile, bool bSetAsCurrentProfile)
{

	/*
	UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
	if (false)//InputSettings != nullptr)
	{
		if (ControllerProfile.ActionOverrides.Num() > 0)
		{
			// Load button mappings
			for (auto& Elem : ControllerProfile.ActionOverrides)
			{
				FName ActionName = Elem.Key;
				FActionMappingDetails Mapping = Elem.Value;

				// We allow for 0 mapped actions here in case you want to delete one
				if (ActionName == NAME_None)
					continue;

				const TArray<FInputActionKeyMapping>& ActionMappings = InputSettings->GetActionMappings();
				for (int32 ActionIndex = ActionMappings.Num() - 1; ActionIndex >= 0; --ActionIndex)
				{
					if (ActionMappings[ActionIndex].ActionName == ActionName)
					{
						InputSettings->RemoveActionMapping(ActionMappings[ActionIndex], false);
						// we don't break because the mapping may have been in the array twice
					}
				}

				// Then add the new bindings
				for (FInputActionKeyMapping &KeyMapping : Mapping.ActionMappings)
				{
					// By default the key mappings don't have an action name, add them here
					KeyMapping.ActionName = ActionName;
					InputSettings->AddActionMapping(KeyMapping, false);
				}
			}
		}

		if (ControllerProfile.AxisOverrides.Num() > 0)
		{
			// Load axis mappings
			for (auto& Elem : ControllerProfile.AxisOverrides)
			{
				FName AxisName = Elem.Key;
				FAxisMappingDetails Mapping = Elem.Value;

				// We allow for 0 mapped Axis's here in case you want to delete one
				if (AxisName == NAME_None)
					continue;

				const TArray<FInputAxisKeyMapping>& AxisMappings = InputSettings->GetAxisMappings();

				// Clear all Axis's that use our Axis name first
				for (int32 AxisIndex = AxisMappings.Num() - 1; AxisIndex >= 0; --AxisIndex)
				{
					if (AxisMappings[AxisIndex].AxisName == AxisName)
					{
						InputSettings->RemoveAxisMapping(AxisMappings[AxisIndex], false);
						// we don't break because the mapping may have been in the array twice
					}
				}

				// Then add the new bindings
				for (FInputAxisKeyMapping &KeyMapping : Mapping.AxisMappings)
				{
					// By default the key mappings don't have an Axis name, add them here
					KeyMapping.AxisName = AxisName;
					InputSettings->AddAxisMapping(KeyMapping, false);
				}
			}
		}

		if (ControllerProfile.ActionOverrides.Num() > 0 || ControllerProfile.AxisOverrides.Num() > 0)
		{
			// Tell all players to use the new keymappings
			InputSettings->ForceRebuildKeymaps();
		}
	}*/

	if (bSetAsCurrentProfile)
	{
		UVRGlobalSettings* VRSettings = GetMutableDefault<UVRGlobalSettings>();
		if (VRSettings)
		{
			VRSettings->CurrentControllerProfileInUse = ControllerProfile.ControllerName;
			VRSettings->CurrentControllerProfileTransform = ControllerProfile.SocketOffsetTransform;
			ensure(!VRSettings->CurrentControllerProfileTransform.ContainsNaN());
			VRSettings->bUseSeperateHandTransforms = ControllerProfile.bUseSeperateHandOffsetTransforms;
			VRSettings->CurrentControllerProfileTransformRight = ControllerProfile.SocketOffsetTransformRightHand;
			ensure(!VRSettings->CurrentControllerProfileTransformRight.ContainsNaN());
			VRSettings->OnControllerProfileChangedEvent.Broadcast();
		}
		else
			return false;
	}


	// Not saving key mapping in purpose, app will revert to default on next load and profiles will load custom changes
	return true;
}

void UVRGlobalSettings::PostInitProperties()
{
#if WITH_EDITOR
	// Not doing this currently, loading defaults is cool, and I may go back to it later when i get
	// controller offsets for vive/touch/MR vs each other.
	/*if (ControllerProfiles.Num() == 0)
	{
		ControllerProfiles.Add(FBPVRControllerProfile(TEXT("Vive_Wands")));
		ControllerProfiles.Add(FBPVRControllerProfile(TEXT("Oculus_Touch"), ControllerProfileStatics::OculusTouchStaticOffset));
		this->SaveConfig(CPF_Config, *this->GetDefaultConfigFilename());
	}*/
#endif

	SetScalers();

	Super::PostInitProperties();
}

#if WITH_EDITOR

void UVRGlobalSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged != nullptr)
	{
#if WITH_EDITORONLY_DATA
		if (
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, bUseChaosTranslationScalers) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, bSetEngineChaosScalers) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, LinearDriveStiffnessScale) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, LinearDriveDampingScale) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, AngularDriveStiffnessScale) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, AngularDriveDampingScale) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, JointStiffness) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, SoftLinearStiffnessScale) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, SoftLinearDampingScale) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, SoftAngularStiffnessScale) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, SoftAngularDampingScale) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, JointLinearBreakScale) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UVRGlobalSettings, JointAngularBreakScale)
			)
		{
			SetScalers();
		}
#endif
	}
}
#endif

void UVRGlobalSettings::SetScalers()
{
	auto CVarLinearDriveStiffnessScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.LinearDriveStiffnessScale"));
	auto CVarLinearDriveDampingScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.LinaearDriveDampingScale"));
	auto CVarAngularDriveStiffnessScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.AngularDriveStiffnessScale"));
	auto CVarAngularDriveDampingScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.AngularDriveDampingScale"));

	// Constraint settings
	auto CVarJointStiffness = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.JointStiffness"));
	auto CVarSoftLinearStiffnessScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.SoftLinearStiffnessScale"));
	auto CVarSoftLinearDampingScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.SoftLinearDampingScale"));
	auto CVarSoftAngularStiffnessScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.SoftAngularStiffnessScale"));
	auto CVarSoftAngularDampingScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.SoftAngularDampingScale"));
	auto CVarJointLinearBreakScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.LinearBreakScale"));
	auto CVarJointAngularBreakScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.AngularBreakScale"));

	if (bUseChaosTranslationScalers && bSetEngineChaosScalers)
	{
		CVarLinearDriveStiffnessScale->Set(LinearDriveStiffnessScale, EConsoleVariableFlags::ECVF_SetByCode);
		CVarLinearDriveDampingScale->Set(LinearDriveDampingScale, EConsoleVariableFlags::ECVF_SetByCode);
		CVarAngularDriveStiffnessScale->Set(AngularDriveStiffnessScale, EConsoleVariableFlags::ECVF_SetByCode);
		CVarAngularDriveDampingScale->Set(AngularDriveDampingScale, EConsoleVariableFlags::ECVF_SetByCode);

		// Constraint settings
		CVarJointStiffness->Set(JointStiffness, EConsoleVariableFlags::ECVF_SetByCode);
		CVarSoftLinearStiffnessScale->Set(SoftLinearStiffnessScale, EConsoleVariableFlags::ECVF_SetByCode);
		CVarSoftLinearDampingScale->Set(SoftLinearDampingScale, EConsoleVariableFlags::ECVF_SetByCode);
		CVarSoftAngularStiffnessScale->Set(SoftAngularStiffnessScale, EConsoleVariableFlags::ECVF_SetByCode);
		CVarSoftAngularDampingScale->Set(SoftAngularDampingScale, EConsoleVariableFlags::ECVF_SetByCode);
		CVarJointLinearBreakScale->Set(JointLinearBreakScale, EConsoleVariableFlags::ECVF_SetByCode);
		CVarJointAngularBreakScale->Set(JointAngularBreakScale, EConsoleVariableFlags::ECVF_SetByCode);
	}
	else if (!bSetEngineChaosScalers)
	{
		CVarLinearDriveStiffnessScale->Set(1.0f, EConsoleVariableFlags::ECVF_SetByCode);
		CVarLinearDriveDampingScale->Set(1.0f, EConsoleVariableFlags::ECVF_SetByCode);
		CVarAngularDriveStiffnessScale->Set(1.5f, EConsoleVariableFlags::ECVF_SetByCode);
		CVarAngularDriveDampingScale->Set(1.5f, EConsoleVariableFlags::ECVF_SetByCode);

		// Constraint settings
		CVarJointStiffness->Set(1.0f, EConsoleVariableFlags::ECVF_SetByCode);
		CVarSoftLinearStiffnessScale->Set(1.5f, EConsoleVariableFlags::ECVF_SetByCode);
		CVarSoftLinearDampingScale->Set(1.2f, EConsoleVariableFlags::ECVF_SetByCode);
		CVarSoftAngularStiffnessScale->Set(100000.f, EConsoleVariableFlags::ECVF_SetByCode);
		CVarSoftAngularDampingScale->Set(1000.f, EConsoleVariableFlags::ECVF_SetByCode);
		CVarJointLinearBreakScale->Set(1.0f, EConsoleVariableFlags::ECVF_SetByCode);
		CVarJointAngularBreakScale->Set(1.0f, EConsoleVariableFlags::ECVF_SetByCode);
	}
}

void UVRGlobalSettings::GetMeleeSurfaceGlobalSettings(TArray<FBPHitSurfaceProperties>& OutMeleeSurfaceSettings)
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();
	OutMeleeSurfaceSettings = VRSettings.MeleeSurfaceSettings;
}

void UVRGlobalSettings::GetVirtualStockGlobalSettings(FBPVirtualStockSettings& OutVirtualStockSettings)
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

	OutVirtualStockSettings.bUseDistanceBasedStockSnapping = VRSettings.VirtualStockSettings.bUseDistanceBasedStockSnapping;
	OutVirtualStockSettings.StockSnapDistance = VRSettings.VirtualStockSettings.StockSnapDistance;
	OutVirtualStockSettings.StockSnapOffset = VRSettings.VirtualStockSettings.StockSnapOffset;
	OutVirtualStockSettings.bSmoothStockHand = VRSettings.VirtualStockSettings.bSmoothStockHand;
	OutVirtualStockSettings.SmoothingValueForStock = VRSettings.VirtualStockSettings.SmoothingValueForStock;
}

void UVRGlobalSettings::SaveVirtualStockGlobalSettings(FBPVirtualStockSettings NewVirtualStockSettings)
{
	UVRGlobalSettings& VRSettings = *GetMutableDefault<UVRGlobalSettings>();
	VRSettings.VirtualStockSettings = NewVirtualStockSettings;

	VRSettings.SaveConfig();
}
