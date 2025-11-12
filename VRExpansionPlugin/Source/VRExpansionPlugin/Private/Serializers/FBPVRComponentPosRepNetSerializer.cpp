#include "Serializers/FBPVRComponentPosRepNetSerializer.h"
#include "Serializers/SerializerHelpers.h"
#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/PackedVectorNetSerializers.h"
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"


namespace UE::Net
{

    // -----------------------------------------------------------------------------
    // Iris serializer for FBPVRComponentPosRep
    // -----------------------------------------------------------------------------
    struct FBPVRComponentPosRepNetSerializer
    {

        inline static const FVectorNetQuantize10NetSerializerConfig Quantize10SerializerConfig;
        inline static const FVectorNetQuantize100NetSerializerConfig Quantize100SerializerConfig;

        inline static const FNetSerializerConfig* VectorNetQuantizeNetSerializerConfigs[2] = { &Quantize10SerializerConfig, &Quantize100SerializerConfig};
        inline static const FNetSerializer* VectorNetQuantizeNetSerializers[2] = {};


        class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
        {
        public:
            virtual ~FNetSerializerRegistryDelegates();

            void InitNetSerializer()
            {
                FBPVRComponentPosRepNetSerializer::VectorNetQuantizeNetSerializers[0] = &UE_NET_GET_SERIALIZER(FVectorNetQuantize10NetSerializer);
                FBPVRComponentPosRepNetSerializer::VectorNetQuantizeNetSerializers[1] = &UE_NET_GET_SERIALIZER(FVectorNetQuantize100NetSerializer);
            }

        private:
            virtual void OnPreFreezeNetSerializerRegistry() override;
            //virtual void OnPostFreezeNetSerializerRegistry() override;
        };

        inline static FBPVRComponentPosRepNetSerializer::FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;


        /** Version is required. */
        static constexpr uint32 Version = 0;

        struct alignas(8) FQuantizedData
        {
            uint64 Position[4]; // We don't need to store double for tracked device positions, but their forwarded serializer uses it
            uint16 Rotation[3];

            uint8 QuantizationLevel : 1;
            uint8 RotationQuantizationLevel : 1;
        };

        typedef FBPVRComponentPosRep SourceType;
        typedef FQuantizedData QuantizedType;
        typedef FBPVRComponentPosRepNetSerializerConfig ConfigType;
        inline static const ConfigType DefaultConfig;

        /** Set to false when a same value delta compression method is undesirable, for example when the serializer only writes a single bit for the state. */
        static constexpr bool bUseDefaultDelta = true;
        // Not doing delta, the majority of the time a single bit (bool) controls the serialization of the entirity
        
        /**
         * Optional. Same as Serialize but where an acked previous state is provided for bitpacking purposes.
         * This is implemented by default to do same value optimization, at the cost of a bit. If implemented
         * then DeserializeDelta is required.
         */
         /*static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs&)
         {

         }*/

         /**
          * Optional. Same as Deserialize but where an acked previous state is provided for bitpacking purposes.
          * This is implemented by default to do same value optimization, at the cost of a bit. If implemented
          * then SerializeDelta is required.
          */
          /*static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs&)
          {

          }*/

          // Called to create a "quantized snapshot" of the struct
        static void Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
        {
            // Actually do the real quantization step here next instead of just in serialize, will save on memory overall
            const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
            QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
            
            Target.QuantizationLevel = (uint8)Source.QuantizationLevel;
            Target.RotationQuantizationLevel = (uint8)Source.RotationQuantizationLevel;

            const FNetSerializer* Serializer = VectorNetQuantizeNetSerializers[Target.QuantizationLevel] ;
            const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfigs[Target.QuantizationLevel] ;

            FNetQuantizeArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Source = NetSerializerValuePointer(&Source.Position);
            MemberArgs.Target = NetSerializerValuePointer(&Target.Position[0]);
            Serializer->Quantize(Context, MemberArgs);

            // Manually quantize these
            switch (Source.RotationQuantizationLevel)
            {
            case EVRRotationQuantization::RoundTo10Bits:
            {        
                Target.Rotation[0] = FBPVRComponentPosRep::CompressAxisTo10BitShort(Source.Rotation.Pitch);
                Target.Rotation[1] = FBPVRComponentPosRep::CompressAxisTo10BitShort(Source.Rotation.Yaw);
                Target.Rotation[2] = FBPVRComponentPosRep::CompressAxisTo10BitShort(Source.Rotation.Roll);
            }break;

            case EVRRotationQuantization::RoundToShort:
            {
                Target.Rotation[0] = FRotator::CompressAxisToShort(Source.Rotation.Pitch);
                Target.Rotation[1] = FRotator::CompressAxisToShort(Source.Rotation.Yaw);
                Target.Rotation[2] = FRotator::CompressAxisToShort(Source.Rotation.Roll);
            }break;
            }
        }

        // Called to apply the quantized snapshot back to gameplay memory
        static void Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
        {
            const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
            SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);
            
            Target.QuantizationLevel = (EVRVectorQuantization)Source.QuantizationLevel;
            Target.RotationQuantizationLevel = (EVRRotationQuantization)Source.RotationQuantizationLevel;

