#pragma once

#include "VRGlobalSettings.generated.h"

UCLASS(config = Engine, defaultconfig)
class VREXPANSIONPLUGIN_API UVRGlobalSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	// Setting to use for the OneEuro smoothing low pass filter when double gripping something held with a hand
	UPROPERTY(config, EditAnywhere, Category = "Secondary Grip 1Euro Settings")
	float OneEuroMinCutoff;

	// Setting to use for the OneEuro smoothing low pass filter when double gripping something held with a hand
	UPROPERTY(config, EditAnywhere, Category = "Secondary Grip 1Euro Settings")
	float OneEuroCutoffSlope;

	// Setting to use for the OneEuro smoothing low pass filter when double gripping something held with a hand
	UPROPERTY(config, EditAnywhere, Category = "Secondary Grip 1Euro Settings")
	float OneEuroDeltaCutoff;
};
