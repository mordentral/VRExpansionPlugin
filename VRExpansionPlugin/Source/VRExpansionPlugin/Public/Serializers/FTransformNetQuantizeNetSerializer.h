#pragma once

#include "Iris/Serialization/NetSerializer.h"
#include "VRBPDatatypes.h"

#include "FTransformNetQuantizeNetSerializer.generated.h"

USTRUCT()
struct FTransformNetQuantizeNetSerializerConfig : public FNetSerializerConfig
{
    GENERATED_BODY()
};

namespace UE::Net
{
    UE_NET_DECLARE_SERIALIZER(FTransformNetQuantizeNetSerializer, VREXPANSIONPLUGIN_API);


    struct alignas(8) FTransformNetQuantizeQuantizedData
    {
        uint64 Position[4]; // We don't need to store double for tracked device positions, but their forwarded serializer uses it
        uint16 Rotation[3];
        uint64 Scale[4]; // We don't need to store double for tracked device positions, but their forwarded serializer uses it
        uint8 UseHighPrecision : 1;
    };
}