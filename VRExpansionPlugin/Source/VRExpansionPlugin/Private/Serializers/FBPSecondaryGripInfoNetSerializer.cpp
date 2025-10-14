#include "Serializers/FBPSecondaryGripInfoNetSerializer.h"
#include "Serializers/SerializerHelpers.h"
#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Serializers/FTransformNetQuantizeNetSerializer.h"


namespace UE::Net
{

    // -----------------------------------------------------------------------------
    // Iris serializer for FBPSecondaryGripInfo
    // -----------------------------------------------------------------------------
    struct FBPSecondaryGripInfoNetSerializer
    {
		inline static const FVectorNetQuantize100NetSerializerConfig FTransformQuantizeSerializerConfig;
		inline static const FObjectPtrNetSerializerConfig ObjectPtrNetSerializerConfig;
		inline static const FNameNetSerializerConfig NameNetSerializerConfig;

		inline static const FNetSerializerConfig* FTransformQuantizeSerializerConfigPtr = &FTransformQuantizeSerializerConfig;
		inline static const FNetSerializer* FTransformQuantizeNetSerializerPtr;

		inline static const FNetSerializerConfig* FObjectPtrSerializerConfigPtr = &ObjectPtrNetSerializerConfig;
		inline static const FNetSerializer* FObjectPtrNetSerializerPtr;

		inline static const FNetSerializerConfig* FNameSerializerConfigPtr = &NameNetSerializerConfig;
		inline static const FNetSerializer* FNameNetSerializerPtr;



        class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
        {
        public:
            virtual ~FNetSerializerRegistryDelegates();

			void InitNetSerializer()
			{
				FBPSecondaryGripInfoNetSerializer::FTransformQuantizeNetSerializerPtr = &UE_NET_GET_SERIALIZER(FTransformNetQuantizeNetSerializer);
				FBPSecondaryGripInfoNetSerializer::FObjectPtrNetSerializerPtr = &UE_NET_GET_SERIALIZER(FObjectPtrNetSerializer);
				FBPSecondaryGripInfoNetSerializer::FNameNetSerializerPtr = &UE_NET_GET_SERIALIZER(FNameNetSerializer);
			}

        private:
            virtual void OnPreFreezeNetSerializerRegistry() override;
            //virtual void OnPostFreezeNetSerializerRegistry() override;
        };

        inline static FBPSecondaryGripInfoNetSerializer::FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;

        /** Version is required. */
        static constexpr uint32 Version = 0;

        struct alignas(8) FQuantizedData
        {
			uint8 bHasSecondaryAttachment;
			FObjectNetSerializerQuantizedReferenceStorage SecondaryAttachment;
			FTransformNetQuantizeQuantizedData SecondaryRelativeTransform;

			uint8 bIsSlotGrip;
			
			alignas(8) uint8 SecondarySlotName[GetNameNetSerializerSafeQuantizedSize()];

			uint16 LerpToRate;

			// Not Replicated
			// SecondaryGripDistance
			//EGripLerpState GripLerpState;
			//float curLerp;
			//FVector LastRelativeLocation;
        };

        typedef FBPSecondaryGripInfo SourceType;
        typedef FQuantizedData QuantizedType;
        typedef FBPSecondaryGripInfoNetSerializerConfig ConfigType;
        inline static const ConfigType DefaultConfig;

		// TODO: This is actually a struct that could use some delta serialization implementations.
        /** Set to false when a same value delta compression method is undesirable, for example when the serializer only writes a single bit for the state. */
        static constexpr bool bUseDefaultDelta = true;
		static constexpr bool bHasDynamicState = true;
		//static constexpr bool bHasCustomNetReference = true;

