
#include "VRGlobalSettings.h"

#if WITH_CHAOS
#include "Chaos/ChaosConstraintSettings.h"
#endif

UVRGlobalSettings::UVRGlobalSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	bUseGlobalLerpToHand(false),
	MinDistanceForLerp(10.0f),
	LerpDuration(0.25f),
	MinSpeedForLerp(100.f),
	MaxSpeedForLerp(500.f),
	LerpInterpolationMode(EVRLerpInterpolationMode::QuatInterp),
	bUseCurve(false),
	MaxCCDPasses(1),
	OneEuroMinCutoff(0.1f),
	OneEuroCutoffSlope(10.0f),
	OneEuroDeltaCutoff(10.0f),
	CurrentControllerProfileInUse(NAME_None),
	CurrentControllerProfileTransform(FTransform::Identity),
	bUseSeperateHandTransforms(false),
	CurrentControllerProfileTransformRight(FTransform::Identity)
{
#if WITH_CHAOS
		LinearDriveStiffnessScale = Chaos::ConstraintSettings::LinearDriveStiffnessScale();
		LinearDriveDampingScale = Chaos::ConstraintSettings::LinearDriveDampingScale();
		AngularDriveStiffnessScale = Chaos::ConstraintSettings::AngularDriveStiffnessScale();
		AngularDriveDampingScale = Chaos::ConstraintSettings::AngularDriveDampingScale();
#endif
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

	// Couldn't find it, return base transform
	return SocketTransform;
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
	Super::PostInitProperties();
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