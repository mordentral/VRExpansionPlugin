//#include "Serialization/ArchiveSaveCompressedProxy.h"
//#include "Serialization/ArchiveLoadCompressedProxy.h"

#include "GeomTools.h"
#include "DrawDebugHelpers.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Net/Core/PushModel/PushModel.h"
#include "TimerManager.h"
//#include "ImageWrapper/Public/IImageWrapper.h"
//#include "ImageWrapper/Public/IImageWrapperModule.h"

#include "VRRenderTargetManager.generated.h"


// #TODO: Dirty rects so don't have to send entire texture?

namespace RLE_Funcs
{
	enum RLE_Flags
	{
		RLE_CompressedByte = 1,
		RLE_CompressedShort = 2,
		RLE_Compressed24 = 3,
		RLE_NotCompressedByte = 4,
		RLE_NotCompressedShort = 5,
		RLE_NotCompressed24 = 6,
		//RLE_Empty = 3,
		//RLE_AllSame = 4,
		RLE_ContinueRunByte = 7,
		RLE_ContinueRunShort = 8,
		RLE_ContinueRun24 = 9
	};

	// #TODO: Think about endianess relevancy with the RLE encoding
	template <typename DataType>
	static bool RLEEncodeLine(TArray<DataType>* LineToEncode, TArray<uint8>* EncodedLine);

	template <typename DataType>
	static bool RLEEncodeBuffer(DataType* BufferToEncode, uint32 EncodeLength, TArray<uint8>* EncodedLine);

	template <typename DataType>
	static void RLEDecodeLine(TArray<uint8>* LineToDecode, TArray<DataType>* DecodedLine, bool bCompressed);

	template <typename DataType>
	static void RLEDecodeLine(const uint8* LineToDecode, uint32 Num, TArray<DataType>* DecodedLine, bool bCompressed);

	static inline void RLEWriteContinueFlag(uint32 Count, uint8** loc);

	template <typename DataType>
	static inline void RLEWriteRunFlag(uint32 Count, uint8** loc, TArray<DataType>& Data, bool bCompressed);
}


USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPVRReplicatedTextureStore
{
	GENERATED_BODY()
public:

	// Not automatically replicated, we are skipping it so that the array isn't checked
	// We manually copy the data into the serialization buffer during netserialize and keep
	// a flip flop dirty flag

	UPROPERTY()
		TArray<uint8> PackedData;
	TArray<uint16> UnpackedData;

	UPROPERTY()
	uint32 Width;

	UPROPERTY()
	uint32 Height;

	UPROPERTY()
		bool bUseColorMap;

	EPixelFormat PixelFormat;

	void Reset()
	{
		PackedData.Reset();
		UnpackedData.Reset();
		Width = 0;
		Height = 0;
		PixelFormat = (EPixelFormat)0;
		bUseColorMap = false;
	}

	void PackData()
	{
		RLE_Funcs::RLEEncodeBuffer<uint16>(UnpackedData.GetData(), UnpackedData.Num(), &PackedData);
		UnpackedData.Reset();
	}


	void UnPackData()
	{
		RLE_Funcs::RLEDecodeLine<uint16>(&PackedData, &UnpackedData, true);
		PackedData.Reset();
	}


	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		Ar.SerializeBits(&bUseColorMap, 1);
		Ar.SerializeIntPacked(Width);
		Ar.SerializeIntPacked(Height);
		Ar.SerializeBits(&PixelFormat, 8);

		// Serialize the raw transaction
		uint32 UncompressedBufferSize = PackedData.Num();
		Ar.SerializeIntPacked(UncompressedBufferSize);
		if (UncompressedBufferSize > 0)
		{
			Ar.SerializeCompressed(PackedData.GetData(), UncompressedBufferSize, NAME_Zlib, ECompressionFlags::COMPRESS_BiasSpeed);
		}

		if (Ar.IsLoading() && UncompressedBufferSize == 0)
		{
			PackedData.Empty();
		}

		return bOutSuccess;
	}

};

template<>
struct TStructOpsTypeTraits< FBPVRReplicatedTextureStore > : public TStructOpsTypeTraitsBase2<FBPVRReplicatedTextureStore>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
	};
};


USTRUCT()
struct FRenderDataStore {
	GENERATED_BODY()

	TArray<FColor> ColorData;
	FRenderCommandFence RenderFence;
	FIntPoint Size2D;
	EPixelFormat PixelFormat;