          // Called to create a "quantized snapshot" of the struct
		static void Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
		{
			// Actually do the real quantization step here next instead of just in serialize, will save on memory overall
			const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
			QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

			Target.bHasSecondaryAttachment = Source.bHasSecondaryAttachment ? 1 : 0;

			if (Target.bHasSecondaryAttachment)
			{
				// ObjectPtr
				const FNetSerializer* ObjSerializer = FObjectPtrNetSerializerPtr;
				const FNetSerializerConfig* ObjSerializerConfig = FObjectPtrSerializerConfigPtr;

				//Target.SecondaryAttachment
				FNetQuantizeArgs MemberArgsObj = Args;
				MemberArgsObj.NetSerializerConfig = NetSerializerConfigParam(ObjSerializerConfig);
				MemberArgsObj.Source = NetSerializerValuePointer(&Source.SecondaryAttachment);
				MemberArgsObj.Target = NetSerializerValuePointer(&Target.SecondaryAttachment);
				ObjSerializer->Quantize(Context, MemberArgsObj);


				// Transform
				const FNetSerializer* TransformSerializer = FTransformQuantizeNetSerializerPtr;
				const FNetSerializerConfig* TransformSerializerConfig = FTransformQuantizeSerializerConfigPtr;

				//SecondaryRelativeTransform
				FNetQuantizeArgs MemberArgsTransform = Args;
				MemberArgsTransform.NetSerializerConfig = NetSerializerConfigParam(TransformSerializerConfig);
				MemberArgsTransform.Source = NetSerializerValuePointer(&Source.SecondaryRelativeTransform);
				MemberArgsTransform.Target = NetSerializerValuePointer(&Target.SecondaryRelativeTransform);
				TransformSerializer->Quantize(Context, MemberArgsTransform);

				Target.bIsSlotGrip = Source.bIsSlotGrip ? 1 : 0;
			
				// FName
				const FNetSerializer* NameSerializer = FNameNetSerializerPtr;
				const FNetSerializerConfig* NameSerializerConfig = FNameSerializerConfigPtr;

				//SecondarySlotName
				FNetQuantizeArgs MemberArgs = Args;
				MemberArgs.NetSerializerConfig = NetSerializerConfigParam(NameSerializerConfig);
				MemberArgs.Source = NetSerializerValuePointer(&Source.SecondarySlotName);
				MemberArgs.Target = NetSerializerValuePointer(&Target.SecondarySlotName);
				NameSerializer->Quantize(Context, MemberArgs);
			}

			// This is 0.0 - 16.0, using compression to get it smaller, 4 bits = max 16 + 1 bit for sign and 7 bits precision for 128 / full 2 digit precision
			Target.LerpToRate = GetCompressedFloat<16, 12>(Source.LerpToRate);
		}

		// Called to apply the quantized snapshot back to gameplay memory
		static void Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
		{
			const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
			SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

			Target.bHasSecondaryAttachment = Source.bHasSecondaryAttachment != 0;

			if (Target.bHasSecondaryAttachment)
			{
				// ObjectPtr
				const FNetSerializer* ObjSerializer = FObjectPtrNetSerializerPtr;
				const FNetSerializerConfig* ObjSerializerConfig = FObjectPtrSerializerConfigPtr;

				//Target.SecondaryAttachment
				FNetDequantizeArgs MemberArgsObj = Args;
				MemberArgsObj.NetSerializerConfig = NetSerializerConfigParam(ObjSerializerConfig);
				MemberArgsObj.Source = NetSerializerValuePointer(&Source.SecondaryAttachment);
				MemberArgsObj.Target = NetSerializerValuePointer(&Target.SecondaryAttachment);
				ObjSerializer->Dequantize(Context, MemberArgsObj);


				// Transform
				const FNetSerializer* TransformSerializer = FTransformQuantizeNetSerializerPtr;
				const FNetSerializerConfig* TransformSerializerConfig = FTransformQuantizeSerializerConfigPtr;

				//SecondaryRelativeTransform
				FNetDequantizeArgs MemberArgsTransform = Args;
				MemberArgsTransform.NetSerializerConfig = NetSerializerConfigParam(TransformSerializerConfig);
				MemberArgsTransform.Source = NetSerializerValuePointer(&Source.SecondaryRelativeTransform);
				MemberArgsTransform.Target = NetSerializerValuePointer(&Target.SecondaryRelativeTransform);
				TransformSerializer->Dequantize(Context, MemberArgsTransform);

				Target.bIsSlotGrip = Source.bIsSlotGrip != 0;

				// FName
				const FNetSerializer* NameSerializer = FNameNetSerializerPtr;
				const FNetSerializerConfig* NameSerializerConfig = FNameSerializerConfigPtr;

				//SecondarySlotName
				FNetDequantizeArgs MemberArgs = Args;
				MemberArgs.NetSerializerConfig = NetSerializerConfigParam(NameSerializerConfig);
				MemberArgs.Source = NetSerializerValuePointer(&Source.SecondarySlotName);
				MemberArgs.Target = NetSerializerValuePointer(&Target.SecondarySlotName);
				NameSerializer->Dequantize(Context, MemberArgs);
			}

			// This is 0.0 - 16.0, using compression to get it smaller, 4 bits = max 16 + 1 bit for sign and 7 bits precision for 128 / full 2 digit precision
			Target.LerpToRate = GetDecompressedFloat<16, 12>(Source.LerpToRate);
		}

