#include "Serializers/FTransformNetQuantizeNetSerializer.h"
#include "Serializers/SerializerHelpers.h"
#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/PackedVectorNetSerializers.h"
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"

#include "VRBPDatatypes.h"


namespace UE::Net
{

    // -----------------------------------------------------------------------------
    // Iris serializer for FTransformNetQuantize
    // -----------------------------------------------------------------------------
    struct FTransformNetQuantizeNetSerializer
    {
        inline static const FVectorNetQuantize100NetSerializerConfig Quantize100SerializerConfig;

        inline static const FNetSerializerConfig* VectorNetQuantizeNetSerializerConfig = &Quantize100SerializerConfig;
        inline static const FNetSerializer* VectorNetQuantizeNetSerializer;


        class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
        {
        public:
            virtual ~FNetSerializerRegistryDelegates();

            void InitNetSerializer()
            {
                FTransformNetQuantizeNetSerializer::VectorNetQuantizeNetSerializer = &UE_NET_GET_SERIALIZER(FVectorNetQuantize100NetSerializer);
            }

        private:
            virtual void OnPreFreezeNetSerializerRegistry() override;
            //virtual void OnPostFreezeNetSerializerRegistry() override;
        };

        inline static FTransformNetQuantizeNetSerializer::FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;


        /** Version is required. */
        static constexpr uint32 Version = 0;

        /*struct alignas(8) FQuantizedData
        {
            uint64 Position[4]; // We don't need to store double for tracked device positions, but their forwarded serializer uses it
            uint16 Rotation[3];
            uint64 Scale[4]; // We don't need to store double for tracked device positions, but their forwarded serializer uses it
            uint8 UseHighPrecision : 1;
        };*/

        typedef FTransform_NetQuantize SourceType;
        typedef FTransformNetQuantizeQuantizedData QuantizedType;
        typedef FTransformNetQuantizeNetSerializerConfig ConfigType;
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

            static const auto CVarRepHighPrecisionTransforms = IConsoleManager::Get().FindConsoleVariable(TEXT("vrexp.RepHighPrecisionTransforms"));
            bool bUseHighPrecision = CVarRepHighPrecisionTransforms->GetBool();
            Target.UseHighPrecision = bUseHighPrecision;

            FVector rTranslation = Source.GetTranslation();
            FVector rScale3D = Source.GetScale3D();
            FRotator rRotation = Source.Rotator();

            if (bUseHighPrecision)
            {
                Target.Position[0] = rTranslation.X;
                Target.Position[1] = rTranslation.Y;
                Target.Position[2] = rTranslation.Z;

                Target.Scale[0] = rScale3D.X;
                Target.Scale[1] = rScale3D.Y;
                Target.Scale[2] = rScale3D.Z;

                Target.Rotation[0] = FRotator::CompressAxisToShort(rRotation.Pitch);
                Target.Rotation[1] = FRotator::CompressAxisToShort(rRotation.Yaw);
                Target.Rotation[2] = FRotator::CompressAxisToShort(rRotation.Roll);
            }
            else
            {
                const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                FNetQuantizeArgs MemberArgs = Args;
                MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                MemberArgs.Source = NetSerializerValuePointer(&rTranslation);
                MemberArgs.Target = NetSerializerValuePointer(&Target.Position[0]);
                Serializer->Quantize(Context, MemberArgs);

                MemberArgs.Source = NetSerializerValuePointer(&rScale3D);
                MemberArgs.Target = NetSerializerValuePointer(&Target.Scale[0]);
                Serializer->Quantize(Context, MemberArgs);

                Target.Rotation[0] = FRotator::CompressAxisToShort(rRotation.Pitch);
                Target.Rotation[1] = FRotator::CompressAxisToShort(rRotation.Yaw);
                Target.Rotation[2] = FRotator::CompressAxisToShort(rRotation.Roll);
            }
        }

        // Called to apply the quantized snapshot back to gameplay memory
        static void Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
        {
            const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
            SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);
            
            bool bUseHighPrecision = Source.UseHighPrecision != 0;