	FRenderDataStore() {
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVROnRenderTargetSaved, FBPVRReplicatedTextureStore, ColorData);


/**
* This class is used as a proxy to send owner only RPCs
*/
UCLASS(ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API ARenderTargetReplicationProxy : public AActor
{
	GENERATED_BODY()

public:
	ARenderTargetReplicationProxy(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

};

ARenderTargetReplicationProxy::ARenderTargetReplicationProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bOnlyRelevantToOwner = true;
	bNetUseOwnerRelevancy = true;
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	SetReplicateMovement(false);
}

USTRUCT()
struct FClientRepData {
	GENERATED_BODY()

	UPROPERTY()
		TWeakObjectPtr<APlayerController> PC;

	UPROPERTY()
		TWeakObjectPtr<ARenderTargetReplicationProxy> ReplicationProxy;

	UPROPERTY()
		bool bIsRelevant;

	UPROPERTY()
		bool bIsDirty;

	FClientRepData() 
	{
		bIsRelevant = false;
		bIsDirty = false;
	}
};


/**
* This class stores reading requests for rendertargets and iterates over them
* It returns the pixel data at the end of processing
* It references code from: https://github.com/TimmHess/UnrealImageCapture
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRRenderTargetManager : public UActorComponent
{
	GENERATED_BODY()

public:

    UVRRenderTargetManager(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		bool bInitiallyReplicateTexture;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "RenderTargetManager")
		UCanvasRenderTarget2D* RenderTarget;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		int32 RenderTargetWidth;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		int32 RenderTargetHeight;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		FColor ClearColor;

	// List of 16 colors that we allow to draw on the render target with
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "RenderTargetManager|ColorMap")
		TMap<FColor, uint8> ColorMap;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager|ColorMap")
		bool bUseColorMap;

	UPROPERTY()
		TArray<FClientRepData> NetRelevancyLog;

	// Rate to poll for actor relevancy
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		float PollRelevancyTime;

	FTimerHandle NetRelevancyTimer_Handle;

	UPROPERTY(BlueprintAssignable, Category = "RenderTargetManager")
		FVROnRenderTargetSaved OnRenderTargetFinishedSave;

	UPROPERTY(Replicated, ReplicatedUsing = OnRep_TextureData)
		FBPVRReplicatedTextureStore RenderTargetStore;

	UFUNCTION()
		virtual void OnRep_TextureData()
	{
		DeCompressRenderTarget2D();
		// Write to buffer now
	}

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override
	{
		Super::GetLifetimeReplicatedProps(OutLifetimeProps);

#if WITH_PUSH_MODEL
		FDoRepLifetimeParams Params;

		Params.bIsPushBased = true;
		DOREPLIFETIME_WITH_PARAMS_FAST(UVRRenderTargetManager, RenderTargetStore, Params);
#else
		DOREPLIFETIME(UVRRenderTargetManager, RenderTargetStore);
#endif
	}


	void InitRenderTarget()
	{
		UWorld* World = GetWorld();


		if (RenderTargetWidth > 0 && RenderTargetHeight > 0 && World)
		{
			//UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(this, UCanvasRenderTarget2D::StaticClass(), RenderTargetWidth, RenderTargetHeight)

			RenderTarget = NewObject<UCanvasRenderTarget2D>(this);
			if (RenderTarget)
			{
				//NewCanvasRenderTarget->World = World;
				RenderTarget->InitAutoFormat(RenderTargetWidth, RenderTargetHeight);
				RenderTarget->ClearColor = ClearColor;
				RenderTarget->bAutoGenerateMips = false;
				RenderTarget->UpdateResourceImmediate(true);
			}
			else
			{
				RenderTarget = nullptr;
			}
		}
		else
		{
			RenderTarget =  nullptr;
		}
	}


	UFUNCTION(BlueprintCallable, Category = "VRRenderTargetManager|UtilityFunctions")
	bool GenerateTrisFromBoxPlaneIntersection(UPrimitiveComponent * PrimToBoxCheck, FTransform WorldTransformOfPlane, const FPlane & LocalProjectionPlane, FVector2D PlaneSize, FColor UVColor, TArray<FCanvasUVTri>& OutTris)
	{

		if (!PrimToBoxCheck)
			return false;

		OutTris.Reset();

		FBoxSphereBounds LocalBounds = PrimToBoxCheck->CalcLocalBounds();
		FVector Center = LocalBounds.Origin;
		FVector Extent = LocalBounds.BoxExtent;
		 
		// Transform into plane local space from our localspace
		FTransform LocalTrans = PrimToBoxCheck->GetComponentTransform() * WorldTransformOfPlane.Inverse();

		FVector BoxMin = Center - Extent;
		FVector BoxMax = Center + Extent;
		
		TArray<FVector> PointList;
		PointList.AddUninitialized(8); // 8 Is number of points on box

		PointList[0] = LocalTrans.TransformPosition(BoxMin);
		PointList[1] = LocalTrans.TransformPosition(BoxMax);
		PointList[2] = LocalTrans.TransformPosition(FVector(BoxMin.X, BoxMin.Y, BoxMax.Z));
		PointList[3] = LocalTrans.TransformPosition(FVector(BoxMin.X, BoxMax.Y, BoxMin.Z));
		PointList[4] = LocalTrans.TransformPosition(FVector(BoxMax.X, BoxMin.Y, BoxMin.Z));
		PointList[5] = LocalTrans.TransformPosition(FVector(BoxMin.X, BoxMax.Y, BoxMax.Z));
		PointList[6] = LocalTrans.TransformPosition(FVector(BoxMax.X, BoxMin.Y, BoxMax.Z));
		PointList[7] = LocalTrans.TransformPosition(FVector(BoxMax.X, BoxMax.Y, BoxMin.Z));

		// List of edges to check, 12 total, 2 points per edge
		int EdgeList[24] = 
		{ 
			0, 3, 
			0, 4,
			0, 2,
			2, 5,
			2, 6,
			4, 7,
			4, 6,
			6, 1,
			1, 7,
			1, 5,
			5, 3,
			3, 7
		};

		TArray<FVector2D> IntersectionPoints;

		FVector Intersection;
		float Time;

		FVector2D HalfPlane = PlaneSize / 2.f;
		FVector2D PtCenter;
		FVector2D NewPt;
		FVector PlanePoint;
		int CenterCount = 0;
		for(int i=0; i < 24; i+=2)
		{

			if (UKismetMathLibrary::LinePlaneIntersection(PointList[EdgeList[i]], PointList[EdgeList[i+1]], LocalProjectionPlane, Time, Intersection))
			{				
				//DrawDebugSphere(GetWorld(), WorldTransformOfPlane.TransformPosition(Intersection), 2.f, 32.f, FColor::Black);
				PlanePoint = Intersection;
				
				NewPt.X = ((PlanePoint.X + HalfPlane.X) / PlaneSize.X) * RenderTargetWidth;
				NewPt.Y = ((PlanePoint.Y + HalfPlane.Y) / PlaneSize.Y) * RenderTargetHeight;

				IntersectionPoints.Add(NewPt);
				PtCenter += NewPt;
				CenterCount++;
			}
		}

		if (IntersectionPoints.Num() <= 2)
		{
			return false;
		}

		// Get our center value
		PtCenter /= CenterCount;

		// Sort the points clockwise
		struct FPointSortCompare
		{
		public:
			FVector2D CenterPoint;
			FPointSortCompare(const FVector2D& InCenterPoint)
				: CenterPoint(InCenterPoint)
			{

			}

			FORCEINLINE bool operator()(const FVector2D& A, const FVector2D& B) const
			{
				if (A.Y - CenterPoint.X >= 0 && B.X - CenterPoint.X < 0)
					return true;
				if (A.X - CenterPoint.X < 0 && B.X - CenterPoint.X >= 0)
					return false;
				if (A.X - CenterPoint.X == 0 && B.X - CenterPoint.X == 0) {
					if (A.Y - CenterPoint.Y >= 0 || B.Y - CenterPoint.Y >= 0)
						return A.Y > B.Y;
					return B.Y > A.Y;
				}

				// compute the cross product of vectors (center -> a) x (center -> b)
				int det = (A.X - CenterPoint.X) * (B.Y - CenterPoint.Y) - (B.X - CenterPoint.X) * (A.Y - CenterPoint.Y);
				if (det < 0)
					return true;
				if (det > 0)
					return false;

				// points a and b are on the same line from the center
				// check which point is closer to the center
				int d1 = (A.X - CenterPoint.X) * (A.X - CenterPoint.X) + (A.Y - CenterPoint.Y) * (A.Y - CenterPoint.Y);
				int d2 = (B.X - CenterPoint.X) * (B.X - CenterPoint.X) + (B.Y - CenterPoint.Y) * (B.Y - CenterPoint.Y);
				return d1 > d2;
			}
		};

		IntersectionPoints.Sort(FPointSortCompare(PtCenter));

		FCanvasUVTri Tri;
		Tri.V0_Color = UVColor;
		Tri.V1_Color = UVColor;
		Tri.V2_Color = UVColor;

		OutTris.Reserve(IntersectionPoints.Num() - 2);

		// Now that we have our sorted list, we can generate a tri map from it, just doing a Fan from first to last
		for (int i = 1; i < IntersectionPoints.Num() - 1; i++)
		{
			Tri.V0_Pos = IntersectionPoints[0];
			Tri.V1_Pos = IntersectionPoints[i];
			Tri.V2_Pos = IntersectionPoints[i + 1];

			OutTris.Add(Tri);
		}

		return true;
	}

	UFUNCTION()
	void UpdateRelevancyMap()
	{
		AActor* myOwner = GetOwner();

		for (int i = NetRelevancyLog.Num() - 1; i >= 0; i--)
		{
			if (!NetRelevancyLog[i].PC.IsValid() || NetRelevancyLog[i].PC->IsLocalController() || !NetRelevancyLog[i].PC->GetPawn())
			{
				NetRelevancyLog[i].ReplicationProxy->Destroy();
				NetRelevancyLog.RemoveAt(i);
			}
			else
			{
				if (APawn* pawn = NetRelevancyLog[i].PC->GetPawn())
				{
					if (!myOwner->IsNetRelevantFor(NetRelevancyLog[i].PC.Get(), pawn, pawn->GetActorLocation()))
					{
						NetRelevancyLog[i].bIsRelevant = false;
						NetRelevancyLog[i].bIsDirty = false;
						//NetRelevancyLog.RemoveAt(i);
					}
				}
			}
		}

		for (FConstPlayerControllerIterator PCIt = GetWorld()->GetPlayerControllerIterator(); PCIt; ++PCIt)
		{
			if (APlayerController* PC = PCIt->Get())
			{
				if (PC->IsLocalController())
					continue;

				if (APawn* pawn = PC->GetPawn())
				{

					if (myOwner->IsNetRelevantFor(PC, pawn, pawn->GetActorLocation()))
					{
						FClientRepData * RepData = NetRelevancyLog.FindByPredicate([PC](const FClientRepData& Other)
							{
								return Other.PC == PC;
							});

						if (!RepData)
						{
							FClientRepData ClientRepData;
							
							FActorSpawnParameters SpawnParams;
							SpawnParams.Owner = PC;
							FTransform NewTransform = this->GetOwner()->GetActorTransform();
							ARenderTargetReplicationProxy* RenderProxy = GetWorld()->SpawnActor<ARenderTargetReplicationProxy>(ARenderTargetReplicationProxy::StaticClass(), NewTransform, SpawnParams);

							if (RenderProxy)
							{
								RenderProxy->AttachToActor(this->GetOwner(), FAttachmentTransformRules::SnapToTargetIncludingScale);
							

								ClientRepData.PC = PC;
								ClientRepData.ReplicationProxy = RenderProxy;
								ClientRepData.bIsRelevant = true;
								ClientRepData.bIsDirty = true;

								NetRelevancyLog.Add(ClientRepData);
							}
							// Update this client with the new data
						}
						else
						{
							if (!RepData->bIsRelevant)
							{
								RepData->bIsRelevant = true;
								RepData->bIsDirty = true;
							}
						}
					}
				}

			}
		}
	}

	// This function blocks the game thread and WILL be slow
	UFUNCTION(BlueprintCallable, Category = "RenderTargetManager")
	bool CompressRenderTarget2D(TArray<uint8>& OutByteData)
	{
		if (!RenderTarget)
			return false;

		TArray<FColor> SurfData;
		FRenderTarget* RenderTarget2D = RenderTarget->GameThread_GetRenderTargetResource();
		RenderTarget2D->ReadPixels(SurfData);

		TArray<uint16> Test;
		Test.AddUninitialized(SurfData.Num());

		uint16 ColorVal = 0;
		uint32 Counter = 0;

		for (FColor col : SurfData)
		{
			ColorVal = col.R >> 3 | (col.G & 0xFC) << 3 | (col.B & 0xF8) << 8;
			Test[Counter++] = ColorVal;
		}

		FIntPoint Size2D = RenderTarget2D->GetSizeXY();
		int32 Width = Size2D.X;
		int32 Height = Size2D.Y;
		EPixelFormat PixelFormat = RenderTarget->GetFormat();
		uint8 PixelFormat8 = (uint8)PixelFormat;
		int32 Size = 0;
		TArray<uint8> TempData;

		OutByteData.Reset();
		/*FArchiveSaveCompressedProxy Compressor(OutByteData, NAME_Zlib, COMPRESS_BiasSpeed);

		Compressor << Width;
		Compressor << Height;
		Compressor << PixelFormat8;
		Compressor << Test;

		Compressor.Flush();*/
		return true;
	}

