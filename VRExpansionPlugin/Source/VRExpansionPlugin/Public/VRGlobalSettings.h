#pragma once

#include "GameFramework/PlayerInput.h"
#include "GameFramework/InputSettings.h"
#include "VRGlobalSettings.generated.h"


/*namespace ControllerProfileStatics
{
	static const FTransform OculusTouchStaticOffset(FRotator(-70.f, 0.f, 0.f));
}*/


USTRUCT(BlueprintType, Category = "VRGlobalSettings")
struct FAxisMappingDetails
{
	GENERATED_BODY()
public:

	/** List of all axis key mappings that correspond to the axis name in the containing map */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TArray<FInputAxisKeyMapping> AxisMappings;

};

USTRUCT(BlueprintType, Category = "VRGlobalSettings")
struct FActionMappingDetails
{
	GENERATED_BODY()
public:

	/** List of all axis key mappings that correspond to the axis name in the containing map */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
		TArray<FInputActionKeyMapping> ActionMappings;
};

USTRUCT(BlueprintType, Category = "ControllerProfiles")
struct VREXPANSIONPLUGIN_API FBPVRControllerProfile
{
	GENERATED_BODY()
public:

	// Name of controller profile
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControllerProfiles")
		FName ControllerName;

	// Offset to use with this controller
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControllerProfiles")
		FTransform SocketOffsetTransform;

	// Setting an axis value here with key bindings will override the equivalent bindings on profile load
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControllerProfiles")
	TMap<FName, FAxisMappingDetails> AxisOverrides;

	// Setting action mappings here will override the equivalent bindings on profile load
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControllerProfiles")
	TMap<FName, FActionMappingDetails> ActionOverrides;


	FBPVRControllerProfile() :
		ControllerName(NAME_None),
		SocketOffsetTransform(FTransform::Identity)
	{}

	FBPVRControllerProfile(FName ControllerName) :
		ControllerName(ControllerName),
		SocketOffsetTransform(FTransform::Identity)
	{}

	FBPVRControllerProfile(FName ControllerName, const FTransform & Offset) :
		ControllerName(ControllerName),
		SocketOffsetTransform(Offset)
	{}

};

