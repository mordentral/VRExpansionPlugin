#pragma once

#include "Iris/Serialization/NetSerializer.h"
#include "VRBPDatatypes.h"

#include "FBPSecondaryGripInfoNetSerializer.generated.h"

USTRUCT()
struct FBPSecondaryGripInfoNetSerializerConfig : public FNetSerializerConfig
{
    GENERATED_BODY()
};

namespace UE::Net
{
    UE_NET_DECLARE_SERIALIZER(FBPSecondaryGripInfoNetSerializer, VREXPANSIONPLUGIN_API);
}