	UFUNCTION(BlueprintCallable, Category = "RenderTargetManager")
		bool DeCompressRenderTarget2D()
	{
		if (!RenderTarget)
			return false;

		RenderTargetStore.UnPackData();


		int32 Width = RenderTargetStore.Width;
		int32 Height = RenderTargetStore.Height;
		EPixelFormat PixelFormat = RenderTargetStore.PixelFormat;
		uint8 PixelFormat8 = 0;

		TArray<FColor> TempData;
		TArray<uint16> Test;


		if (RenderTargetStore.bUseColorMap)
		{

		}
		else
		{
			TArray<FColor> FinalColorData;
			FinalColorData.AddUninitialized(RenderTargetStore.UnpackedData.Num());

			uint32 Counter = 0;
			FColor ColorVal;
			for (uint16 CompColor : RenderTargetStore.UnpackedData)
			{
				ColorVal.R = CompColor << 3;
				ColorVal.G = CompColor >> 5 << 2;
				ColorVal.B = CompColor >> 11 << 3;
				ColorVal.A = 0xFF;
				FinalColorData[Counter++] = ColorVal;
			}

			// Write this to a texture2d
			UTexture2D* RenderBase = UTexture2D::CreateTransient(Width, Height, RenderTargetStore.PixelFormat);
			// Switched to a Memcpy instead of byte by byte transer
			uint8* MipData = (uint8*)RenderBase->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(MipData, (void*)FinalColorData.GetData(), Height * Width * 4);
			RenderBase->PlatformData->Mips[0].BulkData.Unlock();

			//Setting some Parameters for the Texture and finally returning it
			RenderBase->PlatformData->SetNumSlices(1);
			RenderBase->NeverStream = true;
			//Avatar->CompressionSettings = TC_EditorIcon;

			RenderBase->UpdateResource();

			// Then canvas it to the render target

		}


		//FColor ColorVal;
		//uint32 Counter = 0;

		//TempData.AddUninitialized(Test.Num());



		return true;
	}