		// Serialize into bitstream
		static void Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
		{
			const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
			FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

			Writer->WriteBits(static_cast<uint32>(Source.bHasSecondaryAttachment), 1);

			if (Source.bHasSecondaryAttachment != 0)
			{
				// ObjectPtr
				const FNetSerializer* ObjSerializer = FObjectPtrNetSerializerPtr;
				const FNetSerializerConfig* ObjSerializerConfig = FObjectPtrSerializerConfigPtr;

				//Target.SecondaryAttachment
				FNetSerializeArgs MemberArgsObj = Args;
				MemberArgsObj.NetSerializerConfig = NetSerializerConfigParam(ObjSerializerConfig);
				MemberArgsObj.Source = NetSerializerValuePointer(&Source.SecondaryAttachment);
				ObjSerializer->Serialize(Context, MemberArgsObj);


				// Transform
				const FNetSerializer* TransformSerializer = FTransformQuantizeNetSerializerPtr;
				const FNetSerializerConfig* TransformSerializerConfig = FTransformQuantizeSerializerConfigPtr;

				//SecondaryRelativeTransform
				FNetSerializeArgs MemberArgsTransform = Args;
				MemberArgsTransform.NetSerializerConfig = NetSerializerConfigParam(TransformSerializerConfig);
				MemberArgsTransform.Source = NetSerializerValuePointer(&Source.SecondaryRelativeTransform);
				TransformSerializer->Serialize(Context, MemberArgsTransform);

				Writer->WriteBits(static_cast<uint32>(Source.bIsSlotGrip), 1);

				// FName
				const FNetSerializer* NameSerializer = FNameNetSerializerPtr;
				const FNetSerializerConfig* NameSerializerConfig = FNameSerializerConfigPtr;

				//SecondarySlotName
				FNetSerializeArgs MemberArgs = Args;
				MemberArgs.NetSerializerConfig = NetSerializerConfigParam(NameSerializerConfig);
				MemberArgs.Source = NetSerializerValuePointer(&Source.SecondarySlotName);
				NameSerializer->Serialize(Context, MemberArgs);
			}

			// This is 0.0 - 16.0, using compression to get it smaller, 4 bits = max 16 + 1 bit for sign and 7 bits precision for 128 / full 2 digit precision
			Writer->WriteBits(static_cast<uint32>(Source.LerpToRate), 12);
		}

