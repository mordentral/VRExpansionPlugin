#include "Serializers/FRepMovementVRNetSerializer.h"
#include "Serializers/SerializerHelpers.h"
#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/PackedVectorNetSerializers.h"
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"

#include "VRBaseCharacter.h"
#include "RepMovementNetSerializer.h"


namespace UE::Net
{

    // -----------------------------------------------------------------------------
    // Iris serializer for FRepMovementVRCharacter
    // -----------------------------------------------------------------------------
    struct FRepMovementVRCharacterNetSerializer
    {
        inline static const FVectorNetQuantize100NetSerializerConfig Quantize100SerializerConfig;
        inline static const FRepMovementNetSerializerConfig RepMovementPtrNetSerializerConfig;

        inline static const FNetSerializerConfig* VectorNetQuantizeNetSerializerConfig = &Quantize100SerializerConfig;
        inline static const FNetSerializer* VectorNetQuantizeNetSerializer;

        inline static const FNetSerializerConfig* FRepMovementNetSerializerConfig = &RepMovementPtrNetSerializerConfig;
        inline static const FNetSerializer* FRepMovementNetSerializer;

        class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
        {
        public:
            virtual ~FNetSerializerRegistryDelegates();

            void InitNetSerializer()
            {
                FRepMovementVRCharacterNetSerializer::VectorNetQuantizeNetSerializer = &UE_NET_GET_SERIALIZER(FVectorNetQuantize100NetSerializer);
                FRepMovementVRCharacterNetSerializer::FRepMovementNetSerializer = &UE_NET_GET_SERIALIZER(FRepMovementNetSerializer);
            }

        private:
            virtual void OnPreFreezeNetSerializerRegistry() override;
            //virtual void OnPostFreezeNetSerializerRegistry() override;
        };

        inline static FRepMovementVRCharacterNetSerializer::FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;


        /** Version is required. */
        static constexpr uint32 Version = 0;

        // TODO: Keep up to date with the source struct
        // Copy of the rep movement quant data struct, needs to be kept up to date with engine changes
        struct FRepMovementQuantizedDataCopy
        {
            uint64 AngularVelocity[4];
            uint64 LinearVelocity[4];
            uint64 Location[4];
            uint16 Rotation[4];
            uint64 Acceleration[4];
            int32 ServerFrame;
            int32 ServerPhysicsHandle;

            uint16 Flags : 4;
            uint16 VelocityQuantizationLevel : 2;
            uint16 LocationQuantizationLevel : 2;
            uint16 RotationQuantizationLevel : 1;
            uint16 RepAcceleration : 1;
            uint16 Unused : 6;
            uint16 Padding[3];
        };

        struct alignas(8) FRepMovementVRCharacterQuantizedData
        {
            uint8 bJustTeleported;
            uint8 bJustTeleportedGrips;
            uint8 bPausedTracking;
            uint64 PausedTrackingLoc[4];
            uint16 PausedTrackingRot;

            // Not Replicated, is stored as a reference
            //FObjectNetSerializerQuantizedReferenceStorage Owner;

            FRepMovementQuantizedDataCopy RepMovementData;
        };

        typedef FRepMovementVRCharacter SourceType;
        typedef FRepMovementVRCharacterQuantizedData QuantizedType;
        typedef FRepMovementVRCharacterNetSerializerConfig ConfigType;
        inline static const ConfigType DefaultConfig;

        /** Set to false when a same value delta compression method is undesirable, for example when the serializer only writes a single bit for the state. */
        static constexpr bool bUseDefaultDelta = true;

        // Should likely handle delta custom eventually

          // Called to create a "quantized snapshot" of the struct
        static void Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
        {
            // Actually do the real quantization step here next instead of just in serialize, will save on memory overall
            const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
            QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

            Target.bJustTeleported = Source.bJustTeleported ? 1 : 0;
            Target.bJustTeleportedGrips = Source.bJustTeleportedGrips ? 1 : 0;

            Target.bPausedTracking = Source.bPausedTracking ? 1 : 0;

            if (Source.bPausedTracking)
            {
                const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                FNetQuantizeArgs MemberArgs = Args;
                MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                MemberArgs.Source = NetSerializerValuePointer(&Source.PausedTrackingLoc);
                MemberArgs.Target = NetSerializerValuePointer(&Target.PausedTrackingLoc[0]);
                Serializer->Quantize(Context, MemberArgs);

                Target.PausedTrackingRot = FRotator::CompressAxisToShort(Source.PausedTrackingRot);
            }

            // FRepMovementQuantizedDataCopy RepMovementData;
            const FNetSerializer* Serializer = FRepMovementNetSerializer;
            const FNetSerializerConfig* SerializerConfig = FRepMovementNetSerializerConfig;

            FNetQuantizeArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Source = NetSerializerValuePointer(&Source);
            MemberArgs.Target = NetSerializerValuePointer(&Target.RepMovementData);
            Serializer->Quantize(Context, MemberArgs);
        }