    UFUNCTION(BlueprintCallable, Category = "RenderTargetManager")
    void QueueImageStore() 
    {

        if (!RenderTarget)
        {
            return;
        }

		// Init new RenderRequest
		FRenderDataStore* renderData = new FRenderDataStore();

        // Get RenderContext
        FTextureRenderTargetResource* renderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();

		renderData->Size2D = renderTargetResource->GetSizeXY();
        renderData->PixelFormat = RenderTarget->GetFormat();

        struct FReadSurfaceContext {
            FRenderTarget* SrcRenderTarget;
            TArray<FColor>* OutData;
            FIntRect Rect;
            FReadSurfaceDataFlags Flags;
        };

        // Setup GPU command
        FReadSurfaceContext readSurfaceContext = 
        {
            renderTargetResource,
            &(renderData->ColorData),
            FIntRect(0,0,renderTargetResource->GetSizeXY().X, renderTargetResource->GetSizeXY().Y),
            FReadSurfaceDataFlags(RCM_UNorm, CubeFace_MAX)
        };

		ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)(
			[readSurfaceContext](FRHICommandListImmediate& RHICmdList) {
			RHICmdList.ReadSurfaceData(
				readSurfaceContext.SrcRenderTarget->GetRenderTargetTexture(),
				readSurfaceContext.Rect,
				*readSurfaceContext.OutData,
				readSurfaceContext.Flags
			);
		});

