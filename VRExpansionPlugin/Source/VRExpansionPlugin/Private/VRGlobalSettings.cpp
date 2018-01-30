
#include "VRGlobalSettings.h"

UVRGlobalSettings::UVRGlobalSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	CurrentControllerProfileInUse(NAME_None),
	CurrentControllerProfileTransform(FTransform::Identity)
	//OneEuroMinCutoff(2.0f),
	//OneEuroCutoffSlope(0.007f),
	//OneEuroDeltaCutoff(1.0f)
{
}