        // Called to apply the quantized snapshot back to gameplay memory
        static void Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
        {
            const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
            SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

            Target.bJustTeleported = Source.bJustTeleported != 0;
            Target.bJustTeleportedGrips = Source.bJustTeleportedGrips != 0;

            Target.bPausedTracking = Source.bPausedTracking != 0;

            if (Target.bPausedTracking)
            {
                const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                FNetDequantizeArgs MemberArgs = Args;
                MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                MemberArgs.Source = NetSerializerValuePointer(&Source.PausedTrackingLoc[0]);
                MemberArgs.Target = NetSerializerValuePointer(&Target.PausedTrackingLoc);
                Serializer->Dequantize(Context, MemberArgs);

                Target.PausedTrackingRot = FRotator::DecompressAxisFromShort(Source.PausedTrackingRot);
            }

            // FRepMovementQuantizedDataCopy RepMovementData;
            const FNetSerializer* Serializer = FRepMovementNetSerializer;
            const FNetSerializerConfig* SerializerConfig = FRepMovementNetSerializerConfig;

            FNetDequantizeArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Source = NetSerializerValuePointer(&Source.RepMovementData);
            MemberArgs.Target = NetSerializerValuePointer(&Target);
            Serializer->Dequantize(Context, MemberArgs);
        }

        // Serialize into bitstream
        static void Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
        {
            const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
            FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

            Writer->WriteBits(static_cast<uint32>(Source.bJustTeleported), 1);
            Writer->WriteBits(static_cast<uint32>(Source.bJustTeleportedGrips), 1);

            Writer->WriteBits(static_cast<uint32>(Source.bPausedTracking), 1);

            if (Source.bPausedTracking != 0)
            {
                const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                FNetSerializeArgs MemberArgs = Args;
                MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                MemberArgs.Source = NetSerializerValuePointer(&Source.PausedTrackingLoc[0]);
                Serializer->Serialize(Context, MemberArgs);
                                
                Writer->WriteBits(static_cast<uint32>(Source.PausedTrackingRot), 16);
            }

            // FRepMovementQuantizedDataCopy RepMovementData;
            const FNetSerializer* Serializer = FRepMovementNetSerializer;
            const FNetSerializerConfig* SerializerConfig = FRepMovementNetSerializerConfig;

            FNetSerializeArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Source = NetSerializerValuePointer(&Source.RepMovementData);
            Serializer->Serialize(Context, MemberArgs);
        }

        // Deserialize from bitstream
        static void Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
        {
            QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
            FNetBitStreamReader* Reader = Context.GetBitStreamReader();

            Target.bJustTeleported = Reader->ReadBits(1);
            Target.bJustTeleportedGrips = Reader->ReadBits(1);

            Target.bPausedTracking = Reader->ReadBits(1);

            if (Target.bPausedTracking != 0)
            {
                const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                FNetDeserializeArgs MemberArgs = Args;
                MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                MemberArgs.Target = NetSerializerValuePointer(&Target.PausedTrackingLoc[0]);
                Serializer->Deserialize(Context, MemberArgs);

                Target.PausedTrackingRot = Reader->ReadBits(16);
            }

            // FRepMovementQuantizedDataCopy RepMovementData;
            const FNetSerializer* Serializer = FRepMovementNetSerializer;
            const FNetSerializerConfig* SerializerConfig = FRepMovementNetSerializerConfig;

            FNetDeserializeArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Target = NetSerializerValuePointer(&Target.RepMovementData);
            Serializer->Deserialize(Context, MemberArgs);
        }

        static void SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
        {
            const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
            FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

            Writer->WriteBits(static_cast<uint32>(Source.bJustTeleported), 1);
            Writer->WriteBits(static_cast<uint32>(Source.bJustTeleportedGrips), 1);

            Writer->WriteBits(static_cast<uint32>(Source.bPausedTracking), 1);

            if (Source.bPausedTracking != 0)
            {
                const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                FNetSerializeArgs MemberArgs = Args;
                MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                MemberArgs.Source = NetSerializerValuePointer(&Source.PausedTrackingLoc[0]);
                Serializer->Serialize(Context, MemberArgs);

                Writer->WriteBits(static_cast<uint32>(Source.PausedTrackingRot), 16);
            }

            // FRepMovementQuantizedDataCopy RepMovementData;
            const FNetSerializer* Serializer = FRepMovementNetSerializer;
            const FNetSerializerConfig* SerializerConfig = FRepMovementNetSerializerConfig;

            FNetSerializeDeltaArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Source = NetSerializerValuePointer(&Source.RepMovementData);
            Serializer->SerializeDelta(Context, MemberArgs);

        }