        // Notify new task in RenderQueue
        RenderDataQueue.Enqueue(renderData);

        // Set RenderCommandFence
        renderData->RenderFence.BeginFence();

        this->SetComponentTickEnabled(true);
    }

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override
	{

		// Read pixels once RenderFence is completed
        if (RenderDataQueue.IsEmpty())
        {
            SetComponentTickEnabled(false);
        }
        else
		{
			// Peek the next RenderRequest from queue
			FRenderDataStore* nextRenderData;
            RenderDataQueue.Peek(nextRenderData);

            if (nextRenderData)
            {
                if (nextRenderData->RenderFence.IsFenceComplete())
                {				
					RenderTargetStore.Reset();
					RenderTargetStore.UnpackedData.AddUninitialized(nextRenderData->ColorData.Num());

					uint16 ColorVal = 0;
					uint32 Counter = 0;
					
					// Convert to 16bit color
					for (FColor col : nextRenderData->ColorData)
					{
						ColorVal = (col.R >> 3) << 11 | (col.G >> 2) << 5 | (col.B >> 3);
						RenderTargetStore.UnpackedData[Counter++] = ColorVal;

						if (col.R != 255)
						{
							int gg = 0;
						}

						col.R = ColorVal >> 11 << 3;
						col.G = ColorVal >> 5 << 2;
						col.B = ColorVal << 3;
						col.A = 0xFF;
					}

					RenderTargetStore.PackData();
					FIntPoint Size2D = nextRenderData->Size2D;
					RenderTargetStore.Width = Size2D.X;
					RenderTargetStore.Height = Size2D.Y;
					RenderTargetStore.PixelFormat = nextRenderData->PixelFormat;

#if WITH_PUSH_MODEL
					MARK_PROPERTY_DIRTY_FROM_NAME(UVRRenderTargetManager, RenderTargetStore, this);
#endif


					/*for (uint16 CompColor : Test)
					{
						ColorVal.R = CompColor << 3;
						ColorVal.G = CompColor >> 5 << 2;
						ColorVal.B = CompColor >> 11 << 3;
						ColorVal.A = 0xFF;
						TempData[Counter++] = ColorVal;
					}*/

					/*IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
					TSharedPtr<IImageWrapper> imageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

					imageWrapper->SetRaw(nextRenderData->ColorData.GetData(), nextRenderData->ColorData.GetAllocatedSize(), nextRenderData->Size2D.X, nextRenderData->Size2D.Y, ERGBFormat::RGBA, 8);
					const TArray64<uint8>& ImgData = imageWrapper->GetCompressed(0);
					*/
					// 10x the byte cost of ZLib and color downscaling

					//TArray<uint8> OutByteData;
					
					// Not compressing it here, doing it during serialization now instead
					/*
					FArchiveSaveCompressedProxy Compressor(OutByteData, NAME_Zlib, COMPRESS_BiasSpeed);

					Compressor << Width;
					Compressor << Height;
					Compressor << PixelFormat8;
					Compressor << RLEEncodedValue;

					Compressor.Flush();
					*/

                    // Delete the first element from RenderQueue
                    RenderDataQueue.Pop();
                    delete nextRenderData;

					if (OnRenderTargetFinishedSave.IsBound())
					{
						OnRenderTargetFinishedSave.Broadcast(RenderTargetStore);
					}
                }
            }
		}

    }

	virtual void BeginPlay() override
	{
		Super::BeginPlay();

		InitRenderTarget();

		if(bInitiallyReplicateTexture && GetNetMode() < ENetMode::NM_Client)
			GetWorld()->GetTimerManager().SetTimer(NetRelevancyTimer_Handle, this, &UVRRenderTargetManager::UpdateRelevancyMap, PollRelevancyTime, true);
	}

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override
    {
        Super::EndPlay(EndPlayReason);

		FRenderDataStore* Store = nullptr;
		while (!RenderDataQueue.IsEmpty())
		{
			RenderDataQueue.Dequeue(Store);

			if (Store)
			{
				delete Store;
			}
		}

		if (GetNetMode() < ENetMode::NM_Client)
			GetWorld()->GetTimerManager().ClearTimer(NetRelevancyTimer_Handle);

		if (RenderTarget)
		{
			RenderTarget->ReleaseResource();
			RenderTarget = nullptr;
		}

		for (FClientRepData& RepData : NetRelevancyLog)
		{
			RepData.PC = nullptr;
			RepData.PC.Reset();
			if (RepData.ReplicationProxy.IsValid() && !RepData.ReplicationProxy->IsPendingKill())
			{
				RepData.ReplicationProxy->Destroy();
			}

			RepData.ReplicationProxy.Reset();
		}

    }