            FVector rTranslation;
            FVector rScale3D;
            FRotator rRotation;

            if (bUseHighPrecision)
            {
                rTranslation.X  = Source.Position[0];
                rTranslation.Y = Source.Position[1];
                rTranslation.Z = Source.Position[2];

                rScale3D.X = Source.Scale[0];
                rScale3D.Y = Source.Scale[1];
                rScale3D.Z = Source.Scale[2];

                rRotation.Pitch = FRotator::DecompressAxisFromShort(Source.Rotation[0]);
                rRotation.Yaw = FRotator::DecompressAxisFromShort(Source.Rotation[1]);
                rRotation.Roll = FRotator::DecompressAxisFromShort(Source.Rotation[2]);
            }
            else
            {
                const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                FNetDequantizeArgs MemberArgs = Args;
                MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                MemberArgs.Source = NetSerializerValuePointer(&Source.Position[0]);
                MemberArgs.Target = NetSerializerValuePointer(&rTranslation);
                Serializer->Dequantize(Context, MemberArgs);

                MemberArgs.Source = NetSerializerValuePointer(&Source.Scale[0]);
                MemberArgs.Target = NetSerializerValuePointer(&rScale3D);
                Serializer->Dequantize(Context, MemberArgs);

                rRotation.Pitch = FRotator::DecompressAxisFromShort(Source.Rotation[0]);
                rRotation.Yaw = FRotator::DecompressAxisFromShort(Source.Rotation[1]);
                rRotation.Roll = FRotator::DecompressAxisFromShort(Source.Rotation[2]);
            }

            Target.SetComponents(rRotation.Quaternion(), rTranslation, rScale3D);
            Target.NormalizeRotation();
        }

        // Serialize into bitstream
        static void Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
        {
            const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
            FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

            Writer->WriteBits(static_cast<uint32>(Source.UseHighPrecision), 1);

            const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
            const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

            FNetSerializeArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Source = NetSerializerValuePointer(&Source.Position[0]);
            Serializer->Serialize(Context, MemberArgs);

            MemberArgs.Source = NetSerializerValuePointer(&Source.Scale[0]);
            Serializer->Serialize(Context, MemberArgs);

            Writer->WriteBits(static_cast<uint32>(Source.Rotation[0]), 16);
            Writer->WriteBits(static_cast<uint32>(Source.Rotation[1]), 16);
            Writer->WriteBits(static_cast<uint32>(Source.Rotation[2]), 16); 
        }

        // Deserialize from bitstream
        static void Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
        {
            QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
            FNetBitStreamReader* Reader = Context.GetBitStreamReader();

            Target.UseHighPrecision = Reader->ReadBits(1) != 0;

            const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
            const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

            FNetDeserializeArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Target = NetSerializerValuePointer(&Target.Position[0]);
            Serializer->Deserialize(Context, MemberArgs);

            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Target = NetSerializerValuePointer(&Target.Scale[0]);
            Serializer->Deserialize(Context, MemberArgs);

            Target.Rotation[0] = Reader->ReadBits(16);
            Target.Rotation[1] = Reader->ReadBits(16);
            Target.Rotation[2] = Reader->ReadBits(16);
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

                if (!L.Equals(R)) return false;
                return true;
            }
        }
    };


	static const FName PropertyNetSerializerRegistry_NAME_TransformNetQuantize("Transform_NetQuantize");
	UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_TransformNetQuantize, FTransformNetQuantizeNetSerializer);

	FTransformNetQuantizeNetSerializer::FNetSerializerRegistryDelegates::~FNetSerializerRegistryDelegates()
	{
		UE_NET_UNREGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_TransformNetQuantize);
	}

	void FTransformNetQuantizeNetSerializer::FNetSerializerRegistryDelegates::OnPreFreezeNetSerializerRegistry()
	{
        InitNetSerializer();
		UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_TransformNetQuantize);
	}

    UE_NET_IMPLEMENT_SERIALIZER(FTransformNetQuantizeNetSerializer);
}