		// Deserialize from bitstream
		static void Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
		{
			QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
			FNetBitStreamReader* Reader = Context.GetBitStreamReader();

			Target.bHasSecondaryAttachment = Reader->ReadBits(1);

			if (Target.bHasSecondaryAttachment != 0)
			{
				// ObjectPtr
				const FNetSerializer* ObjSerializer = FObjectPtrNetSerializerPtr;
				const FNetSerializerConfig* ObjSerializerConfig = FObjectPtrSerializerConfigPtr;

				//Target.SecondaryAttachment
				FNetDeserializeArgs MemberArgsObj = Args;
				MemberArgsObj.NetSerializerConfig = NetSerializerConfigParam(ObjSerializerConfig);
				MemberArgsObj.Target = NetSerializerValuePointer(&Target.SecondaryAttachment);
				ObjSerializer->Deserialize(Context, MemberArgsObj);


				// Transform
				const FNetSerializer* TransformSerializer = FTransformQuantizeNetSerializerPtr;
				const FNetSerializerConfig* TransformSerializerConfig = FTransformQuantizeSerializerConfigPtr;

				//SecondaryRelativeTransform
				FNetDeserializeArgs MemberArgsTransform = Args;
				MemberArgsTransform.NetSerializerConfig = NetSerializerConfigParam(TransformSerializerConfig);
				MemberArgsTransform.Target = NetSerializerValuePointer(&Target.SecondaryRelativeTransform);
				TransformSerializer->Deserialize(Context, MemberArgsTransform);

				Target.bIsSlotGrip = Reader->ReadBits(1);

				// FName
				const FNetSerializer* NameSerializer = FNameNetSerializerPtr;
				const FNetSerializerConfig* NameSerializerConfig = FNameSerializerConfigPtr;

				//SecondarySlotName
				FNetDeserializeArgs MemberArgs = Args;
				MemberArgs.NetSerializerConfig = NetSerializerConfigParam(NameSerializerConfig);
				MemberArgs.Target = NetSerializerValuePointer(&Target.SecondarySlotName);
				NameSerializer->Deserialize(Context, MemberArgs);
			}

			// This is 0.0 - 16.0, using compression to get it smaller, 4 bits = max 16 + 1 bit for sign and 7 bits precision for 128 / full 2 digit precision
			Target.LerpToRate = Reader->ReadBits(12);
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

				if (L.bHasSecondaryAttachment != R.bHasSecondaryAttachment) return false;

				if (L.bHasSecondaryAttachment)
				{
					if (L.SecondaryAttachment != R.SecondaryAttachment) return false;
					if (!L.SecondaryRelativeTransform.Equals(R.SecondaryRelativeTransform)) return false;
					if (L.bIsSlotGrip != R.bIsSlotGrip) return false;
					if (L.SecondarySlotName != R.SecondarySlotName) return false;
				}

				if (L.LerpToRate != R.LerpToRate) return false;

				return true;
			}
		}

		static void CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
		{
			const QuantizedType* Source = reinterpret_cast<const QuantizedType*>(Args.Source);
			QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

			const FNetSerializer* NameSerializer = FNameNetSerializerPtr;
			const FNetSerializerConfig* NameSerializerConfig = FNameSerializerConfigPtr;

			FNetCloneDynamicStateArgs MemberArgs = Args;
			MemberArgs.NetSerializerConfig = NetSerializerConfigParam(NameSerializerConfig);
			MemberArgs.Target = NetSerializerValuePointer(&Target.SecondarySlotName);
			MemberArgs.Source = NetSerializerValuePointer(&Source->SecondarySlotName);
			NameSerializer->CloneDynamicState(Context, MemberArgs);



			const FNetSerializer* ObjSerializer = FObjectPtrNetSerializerPtr;
			const FNetSerializerConfig* ObjSerializerConfig = FObjectPtrSerializerConfigPtr;

			FNetCloneDynamicStateArgs ObjMemberArgs = Args;
			ObjMemberArgs.NetSerializerConfig = NetSerializerConfigParam(ObjSerializerConfig);
			ObjMemberArgs.Target = NetSerializerValuePointer(&Target.SecondaryAttachment);
			ObjMemberArgs.Source = NetSerializerValuePointer(&Source->SecondaryAttachment);
			ObjSerializer->CloneDynamicState(Context, MemberArgs);
		}