protected:
	TQueue<FRenderDataStore*> RenderDataQueue;

};

UVRRenderTargetManager::UVRRenderTargetManager(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	PollRelevancyTime = 0.1f;

	// http://alumni.media.mit.edu/~wad/color/palette.html

	ColorMap.Reserve(16);
	ColorMap.Add(FColor::Black, 0);
	ColorMap.Add(FColor::White, 1);
	ColorMap.Add(FColor(87, 87, 87), 2);		// DK Gray
	ColorMap.Add(FColor(173, 35, 35), 3);		// Red
	ColorMap.Add(FColor(42, 75, 215), 4);		// Blue
	ColorMap.Add(FColor(29, 105, 20), 5);		// Green
	ColorMap.Add(FColor(129, 74, 125), 6);		// Brown
	ColorMap.Add(FColor(129, 38, 192), 7);		// Purple
	ColorMap.Add(FColor(160, 160, 160), 8);		// Light Gray
	ColorMap.Add(FColor(129, 197, 122), 9);		// Light Green
	ColorMap.Add(FColor(157, 175, 255), 10);	// Light Blue
	ColorMap.Add(FColor(41, 208, 208), 11);		// Cyan
	ColorMap.Add(FColor(255, 146, 51), 12);		// Orange
	ColorMap.Add(FColor(255, 238, 51), 13);		// Yellow
	ColorMap.Add(FColor(233, 222, 187), 14);	// Tan
	ColorMap.Add(FColor(255, 205, 243), 15);	// Pink


	RenderTarget = nullptr;
	RenderTargetWidth = 100;
	RenderTargetHeight = 100;
	ClearColor = FColor::White;

	bInitiallyReplicateTexture = false;
}


// Followed by a count of the following voxels
template <typename DataType>
void RLE_Funcs::RLEDecodeLine(TArray<uint8>* LineToDecode, TArray<DataType>* DecodedLine, bool bCompressed)
{
	if (!LineToDecode || !DecodedLine)
		return;

	RLEDecodeLine(LineToDecode->GetData(), LineToDecode->Num(), DecodedLine, bCompressed);
}

// Followed by a count of the following voxels
template <typename DataType>
void RLE_Funcs::RLEDecodeLine(const uint8* LineToDecode, uint32 Num, TArray<DataType>* DecodedLine, bool bCompressed)
{
	if (!bCompressed)
	{
		DecodedLine->Empty(Num / sizeof(DataType));
		DecodedLine->AddUninitialized(Num / sizeof(DataType));
		FMemory::Memcpy(DecodedLine->GetData(), LineToDecode, Num);
		return;
	}

	const uint8* StartLoc = LineToDecode;
	const uint8* EndLoc = StartLoc + Num;
	uint8 incr = sizeof(DataType);

	DataType ValToWrite = *((DataType*)LineToDecode); // This is just to prevent stupid compiler warnings without disabling them

	DecodedLine->Empty();

	uint8 RLE_FLAG;
	uint32 Length32;
	uint32 Length8;
	uint32 Length16;
	int origLoc;

	for (const uint8* loc = StartLoc; loc < EndLoc;)
	{
		RLE_FLAG = *loc >> 4; // Get the RLE flag from the first 4 bits of the first byte

		switch (RLE_FLAG)
		{
		case RLE_Flags::RLE_CompressedByte:
		{
			Length8 = (*loc & ~0xF0) + 1;
			loc++;
			ValToWrite = *((DataType*)loc);
			loc += incr;

			origLoc = DecodedLine->AddUninitialized(Length8);

			for (uint32 i = origLoc; i < origLoc + Length8; i++)
			{
				(*DecodedLine)[i] = ValToWrite;
			}

		}break;
		case RLE_Flags::RLE_CompressedShort:
		{
			Length16 = (((uint16)(*loc & ~0xF0)) << 8 | (*(loc + 1))) + 1;
			loc += 2;
			ValToWrite = *((DataType*)loc);
			loc += incr;

			origLoc = DecodedLine->AddUninitialized(Length16);

			for (uint32 i = origLoc; i < origLoc + Length16; i++)
			{
				(*DecodedLine)[i] = ValToWrite;
			}

		}break;
		case RLE_Flags::RLE_Compressed24:
		{
			Length32 = (((uint32)(*loc & ~0xF0)) << 16 | ((uint32)(*(loc + 1))) << 8 | (uint32)(*(loc + 2))) + 1;
			loc += 3;
			ValToWrite = *((DataType*)loc);
			loc += incr;

			origLoc = DecodedLine->AddUninitialized(Length32);

			for (uint32 i = origLoc; i < origLoc + Length32; i++)
			{
				(*DecodedLine)[i] = ValToWrite;
			}

		}break;

		case RLE_Flags::RLE_NotCompressedByte:
		{
			Length8 = (*loc & ~0xF0) + 1;
			loc++;

			origLoc = DecodedLine->AddUninitialized(Length8);

			for (uint32 i = origLoc; i < origLoc + Length8; i++)
			{
				(*DecodedLine)[i] = *((DataType*)loc);
				loc += incr;
			}

		}break;
		case RLE_Flags::RLE_NotCompressedShort:
		{
			Length16 = (((uint16)(*loc & ~0xF0)) << 8 | (*(loc + 1))) + 1;
			loc += 2;

			origLoc = DecodedLine->AddUninitialized(Length16);

			for (uint32 i = origLoc; i < origLoc + Length16; i++)
			{
				(*DecodedLine)[i] = *((DataType*)loc);
				loc += incr;
			}

		}break;
		case RLE_Flags::RLE_NotCompressed24:
		{
			Length32 = (((uint32)(*loc & ~0xF0)) << 16 | ((uint32)(*(loc + 1))) << 8 | ((uint32)(*(loc + 2)))) + 1;
			loc += 3;

			origLoc = DecodedLine->AddUninitialized(Length32);

			for (uint32 i = origLoc; i < origLoc + Length32; i++)
			{
				(*DecodedLine)[i] = *((DataType*)loc);
				loc += incr;
			}

		}break;

		case RLE_Flags::RLE_ContinueRunByte:
		{
			Length8 = (*loc & ~0xF0) + 1;
			loc++;

			origLoc = DecodedLine->AddUninitialized(Length8);

			for (uint32 i = origLoc; i < origLoc + Length8; i++)
			{
				(*DecodedLine)[i] = ValToWrite;
			}

		}break;
		case RLE_Flags::RLE_ContinueRunShort:
		{
			Length16 = (((uint16)(*loc & ~0xF0)) << 8 | (*(loc + 1))) + 1;
			loc += 2;

			origLoc = DecodedLine->AddUninitialized(Length16);

			for (uint32 i = origLoc; i < origLoc + Length16; i++)
			{
				(*DecodedLine)[i] = ValToWrite;
			}

		}break;
		case RLE_Flags::RLE_ContinueRun24:
		{
			Length32 = (((uint32)(*loc & ~0xF0)) << 16 | ((uint32)(*(loc + 1))) << 8 | (*(loc + 2))) + 1;
			loc += 3;

			origLoc = DecodedLine->AddUninitialized(Length32);

			for (uint32 i = origLoc; i < origLoc + Length32; i++)
			{
				(*DecodedLine)[i] = ValToWrite;
			}

		}break;

		}
	}
}

