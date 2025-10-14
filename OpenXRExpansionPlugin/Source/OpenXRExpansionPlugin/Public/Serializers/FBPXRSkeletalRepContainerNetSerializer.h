#pragma once

#include "Iris/Serialization/NetSerializer.h"
#include "FBPXRSkeletalRepContainerNetSerializer.generated.h"

USTRUCT()
struct FBPXRSkeletalRepContainerNetSerializerConfig : public FNetSerializerConfig
{
    GENERATED_BODY()
};

namespace UE::Net
{
    UE_NET_DECLARE_SERIALIZER(FBPXRSkeletalRepContainerNetSerializer, OPENXREXPANSIONPLUGIN_API);

}