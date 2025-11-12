#include "Serializers/FBPXRSkeletalRepContainerNetSerializer.h"
#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/PackedVectorNetSerializers.h"
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"

#include "OpenXRHandPoseComponent.h"


namespace UE::Net
{

    // -----------------------------------------------------------------------------
    // Iris serializer for FBPXRSkeletalRepContainer
    // -----------------------------------------------------------------------------
    struct FBPXRSkeletalRepContainerNetSerializer
    {
        inline static const  FVectorNetQuantize100NetSerializerConfig Quantize100SerializerConfig;

        inline static const FNetSerializerConfig* VectorNetQuantizeNetSerializerConfig = &Quantize100SerializerConfig;
        inline static const FNetSerializer* VectorNetQuantizeNetSerializer;

        class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
        {
        public:
            virtual ~FNetSerializerRegistryDelegates();

            void InitNetSerializer()
            {
                FBPXRSkeletalRepContainerNetSerializer::VectorNetQuantizeNetSerializer = &UE_NET_GET_SERIALIZER(FVectorNetQuantize100NetSerializer);
            }

        private:
            virtual void OnPreFreezeNetSerializerRegistry() override;
            //virtual void OnPostFreezeNetSerializerRegistry() override;
        };

        inline static FBPXRSkeletalRepContainerNetSerializer::FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;


        /** Version is required. */
        static constexpr uint32 Version = 0;

        struct alignas(8) FSkeletalTransformQuantizedData
        {
            uint64 Position[4]; // We don't need to store double for tracked device positions, but their forwarded serializer uses it
            uint16 Rotation[3];
        };

        struct alignas(8) FBPXRSkeletalRepContainerQuantizedData
        {
            uint8 TargetHand;
            uint8 bAllowDeformingMesh;
            uint8 bEnableUE4HandRepSavings;

            uint16 ElementCount;
            FSkeletalTransformQuantizedData SkeletalTransforms[EHandKeypointCount];
            // List of array data, we know the max elements so we can statically set it to that
            // Technically should be -6 since we dont use some but im going to future proof it a bit here
        };

        typedef FBPXRSkeletalRepContainer SourceType;
        typedef FBPXRSkeletalRepContainerQuantizedData QuantizedType;
        typedef FBPXRSkeletalRepContainerNetSerializerConfig ConfigType;
        inline static const ConfigType DefaultConfig;

        /** Set to false when a same value delta compression method is undesirable, for example when the serializer only writes a single bit for the state. */
        static constexpr bool bUseDefaultDelta = true;

        // Not using delta as this struct is sent on a timer and not changed incrementally


        static constexpr uint32 ElementSizeBytes = sizeof(FSkeletalTransformQuantizedData);

          // Called to create a "quantized snapshot" of the struct
        static void Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
        {
            // Actually do the real quantization step here next instead of just in serialize, will save on memory overall
            const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
            QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

            Target.TargetHand = (uint8)Source.TargetHand;
            Target.bAllowDeformingMesh = Source.bAllowDeformingMesh ? 1 : 0;
            Target.bEnableUE4HandRepSavings = Source.bEnableUE4HandRepSavings ? 1 : 0;

            // Free data if non null
            Target.ElementCount = 0;

            const uint32 Num = Source.SkeletalTransforms.Num();
            Target.ElementCount = static_cast<uint16>(Num);

            if (Num > 0)
            {
                FRotator TargetRot;
                FVector TargetLoc;
                for (uint16 i = 0; i < Num; ++i)
                {
                    TargetRot = Source.SkeletalTransforms[i].Rotator();
                    TargetLoc = Source.SkeletalTransforms[i].GetLocation();

                    Target.SkeletalTransforms[i].Rotation[0] = FRotator::CompressAxisToShort(TargetRot.Pitch);
                    Target.SkeletalTransforms[i].Rotation[1] = FRotator::CompressAxisToShort(TargetRot.Yaw);
                    Target.SkeletalTransforms[i].Rotation[2] = FRotator::CompressAxisToShort(TargetRot.Roll);

                    const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                    const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                    FNetQuantizeArgs MemberArgs = Args;
                    MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                    MemberArgs.Source = NetSerializerValuePointer(&TargetLoc);
                    MemberArgs.Target = NetSerializerValuePointer(&Target.SkeletalTransforms[i].Position[0]);
                    Serializer->Quantize(Context, MemberArgs);
                }
            }
        }