template <typename DataType>
bool RLE_Funcs::RLEEncodeLine(TArray<DataType>* LineToEncode, TArray<uint8>* EncodedLine)
{
	return RLEEncodeBuffer<DataType>(LineToEncode->GetData(), LineToEncode->Num(), EncodedLine);
}

void RLE_Funcs::RLEWriteContinueFlag(uint32 count, uint8** loc)
{
	if (count <= 16)
	{
		**loc = (((uint8)RLE_Flags::RLE_ContinueRunByte << 4) | ((uint8)count - 1));
		(*loc)++;
	}
	else if (count <= 4096)
	{
		uint16 val = ((((uint16)RLE_Flags::RLE_ContinueRunShort) << 12) | ((uint16)count - 1));
		**loc = val >> 8;
		(*loc)++;
		**loc = (uint8)val;
		(*loc)++;
	}
	else
	{
		uint32 val = ((((uint32)RLE_Flags::RLE_ContinueRun24) << 20) | ((uint32)count - 1));
		**loc = (uint8)(val >> 16);
		(*loc)++;
		**loc = (uint8)(val >> 8);
		(*loc)++;
		**loc = (uint8)val;
		(*loc)++;
	}
}

template <typename DataType>
void RLE_Funcs::RLEWriteRunFlag(uint32 count, uint8** loc, TArray<DataType>& Data, bool bCompressed)
{

	if (count <= 16)
	{
		uint8 val;
		if (bCompressed)
			val = ((((uint8)RLE_Flags::RLE_CompressedByte) << 4) | ((uint8)count - 1));
		else
			val = ((((uint8)RLE_Flags::RLE_NotCompressedByte) << 4) | ((uint8)count - 1));

		**loc = val;
		(*loc)++;
	}
	else if (count <= 4096)
	{
		uint16 val;
		if (bCompressed)
			val = ((((uint16)RLE_Flags::RLE_CompressedShort) << 12) | ((uint16)count - 1));
		else
			val = ((((uint16)RLE_Flags::RLE_NotCompressedShort) << 12) | ((uint16)count - 1));

		**loc = (uint8)(val >> 8);
		(*loc)++;
		**loc = (uint8)val;
		(*loc)++;
	}
	else
	{
		uint32 val;
		if (bCompressed)
			val = ((((uint32)RLE_Flags::RLE_Compressed24) << 20) | ((uint32)count - 1));
		else
			val = ((((uint32)RLE_Flags::RLE_NotCompressed24) << 20) | ((uint32)count - 1));

		**loc = (uint8)(val >> 16);
		(*loc)++;
		**loc = (uint8)(val >> 8);
		(*loc)++;
		**loc = (uint8)(val);
		(*loc)++;
	}

	FMemory::Memcpy(*loc, Data.GetData(), Data.Num() * sizeof(DataType));
	*loc += Data.Num() * sizeof(DataType);
	Data.Empty(256);
}

