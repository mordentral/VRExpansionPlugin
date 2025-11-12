#pragma once

#include "Iris/Serialization/NetSerializer.h"

#include "FRepMovementVRNetSerializer.generated.h"

USTRUCT()
struct FRepMovementVRCharacterNetSerializerConfig : public FNetSerializerConfig
{
    GENERATED_BODY()
};

namespace UE::Net
{
    UE_NET_DECLARE_SERIALIZER(FRepMovementVRCharacterNetSerializer, VREXPANSIONPLUGIN_API);

}