        // Called to apply the quantized snapshot back to gameplay memory
        static void Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
        {
            const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
            SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

            Target.TargetHand = (EVRSkeletalHandIndex)Source.TargetHand;
            Target.bAllowDeformingMesh = Source.bAllowDeformingMesh != 0;
            Target.bEnableUE4HandRepSavings = Source.bEnableUE4HandRepSavings != 0;

            const uint16 Count = Source.ElementCount;
            Target.SkeletalTransforms.Reset();
            if (Count > 0)
            {
                Target.SkeletalTransforms.AddUninitialized(Count);

                const FSkeletalTransformQuantizedData* Data = Source.SkeletalTransforms;
                FRotator TargetRot;
                FVector TargetLoc;
                for (int i = 0; i < Count; ++i)
                {
                    TargetRot.Pitch = FRotator::DecompressAxisFromShort(Data[i].Rotation[0]);
                    TargetRot.Yaw = FRotator::DecompressAxisFromShort(Data[i].Rotation[1]);
                    TargetRot.Roll = FRotator::DecompressAxisFromShort(Data[i].Rotation[2]);
                    TargetRot.Normalize();

                    const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                    const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                    FNetDequantizeArgs MemberArgs = Args;
                    MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                    MemberArgs.Source = NetSerializerValuePointer(&TargetLoc);
                    MemberArgs.Target = NetSerializerValuePointer(&Data[i].Position[0]);
                    Serializer->Dequantize(Context, MemberArgs);

                    Target.SkeletalTransforms[i].SetComponents(TargetRot.Quaternion(), TargetLoc, FVector(1.0f));
                }
            }
        }

        // Serialize into bitstream
        static void Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
        {
            const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
            FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

            // write header bytes (small fields) — do as bytes for compactness
            Writer->WriteBits(Source.TargetHand, 8);
            Writer->WriteBits(Source.bAllowDeformingMesh, 1);
            Writer->WriteBits(Source.bEnableUE4HandRepSavings, 1);
                  
            int32 BoneCountAdjustment = 6 + (Source.bEnableUE4HandRepSavings != 0 ? 4 : 0);
            uint8 TransformCount = EHandKeypointCount - BoneCountAdjustment;

            bool bHasValidData = Source.ElementCount >= TransformCount;
            
            Writer->WriteBits(bHasValidData, 1);

            if (bHasValidData)
            {
                // write element count (16 bits)
                uint32 ElemCount = Source.ElementCount;

                if (ElemCount < 1)
                { 
                    // Write nothing
                    ElemCount = 0;
                }

                Writer->WriteBits(ElemCount, 16);

                const FSkeletalTransformQuantizedData* Data = Source.SkeletalTransforms;
                for (uint16 i = 0; i < ElemCount; ++i)
                {
                    if (Source.bAllowDeformingMesh)
                    {
                        const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                        const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                        FNetSerializeArgs MemberArgs = Args;
                        MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                        MemberArgs.Source = NetSerializerValuePointer(&Data[i].Position[0]);
                        Serializer->Serialize(Context, MemberArgs);
                    }

                    Writer->WriteBits(Data[i].Rotation[0], 16);
                    Writer->WriteBits(Data[i].Rotation[1], 16);
                    Writer->WriteBits(Data[i].Rotation[2], 16);
                }
            }
        }