		static void FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
		{
			QuantizedType& Source = *reinterpret_cast<QuantizedType*>(Args.Source);

			const FNetSerializer* NameSerializer = FNameNetSerializerPtr;
			const FNetSerializerConfig* NameSerializerConfig = FNameSerializerConfigPtr;

			FNetFreeDynamicStateArgs MemberArgs = Args;
			MemberArgs.NetSerializerConfig = NetSerializerConfigParam(NameSerializerConfig);
			MemberArgs.Source = NetSerializerValuePointer(&Source.SecondarySlotName);
			NameSerializer->FreeDynamicState(Context, MemberArgs);


			const FNetSerializer* ObjSerializer = FObjectPtrNetSerializerPtr;
			const FNetSerializerConfig* ObjSerializerConfig = FObjectPtrSerializerConfigPtr;

			FNetFreeDynamicStateArgs ObjMemberArgs = Args;
			ObjMemberArgs.NetSerializerConfig = NetSerializerConfigParam(ObjSerializerConfig);
			ObjMemberArgs.Source = NetSerializerValuePointer(&Source.SecondaryAttachment);
			ObjSerializer->FreeDynamicState(Context, MemberArgs);
		}

		static void Apply(FNetSerializationContext&, const FNetApplyArgs& Args)
		{
			const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
			SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

			Target.bHasSecondaryAttachment = Source.bHasSecondaryAttachment;

			if (Target.bHasSecondaryAttachment)
			{
				Target.SecondaryAttachment = Source.SecondaryAttachment;
				Target.SecondaryRelativeTransform = Source.SecondaryRelativeTransform;
				Target.bIsSlotGrip = Source.bIsSlotGrip;
				Target.SecondarySlotName = Source.SecondarySlotName;
			}
			else
			{
				// Clear non repped values
				Target.SecondaryAttachment = nullptr;
				Target.SecondaryRelativeTransform = FTransform::Identity;
				Target.bIsSlotGrip = false;
				Target.SecondarySlotName = NAME_None;
			}

			Target.LerpToRate = Source.LerpToRate;

			// Not Replicated
			// SecondaryGripDistance
			//EGripLerpState GripLerpState;
			//float curLerp;
			//FVector LastRelativeLocation;
		}

		/*static void CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
		{
			const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
			const FNetSerializer* ObjSerializer = FObjectPtrNetSerializerPtr;
			const FNetSerializerConfig* ObjSerializerConfig = FObjectPtrSerializerConfigPtr;

			FNetCollectReferencesArgs CollectNetReferencesArgs = Args;
			CollectNetReferencesArgs.NetSerializerConfig = FObjectPtrSerializerConfigPtr;
			CollectNetReferencesArgs.Source = NetSerializerValuePointer(&Source.SecondaryAttachment);
			ObjSerializer->CollectNetReferences(Context, CollectNetReferencesArgs);
		}*/
    };


	static const FName PropertyNetSerializerRegistry_NAME_BPSecondaryGripInfo("BPSecondaryGripInfo");
	UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_BPSecondaryGripInfo, FBPSecondaryGripInfoNetSerializer);

	FBPSecondaryGripInfoNetSerializer::FNetSerializerRegistryDelegates::~FNetSerializerRegistryDelegates()
	{
		UE_NET_UNREGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_BPSecondaryGripInfo);
	}

	void FBPSecondaryGripInfoNetSerializer::FNetSerializerRegistryDelegates::OnPreFreezeNetSerializerRegistry()
	{
		InitNetSerializer();
		UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_BPSecondaryGripInfo);
	}

    UE_NET_IMPLEMENT_SERIALIZER(FBPSecondaryGripInfoNetSerializer);
}
