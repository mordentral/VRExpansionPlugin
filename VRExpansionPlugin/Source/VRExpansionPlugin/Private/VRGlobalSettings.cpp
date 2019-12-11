
#include "VRGlobalSettings.h"

UVRGlobalSettings::UVRGlobalSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	MaxCCDPasses(1),
	OneEuroMinCutoff(2.0f),
	OneEuroCutoffSlope(0.007f),
	OneEuroDeltaCutoff(1.0f),
	CurrentControllerProfileInUse(NAME_None),
	CurrentControllerProfileTransform(FTransform::Identity),
	bUseSeperateHandTransforms(false),
	CurrentControllerProfileTransformRight(FTransform::Identity)
{
}