        // Deserialize from bitstream
        static void Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
        {
            QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
            FNetBitStreamReader* Reader = Context.GetBitStreamReader();

            // write header bytes (small fields) — do as bytes for compactness
            
            Target.TargetHand = Reader->ReadBits(8);
            Target.bAllowDeformingMesh = Reader->ReadBits(1);
            Target.bEnableUE4HandRepSavings = Reader->ReadBits(1);

            int32 BoneCountAdjustment = 6 + (Target.bEnableUE4HandRepSavings != 0 ? 4 : 0);
            uint8 TransformCount = EHandKeypointCount - BoneCountAdjustment;

            bool bHasValidData = Reader->ReadBits(1) != 0;

            if (bHasValidData)
            {
                // write element count (16 bits)
                uint32 ElemCount = Reader->ReadBits(16);

                Target.ElementCount = static_cast<uint16>(ElemCount);

                if (ElemCount > 0)
                {
                    FSkeletalTransformQuantizedData* Data = Target.SkeletalTransforms;
                    for (uint16 i = 0; i < ElemCount; ++i)
                    {
                        if (Target.bAllowDeformingMesh)
                        {
                            const FNetSerializer* Serializer = VectorNetQuantizeNetSerializer;
                            const FNetSerializerConfig* SerializerConfig = VectorNetQuantizeNetSerializerConfig;

                            FNetDeserializeArgs MemberArgs = Args;
                            MemberArgs.NetSerializerConfig = NetSerializerConfigParam(SerializerConfig);
                            MemberArgs.Target = NetSerializerValuePointer(&Data[i].Position[0]);
                            Serializer->Deserialize(Context, MemberArgs);
                        }

                        Data[i].Rotation[0] = Reader->ReadBits(16);
                        Data[i].Rotation[1] = Reader->ReadBits(16);
                        Data[i].Rotation[2] = Reader->ReadBits(16);
                    }
                }
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


                if (L.bAllowDeformingMesh != R.bAllowDeformingMesh) return false;
                if (L.bEnableUE4HandRepSavings != R.bEnableUE4HandRepSavings) return false;
                if (L.SkeletalTransforms.Num() != R.SkeletalTransforms.Num()) return false;
                
                // Using L num is valid as we already checked if they are the same count
                return FPlatformMemory::Memcmp(L.SkeletalTransforms.GetData(), R.SkeletalTransforms.GetData(), sizeof(FTransform) * L.SkeletalTransforms.Num()) == 0;
            }
        }

        static void Apply(FNetSerializationContext& Context, const FNetApplyArgs& Args)
        {
            const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
            SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);
            
            Target.bAllowDeformingMesh = Source.bAllowDeformingMesh;
            Target.bEnableUE4HandRepSavings = Source.bEnableUE4HandRepSavings;
            Target.TargetHand = Source.TargetHand;

            // Copys it over with new allocations
            Target.SkeletalTransforms = Source.SkeletalTransforms;       
        }

        static void CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
        {
            const QuantizedType& Source = *reinterpret_cast<QuantizedType*>(Args.Source);
            QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

            // copy small fields
            Target.TargetHand = Source.TargetHand;
            Target.bAllowDeformingMesh = Source.bAllowDeformingMesh;
            Target.bEnableUE4HandRepSavings = Source.bEnableUE4HandRepSavings;

            // copy elements
            Target.ElementCount = Source.ElementCount;

            const uint32 Count = Target.ElementCount;
            if (Count > 0)
            {
                const SIZE_T Bytes = SIZE_T(Count) * ElementSizeBytes;
                FMemory::Memcpy(Target.SkeletalTransforms, Source.SkeletalTransforms, Bytes);
            }
        }

        static void FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
        {
            QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Source);
            Target.ElementCount = 0;
        }
    };


	static const FName PropertyNetSerializerRegistry_NAME_BPXRSkeletalRepContainer("BPXRSkeletalRepContainer");
	UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_BPXRSkeletalRepContainer, FBPXRSkeletalRepContainerNetSerializer);

	FBPXRSkeletalRepContainerNetSerializer::FNetSerializerRegistryDelegates::~FNetSerializerRegistryDelegates()
	{
		UE_NET_UNREGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_BPXRSkeletalRepContainer);
	}

	void FBPXRSkeletalRepContainerNetSerializer::FNetSerializerRegistryDelegates::OnPreFreezeNetSerializerRegistry()
	{
        InitNetSerializer();
		UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_BPXRSkeletalRepContainer);
	}

    UE_NET_IMPLEMENT_SERIALIZER(FBPXRSkeletalRepContainerNetSerializer);
}
