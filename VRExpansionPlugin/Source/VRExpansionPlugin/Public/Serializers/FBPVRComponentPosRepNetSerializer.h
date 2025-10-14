#pragma once

#include "Iris/Serialization/NetSerializer.h"
#include "VRBPDatatypes.h"

#include "FBPVRComponentPosRepNetSerializer.generated.h"

USTRUCT()
struct FBPVRComponentPosRepNetSerializerConfig : public FNetSerializerConfig
{
    GENERATED_BODY()
};

namespace UE::Net
{
    UE_NET_DECLARE_SERIALIZER(FBPVRComponentPosRepNetSerializer, VREXPANSIONPLUGIN_API);
}