UCLASS(config = Engine, defaultconfig)
class VREXPANSIONPLUGIN_API UVRGlobalSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	// Controller profiles to store related information on a per profile basis
	UPROPERTY(config, EditAnywhere, Category = "ControllerProfiles")
	TArray<FBPVRControllerProfile> ControllerProfiles;

	// Store these to save some processing when getting the transform after a profile is loaded
	FName CurrentControllerProfileInUse;
	FTransform CurrentControllerProfileTransform;

	// Setting to use for the OneEuro smoothing low pass filter when double gripping something held with a hand
	//UPROPERTY(config, EditAnywhere, Category = "Secondary Grip 1Euro Settings")
	//float OneEuroMinCutoff;

	// Setting to use for the OneEuro smoothing low pass filter when double gripping something held with a hand
	//UPROPERTY(config, EditAnywhere, Category = "Secondary Grip 1Euro Settings")
	//float OneEuroCutoffSlope;

	// Setting to use for the OneEuro smoothing low pass filter when double gripping something held with a hand
	//UPROPERTY(config, EditAnywhere, Category = "Secondary Grip 1Euro Settings")
	//float OneEuroDeltaCutoff;

	// Adjust the transform of a socket for a particular controller model, if a name is not sent in, it will use the currently loaded one
	// If there is no currently loaded one, it will return the input transform as is.
	UFUNCTION(BlueprintPure, Category = "VRControllerProfiles")
	static FTransform AdjustTransformByControllerProfile(FName OptionalControllerProfileName, const FTransform & SocketTransform)
	{
		const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

		if (OptionalControllerProfileName == NAME_None)
		{
			if (VRSettings.CurrentControllerProfileInUse != NAME_None)
			{
				// Use currently loaded transform
				return SocketTransform * VRSettings.CurrentControllerProfileTransform;
			}

			// No override and no default, return base transform back
			return SocketTransform;
		}

		// Had an override, find it if possible and use its transform
		const FBPVRControllerProfile * FoundProfile = VRSettings.ControllerProfiles.FindByPredicate([OptionalControllerProfileName](const FBPVRControllerProfile & ArrayItem)
		{
			return ArrayItem.ControllerName == OptionalControllerProfileName;
		});

		if (FoundProfile)
		{
			return SocketTransform * FoundProfile->SocketOffsetTransform;
		}

		// Couldn't find it, return base transform
		return SocketTransform;
	}

	// Get array of controller profiles
	UFUNCTION(BlueprintPure, Category = "VRControllerProfiles")
		static TArray<FBPVRControllerProfile> GetControllerProfiles()
	{
		const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();
		
		return VRSettings.ControllerProfiles;
	}

	// Get name of currently loaded profile (if one is loaded)
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles")
		static FName GetCurrentProfile()
	{
		const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

		return VRSettings.CurrentControllerProfileInUse;
	}

	// Get a controller profile by name
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles")
		static bool GetControllerProfile(FName ControllerProfileName, FBPVRControllerProfile & OutProfile)
	{
		const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

		const FBPVRControllerProfile * FoundProfile = VRSettings.ControllerProfiles.FindByPredicate([ControllerProfileName](const FBPVRControllerProfile & ArrayItem)
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

	// Load a controller profile by name
	// Action / Axis mappings overwrite ones with the same action/axis name in the input settings.
	// If you have an action/axis override but don't assign buttons to it then it will just delete it.
	// This can be useful for removing actions and using multiple actions (IE: Grip Touch, Grip Vive actions)
	// For when just changing the buttons isn't good enough
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles")
	static bool LoadControllerProfileByName(FName ControllerProfileName)
	{
		const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

		const FBPVRControllerProfile * FoundProfile = VRSettings.ControllerProfiles.FindByPredicate([ControllerProfileName](const FBPVRControllerProfile & ArrayItem)
		{
			return ArrayItem.ControllerName == ControllerProfileName;
		});

		if (FoundProfile)
		{
			return LoadControllerProfile(*FoundProfile);
		}

		return false;
	}

	// Load a controller profile
	// Action / Axis mappings overwrite ones with the same action/axis name in the input settings.
	// If you have an action/axis override but don't assign buttons to it then it will just delete it.
	// This can be useful for removing actions and using multiple actions (IE: Grip Touch, Grip Vive actions)
	// For when just changing the buttons isn't good enough
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles")
		static bool LoadControllerProfile(const FBPVRControllerProfile & ControllerProfile)
	{
		if (ControllerProfile.ActionOverrides.Num() == 0 && ControllerProfile.AxisOverrides.Num() == 0)
			return false;

		UInputSettings* InputSettings = const_cast<UInputSettings*>(GetDefault<UInputSettings>());
		if (!InputSettings)
			return false;

		// Load button mappings
		for (auto& Elem : ControllerProfile.ActionOverrides)
		{
			FName ActionName = Elem.Key;
			FActionMappingDetails Mapping = Elem.Value;

			// We allow for 0 mapped actions here in case you want to delete one
			if (ActionName == NAME_None /*|| Mapping.ActionMappings.Num() < 1*/)
				continue;

			// Clear all actions that use our action name first
			for (int32 ActionIndex = InputSettings->ActionMappings.Num() - 1; ActionIndex >= 0; --ActionIndex)
			{
				if (InputSettings->ActionMappings[ActionIndex].ActionName == ActionName)
				{
					InputSettings->ActionMappings.RemoveAt(ActionIndex);
					// we don't break because the mapping may have been in the array twice
				}
			}

			// Then add the new bindings
			for (FInputActionKeyMapping &KeyMapping : Mapping.ActionMappings)
			{			
				// By default the key mappings don't have an action name, add them here
				KeyMapping.ActionName = ActionName;
				InputSettings->ActionMappings.Add(KeyMapping);
			}
		}

		// Load axis mappings
		for (auto& Elem : ControllerProfile.AxisOverrides)
		{
			FName AxisName = Elem.Key;
			FAxisMappingDetails Mapping = Elem.Value;

			// We allow for 0 mapped Axis's here in case you want to delete one
			if (AxisName == NAME_None /*|| Mapping.AxisMappings.Num() < 1*/)
				continue;

			// Clear all Axis's that use our Axis name first
			for (int32 AxisIndex = InputSettings->AxisMappings.Num() - 1; AxisIndex >= 0; --AxisIndex)
			{
				if (InputSettings->AxisMappings[AxisIndex].AxisName == AxisName)
				{
					InputSettings->AxisMappings.RemoveAt(AxisIndex);
					// we don't break because the mapping may have been in the array twice
				}
			}

			// Then add the new bindings
			for (FInputAxisKeyMapping &KeyMapping : Mapping.AxisMappings)
			{
				// By default the key mappings don't have an Axis name, add them here
				KeyMapping.AxisName = AxisName;
				InputSettings->AxisMappings.Add(KeyMapping);
			}
		}

		// Tell all players to use the new keymappings
		InputSettings->ForceRebuildKeymaps();

		UVRGlobalSettings* VRSettings = const_cast<UVRGlobalSettings*>(GetDefault<UVRGlobalSettings>());
		if (VRSettings)
		{
			VRSettings->CurrentControllerProfileInUse = ControllerProfile.ControllerName;
			VRSettings->CurrentControllerProfileTransform = ControllerProfile.SocketOffsetTransform;
		}

		// Not saving key mapping in purpose, app will revert to default on next load and profiles will load custom changes
		return true;
	}

	virtual void PostInitProperties() override
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
};