        static void DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
        {
            QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
            FNetBitStreamReader* Reader = Context.GetBitStreamReader();

            Target.bJustTeleported = Reader->ReadBits(1);
            Target.bJustTeleportedGrips = Reader->ReadBits(1);

            Target.bPausedTracking = Reader->ReadBits(1);

            if (Target.bPausedTracking != 0)
            {
                const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                FNetDeserializeArgs MemberArgs = Args;
                MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                MemberArgs.Target = NetSerializerValuePointer(&Target.PausedTrackingLoc[0]);
                Serializer->Deserialize(Context, MemberArgs);

                Target.PausedTrackingRot = Reader->ReadBits(16);
            }

            // FRepMovementQuantizedDataCopy RepMovementData;
            const FNetSerializer* Serializer = FRepMovementNetSerializer;
            const FNetSerializerConfig* SerializerConfig = FRepMovementNetSerializerConfig;

            FNetDeserializeDeltaArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Target = NetSerializerValuePointer(&Target.RepMovementData);
            Serializer->DeserializeDelta(Context, MemberArgs);
        }

        // Compare two instances to see if they differ
        static bool IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
        {
            if (Args.bStateIsQuantized)
            {
                const QuantizedType& QuantizedValue0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
                const QuantizedType& QuantizedValue1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);
                return FPlatformMemory::Memcmp(&QuantizedValue0, &QuantizedValue1, sizeof(QuantizedType)) == 0;
            }
            else
            {
                const SourceType& L = *reinterpret_cast<const SourceType*>(Args.Source0);
                const SourceType& R = *reinterpret_cast<const SourceType*>(Args.Source1);

                if (L.bJustTeleported != R.bJustTeleported) return false;
                if (L.bJustTeleportedGrips != R.bJustTeleportedGrips) return false;
                if (L.bPausedTracking != R.bPausedTracking) return false;

                if (L.bPausedTracking)
                {
                    if (!L.PausedTrackingLoc.Equals(R.PausedTrackingLoc)) return false;
                    if (!FMath::IsNearlyEqual(L.PausedTrackingRot, R.PausedTrackingRot)) return false;
                }

                const FNetSerializer* Serializer = FRepMovementNetSerializer;
                const FNetSerializerConfig* SerializerConfig = FRepMovementNetSerializerConfig;

                FNetIsEqualArgs MemberArgs = Args;
                MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                MemberArgs.Source0 = NetSerializerValuePointer(&L);
                MemberArgs.Source1 = NetSerializerValuePointer(&R);
                return Serializer->IsEqual(Context, MemberArgs);
            }
        }

        static bool Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
        {
            const SourceType& Source = *reinterpret_cast<SourceType*>(Args.Source);

            const FNetSerializer* Serializer = FRepMovementNetSerializer;
            const FNetSerializerConfig* SerializerConfig = FRepMovementNetSerializerConfig;

            FNetValidateArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Source = NetSerializerValuePointer(&Source);
            return Serializer->Validate(Context, MemberArgs);
        }

        static void Apply(FNetSerializationContext& Context, const FNetApplyArgs& Args)
        {
            const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
            SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);


            Target.bJustTeleported = Source.bJustTeleported;
            Target.bJustTeleportedGrips = Source.bJustTeleportedGrips;
            
            Target.bPausedTracking = Source.bPausedTracking;

            if (Target.bPausedTracking)
            {
                Target.PausedTrackingLoc = Source.PausedTrackingLoc;
                Target.PausedTrackingRot = Source.PausedTrackingRot;
            }
            else
            {
                Target.PausedTrackingLoc = FVector::ZeroVector;
                Target.PausedTrackingRot = 0.0f;
            }
            
            const FNetSerializer* Serializer = FRepMovementNetSerializer;
            const FNetSerializerConfig* SerializerConfig = FRepMovementNetSerializerConfig;

            FNetApplyArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Source = NetSerializerValuePointer(&Source);
            MemberArgs.Target = NetSerializerValuePointer(&Target);
            Serializer->Apply(Context, MemberArgs);
        }
    };


	static const FName PropertyNetSerializerRegistry_NAME_RepMovementVRCharacter("RepMovementVRCharacter");
	UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_RepMovementVRCharacter, FRepMovementVRCharacterNetSerializer);

	FRepMovementVRCharacterNetSerializer::FNetSerializerRegistryDelegates::~FNetSerializerRegistryDelegates()
	{
		UE_NET_UNREGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_RepMovementVRCharacter);
	}

	void FRepMovementVRCharacterNetSerializer::FNetSerializerRegistryDelegates::OnPreFreezeNetSerializerRegistry()
	{
        InitNetSerializer();
		UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_RepMovementVRCharacter);
	}

    UE_NET_IMPLEMENT_SERIALIZER(FRepMovementVRCharacterNetSerializer);
}