template <typename DataType>
bool RLE_Funcs::RLEEncodeBuffer(DataType* BufferToEncode, uint32 EncodeLength, TArray<uint8>* EncodedLine)
{
	uint32 OrigNum = EncodeLength;//LineToEncode->Num();
	uint8 incr = sizeof(DataType);
	uint32 MAX_COUNT = 1048576; // Max of 2.5 bytes as 0.5 bytes is used for control flags

	EncodedLine->Empty((OrigNum * sizeof(DataType)) + (OrigNum * (sizeof(short))));
	// Reserve enough memory to account for a perfectly bad situation (original size + 3 bytes per max array value)
	// Remove the remaining later with RemoveAt() and a count
	EncodedLine->AddUninitialized((OrigNum * sizeof(DataType)) + ((OrigNum / MAX_COUNT * 3)));

	DataType* First = BufferToEncode;// LineToEncode->GetData();
	DataType Last;
	uint32 RunCount = 0;

	uint8* loc = EncodedLine->GetData();
	//uint8 * countLoc = NULL;

	bool bInRun = false;
	bool bWroteStart = false;
	bool bContinueRun = false;

	TArray<DataType> TempBuffer;
	TempBuffer.Reserve(256);
	uint32 TempCount = 0;

	Last = *First;
	First++;

	for (uint32 i = 0; i < OrigNum - 1; i++, First++)
	{
		if (Last == *First)
		{
			if (bWroteStart && !bInRun)
			{
				RLE_Funcs::RLEWriteRunFlag(TempCount, &loc, TempBuffer, false);
				bWroteStart = false;
			}

			if (bInRun && /**countLoc*/TempCount < MAX_COUNT)
			{
				TempCount++;

				if (TempCount == MAX_COUNT)
				{
					// Write run byte
					if (bContinueRun)
					{
						RLE_Funcs::RLEWriteContinueFlag(TempCount, &loc);
					}
					else
						RLE_Funcs::RLEWriteRunFlag(TempCount, &loc, TempBuffer, true);

					bContinueRun = true;
					TempCount = 0;
				}
			}
			else
			{
				bInRun = true;
				bWroteStart = false;
				bContinueRun = false;

				TempBuffer.Add(Last);
				TempCount = 1;
			}

			// Begin Run Here
		}
		else if (bInRun)
		{
			bInRun = false;

			if (bContinueRun)
			{
				TempCount++;
				RLE_Funcs::RLEWriteContinueFlag(TempCount, &loc);
			}
			else
			{
				TempCount++;
				RLE_Funcs::RLEWriteRunFlag(TempCount, &loc, TempBuffer, true);
			}

			bContinueRun = false;
		}
		else
		{
			if (bWroteStart && TempCount/**countLoc*/ < MAX_COUNT)
			{
				TempCount++;
				TempBuffer.Add(Last);
			}
			else if (bWroteStart && TempCount == MAX_COUNT)
			{
				RLE_Funcs::RLEWriteRunFlag(TempCount, &loc, TempBuffer, false);

				bWroteStart = true;
				TempBuffer.Add(Last);
				TempCount = 1;
			}
			else
			{
				TempBuffer.Add(Last);
				TempCount = 1;
				//*countLoc = 1;

				bWroteStart = true;
			}
		}

		Last = *First;
	}

	// Finish last num
	if (bInRun)
	{
		if (TempCount <= MAX_COUNT)
		{
			if (TempCount == MAX_COUNT)
			{
				// Write run byte
				RLE_Funcs::RLEWriteRunFlag(TempCount, &loc, TempBuffer, true);
				bContinueRun = true;
			}

			if (bContinueRun)
			{
				TempCount++;
				RLE_Funcs::RLEWriteContinueFlag(TempCount, &loc);
			}
			else
			{
				TempCount++;
				RLE_Funcs::RLEWriteRunFlag(TempCount, &loc, TempBuffer, true);
			}
		}

		// Begin Run Here
	}
	else
	{
		if (bWroteStart && TempCount/**countLoc*/ <= MAX_COUNT)
		{
			if (TempCount/**countLoc*/ == MAX_COUNT)
			{
				// Write run byte
				RLE_Funcs::RLEWriteRunFlag(TempCount, &loc, TempBuffer, false);
				TempCount = 0;
			}

			TempCount++;
			TempBuffer.Add(Last);
			RLE_Funcs::RLEWriteRunFlag(TempCount, &loc, TempBuffer, false);
		}
	}

	// Resize the out array to fit compressed contents
	uint32 Wrote = loc - EncodedLine->GetData();
	EncodedLine->RemoveAt(Wrote, EncodedLine->Num() - Wrote, true);

	// If the compression performed worse than the original file size, throw the results array and use the original instead.
	// This will almost never happen with voxels but can so should be accounted for.

	return true;
	// Skipping non compressed for now, the overhead is so low that it isn't worth supporting since the last revision
	if (Wrote > OrigNum* incr)
	{
		EncodedLine->Empty(OrigNum * incr);
		EncodedLine->AddUninitialized(OrigNum * incr);
		FMemory::Memcpy(EncodedLine->GetData(), BufferToEncode/*LineToEncode->GetData()*/, OrigNum * incr);
		return false; // Return that there was no compression, so the decoder can receive it later
	}
	else
		return true;
}