            const FNetSerializer* Serializer = VectorNetQuantizeNetSerializers[Source.QuantizationLevel];
            const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfigs[Source.QuantizationLevel];

            FNetDequantizeArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Source = NetSerializerValuePointer(&Source.Position[0]);
            MemberArgs.Target = NetSerializerValuePointer(&Target.Position);
            Serializer->Dequantize(Context, MemberArgs);
            
            // Manually quantize these
            switch (Target.RotationQuantizationLevel)
            {
            case EVRRotationQuantization::RoundTo10Bits:
            {         
                Target.Rotation.Pitch = FBPVRComponentPosRep::DecompressAxisFrom10BitShort(Source.Rotation[0]);
                Target.Rotation.Yaw = FBPVRComponentPosRep::DecompressAxisFrom10BitShort(Source.Rotation[1]);
                Target.Rotation.Roll = FBPVRComponentPosRep::DecompressAxisFrom10BitShort(Source.Rotation[2]);
            }break;

            case EVRRotationQuantization::RoundToShort:
            {
                Target.Rotation.Pitch = FRotator::DecompressAxisFromShort(Source.Rotation[0]);
                Target.Rotation.Yaw = FRotator::DecompressAxisFromShort(Source.Rotation[1]);
                Target.Rotation.Roll = FRotator::DecompressAxisFromShort(Source.Rotation[2]);
            }break;
            }
        }

        // Serialize into bitstream
        static void Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
        {
            const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
            FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

            Writer->WriteBits(static_cast<uint32>(Source.QuantizationLevel), 1);
            Writer->WriteBits(static_cast<uint32>(Source.RotationQuantizationLevel), 1);

            const FNetSerializer* Serializer = VectorNetQuantizeNetSerializers[Source.QuantizationLevel] ;
            const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfigs[Source.QuantizationLevel];

            FNetSerializeArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Source = NetSerializerValuePointer(&Source.Position[0]);
            Serializer->Serialize(Context, MemberArgs);

            switch ((EVRRotationQuantization)Source.RotationQuantizationLevel)
            {
                case EVRRotationQuantization::RoundTo10Bits:
                {
                    Writer->WriteBits(static_cast<uint32>(Source.Rotation[0]), 10);
                    Writer->WriteBits(static_cast<uint32>(Source.Rotation[1]), 10);
                    Writer->WriteBits(static_cast<uint32>(Source.Rotation[2]), 10);
                }break;

                case EVRRotationQuantization::RoundToShort:
                {
                    Writer->WriteBits(static_cast<uint32>(Source.Rotation[0]), 16);
                    Writer->WriteBits(static_cast<uint32>(Source.Rotation[1]), 16);
                    Writer->WriteBits(static_cast<uint32>(Source.Rotation[2]), 16);
                }break;
            }
   
        }

        // Deserialize from bitstream
        static void Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
        {
            QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
            FNetBitStreamReader* Reader = Context.GetBitStreamReader();

            Target.QuantizationLevel = Reader->ReadBits(1);
            Target.RotationQuantizationLevel = Reader->ReadBits(1);

            const FNetSerializer* Serializer = VectorNetQuantizeNetSerializers[Target.QuantizationLevel] ;
            const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfigs[Target.QuantizationLevel];

            FNetDeserializeArgs MemberArgs = Args;
            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
            MemberArgs.Target = NetSerializerValuePointer(&Target.Position[0]);
            Serializer->Deserialize(Context, MemberArgs);

            switch ((EVRRotationQuantization)Target.RotationQuantizationLevel)
            {
            case EVRRotationQuantization::RoundTo10Bits:
            {
                Target.Rotation[0] = Reader->ReadBits(10);
                Target.Rotation[1] = Reader->ReadBits(10);
                Target.Rotation[2] = Reader->ReadBits(10);
            }break;

            case EVRRotationQuantization::RoundToShort:
            {
                Target.Rotation[0] = Reader->ReadBits(16);
                Target.Rotation[1] = Reader->ReadBits(16);
                Target.Rotation[2] = Reader->ReadBits(16);
            }break;
            }
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

                if (L.QuantizationLevel != R.QuantizationLevel) return false;
                if (L.RotationQuantizationLevel != R.RotationQuantizationLevel) return false;
               
                if (!L.Position.Equals(R.Position)) return false;
                if (!L.Rotation.Equals(R.Rotation)) return false;

                return true;
            }
        }
    };


	static const FName PropertyNetSerializerRegistry_NAME_BPVRComponentPosRep("BPVRComponentPosRep");
	UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_BPVRComponentPosRep, FBPVRComponentPosRepNetSerializer);

	FBPVRComponentPosRepNetSerializer::FNetSerializerRegistryDelegates::~FNetSerializerRegistryDelegates()
	{
		UE_NET_UNREGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_BPVRComponentPosRep);
	}

	void FBPVRComponentPosRepNetSerializer::FNetSerializerRegistryDelegates::OnPreFreezeNetSerializerRegistry()
	{
        InitNetSerializer();
		UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_BPVRComponentPosRep);
	}

	/*void FBPVRComponentPosRepNetSerializer::FNetSerializerRegistryDelegates::OnPostFreezeNetSerializerRegistry()
	{
	}*/

    UE_NET_IMPLEMENT_SERIALIZER(FBPVRComponentPosRepNetSerializer);
}
