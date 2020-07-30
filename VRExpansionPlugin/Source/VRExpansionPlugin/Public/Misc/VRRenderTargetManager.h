#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Serialization/ArchiveLoadCompressedProxy.h"

#include "RenderUtils.h"
#include "GeomTools.h"
#include "DrawDebugHelpers.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/CanvasRenderTarget2D.h"
//#include "Net/Core/PushModel/PushModel.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Canvas.h"
//#include "ImageWrapper/Public/IImageWrapper.h"
//#include "ImageWrapper/Public/IImageWrapperModule.h"

#include "VRRenderTargetManager.generated.h"

class UVRRenderTargetManager;

// #TODO: Dirty rects so don't have to send entire texture?


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
		bool bIsZipped;

	EPixelFormat PixelFormat;

	void Reset()
	{
		PackedData.Reset();
		UnpackedData.Reset();
		Width = 0;
		Height = 0;
		PixelFormat = (EPixelFormat)0;
		bIsZipped = false;
	}

	void PackData();
	void UnPackData();


	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

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

UENUM(BlueprintType)
enum class ERenderManagerOperationType : uint8
{
	Op_LineDraw = 0x00,
	Op_TriDraw = 0x01,
	Op_TexDraw = 0x02
};

template<int32 ScaleFactor, int32 MaxBitsPerComponent>
bool WritePackedVector2D(FVector2D Value, FArchive& Ar)	// Note Value is intended to not be a reference since we are scaling it before serializing!
{
	check(Ar.IsSaving());

	// Scale vector by quant factor first
	Value *= ScaleFactor;
	
	// Nan Check
	if (Value.ContainsNaN())
	{
		logOrEnsureNanError(TEXT("WritePackedVector2D: Value contains NaN, clearing for safety."));
		FVector2D	Dummy(0, 0);
		WritePackedVector2D<ScaleFactor, MaxBitsPerComponent>(Dummy, Ar);
		return false;
	}

	float MinV = -1073741824.0f;
	float MaxV = 1073741760.0f;

	// Some platforms have RoundToInt implementations that essentially reduces the allowed inputs to 2^31.
	const FVector2D ClampedValue = FVector2D(FMath::Clamp(Value.X, MinV, MaxV), FMath::Clamp(Value.Y, MinV, MaxV));
	bool bClamp = ClampedValue != Value;

	// Do basically FVector::SerializeCompressed
	int32 IntX = FMath::RoundToInt(ClampedValue.X);
	int32 IntY = FMath::RoundToInt(ClampedValue.Y);

	uint32 Bits = FMath::Clamp<uint32>(FMath::CeilLogTwo(1 + FMath::Max(FMath::Abs(IntX), FMath::Abs(IntY))), 1, MaxBitsPerComponent) - 1;

	// Serialize how many bits each component will have
	Ar.SerializeInt(Bits, MaxBitsPerComponent);

	int32  Bias = 1 << (Bits + 1);
	uint32 Max = 1 << (Bits + 2);
	uint32 DX = IntX + Bias;
	uint32 DY = IntY + Bias;

	if (DX >= Max) { bClamp = true; DX = static_cast<int32>(DX) > 0 ? Max - 1 : 0; }
	if (DY >= Max) { bClamp = true; DY = static_cast<int32>(DY) > 0 ? Max - 1 : 0; }

	Ar.SerializeInt(DX, Max);
	Ar.SerializeInt(DY, Max);

	return !bClamp;
}

template<uint32 ScaleFactor, int32 MaxBitsPerComponent>
bool ReadPackedVector2D(FVector2D& Value, FArchive& Ar)
{
	uint32 Bits = 0;

	// Serialize how many bits each component will have
	Ar.SerializeInt(Bits, MaxBitsPerComponent);

	int32  Bias = 1 << (Bits + 1);
	uint32 Max = 1 << (Bits + 2);
	uint32 DX = 0;
	uint32 DY = 0;

	Ar.SerializeInt(DX, Max);
	Ar.SerializeInt(DY, Max);


	float fact = (float)ScaleFactor;

	Value.X = (float)(static_cast<int32>(DX) - Bias) / fact;
	Value.Y = (float)(static_cast<int32>(DY) - Bias) / fact;

	return true;
}

USTRUCT()
struct FRenderManagerTri {
	GENERATED_BODY()
public:
	FVector2D P1;
	FVector2D P2;
	FVector2D P3;
};

USTRUCT()
struct FRenderManagerOperation {
	GENERATED_BODY()
public:

	UPROPERTY()
		uint32 OwnerID;

	UPROPERTY()
	ERenderManagerOperationType OperationType;

	UPROPERTY()
	FColor Color;

	UPROPERTY()
	FVector2D P1;

	UPROPERTY()
	FVector2D P2;

	UPROPERTY()
		uint32 Thickness;

	UPROPERTY()
	TArray<FRenderManagerTri> Tris;

	UPROPERTY()
		TSoftObjectPtr<UTexture2D> Texture;

	UPROPERTY()
		TSoftObjectPtr<UMaterial> Material;

	FRenderManagerOperation() {
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		
		Ar.SerializeIntPacked(OwnerID);
		Ar.SerializeBits(&OperationType, 3);

		switch (OperationType)
		{
		case ERenderManagerOperationType::Op_LineDraw:
		{
			Ar << Color;
			Ar.SerializeIntPacked(Thickness);

			if (Ar.IsSaving())
			{
				bOutSuccess &= WritePackedVector2D<1, 20>(P1, Ar);
				bOutSuccess &= WritePackedVector2D<1, 20>(P2, Ar);
			}
			else
			{
				ReadPackedVector2D<1, 20>(P1, Ar);
				ReadPackedVector2D<1, 20>(P2, Ar);
			}
		}break;
		case ERenderManagerOperationType::Op_TexDraw:
		{
			Ar << Texture;
			
			if (Ar.IsSaving())
			{
				bOutSuccess &= WritePackedVector2D<1, 20>(P1, Ar);
			}
			else
			{
				ReadPackedVector2D<1, 20>(P1, Ar);
			}
		}break;
		case ERenderManagerOperationType::Op_TriDraw:
		{
			Ar << Color;
			Ar << Material;

			uint32 ArrayCt = Tris.Num();
			Ar.SerializeIntPacked(ArrayCt);

			if (Ar.IsLoading())
			{
				Tris.Reset(ArrayCt);
				Tris.AddUninitialized(ArrayCt);

				FRenderManagerTri TriTemp;
				for (uint32 i = 0; i < ArrayCt; ++i)
				{
					ReadPackedVector2D<1, 20>(TriTemp.P1, Ar);
					ReadPackedVector2D<1, 20>(TriTemp.P2, Ar);
					ReadPackedVector2D<1, 20>(TriTemp.P3, Ar);
					Tris[i] = TriTemp;
				}
			}
			else
			{
				for (uint32 i = 0; i < ArrayCt; ++i)
				{
					WritePackedVector2D<1, 20>(Tris[i].P1, Ar);
					WritePackedVector2D<1, 20>(Tris[i].P2, Ar);
					WritePackedVector2D<1, 20>(Tris[i].P3, Ar);
				}
			}

		}break;
		}

		return bOutSuccess;
	}
};
template<>
struct TStructOpsTypeTraits< FRenderManagerOperation > : public TStructOpsTypeTraitsBase2<FRenderManagerOperation>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
	};
};

/**
* This class is used as a proxy to send owner only RPCs
*/
UCLASS(ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API ARenderTargetReplicationProxy : public AActor
{
	GENERATED_BODY()

public:
	ARenderTargetReplicationProxy(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(Replicated, ReplicatedUsing = OnRep_Manager)
		TWeakObjectPtr<UVRRenderTargetManager> OwningManager;

	UPROPERTY(Replicated)
		uint32 OwnersID;
	
	UFUNCTION()
		void OnRep_Manager();

	UPROPERTY(Transient)
	FBPVRReplicatedTextureStore TextureStore;
	
	UPROPERTY(Transient)
		int32 BlobNum;

	void SendInitMessage();

	UFUNCTION()
	void SendNextDataBlob();

	FTimerHandle SendTimer_Handle;

	// Maximum size of texture blobs to use for sending (size of chunks that it gets broken down into)
	UPROPERTY()
		int32 TextureBlobSize;

	// Maximum bytes per second to send, you will want to play around with this and the
	// MaxClientRate settings in config in order to balance the bandwidth and avoid saturation
	// If you raise this above the max replication size of a 65k byte size then you will need
	// To adjust the max size in engine network settings.
	UPROPERTY()
		int32 MaxBytesPerSecondRate;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override
	{
		if(SendTimer_Handle.IsValid())
			GetWorld()->GetTimerManager().ClearTimer(SendTimer_Handle);

		Super::EndPlay(EndPlayReason);
	}


	UFUNCTION(Reliable, Server, WithValidation)
		void SendLocalDrawOperations(const TArray<FRenderManagerOperation>& LocalRenderOperationStoreList);

	UFUNCTION(Reliable, Client, WithValidation)
		void InitTextureSend(int32 Width, int32 Height, int32 TotalDataCount, int32 BlobCount, EPixelFormat PixelFormat, bool bIsZipped);

	UFUNCTION(Reliable, Server, WithValidation)
		void Ack_InitTextureSend(int32 TotalDataCount);

	UFUNCTION(Reliable, Client, WithValidation)
		void ReceiveTextureBlob(const TArray<uint8>& TextureBlob, int32 LocationInData, int32 BlobCount);

	UFUNCTION(Reliable, Server, WithValidation)
		void Ack_ReceiveTextureBlob(int32 BlobCount);

	UFUNCTION(Reliable, Client, WithValidation)
		void ReceiveTexture(const FBPVRReplicatedTextureStore&TextureData);

};



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
	
	uint32 OwnerIDCounter;

	UPROPERTY(Transient)
		TWeakObjectPtr<ARenderTargetReplicationProxy> LocalProxy;

	TArray<FRenderManagerOperation> RenderOperationStore;
	TArray<FRenderManagerOperation> LocalRenderOperationStore;

	// Rate to poll for drawing new operations
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		float DrawRate;

	UFUNCTION(Reliable, NetMultiCast, WithValidation)
		void SendDrawOperations(const TArray<FRenderManagerOperation>& RenderOperationStoreList);

	UFUNCTION(BlueprintCallable, Category = "VRRenderTargetManager|DrawingFunctions")
	void AddLineDrawOperation(FVector2D Point1, FVector2D Point2, FColor Color, int32 Thickness)
	{
		FRenderManagerOperation NewOperation;
		NewOperation.OperationType = ERenderManagerOperationType::Op_LineDraw;
		NewOperation.Color = Color;
		NewOperation.P1 = Point1;
		NewOperation.P2 = Point2;
		NewOperation.Thickness = (uint32)Thickness;

		if(GetNetMode() < ENetMode::NM_Client)
			RenderOperationStore.Add(NewOperation);
		else
			LocalRenderOperationStore.Add(NewOperation);

		if (!DrawHandle.IsValid())
			GetWorld()->GetTimerManager().SetTimer(DrawHandle, this, &UVRRenderTargetManager::DrawPoll, DrawRate, true);

		// Send to server now
	}

	UFUNCTION(BlueprintCallable, Category = "VRRenderTargetManager|DrawingFunctions")
		void AddTextureDrawOperation(FVector2D Position, UTexture2D * TextureToDisplay)
	{

		if (!TextureToDisplay)
			return;

		FRenderManagerOperation NewOperation;
		NewOperation.OperationType = ERenderManagerOperationType::Op_TexDraw;
		NewOperation.P1 = Position;
		NewOperation.Texture = TextureToDisplay;

		if (GetNetMode() < ENetMode::NM_Client)
			RenderOperationStore.Add(NewOperation);
		else
			LocalRenderOperationStore.Add(NewOperation);

		if (!DrawHandle.IsValid())
			GetWorld()->GetTimerManager().SetTimer(DrawHandle, this, &UVRRenderTargetManager::DrawPoll, DrawRate, true);

		// Send to server now
	}

	// Adds a draw operation for a triangle list, only takes the first vertex's color
	UFUNCTION(BlueprintCallable, Category = "VRRenderTargetManager|DrawingFunctions")
		void AddMaterialTrianglesDrawOperation(TArray<FCanvasUVTri> Tris, UMaterial * Material)
	{

		if (!Tris.Num())
			return;

		FRenderManagerOperation NewOperation;
		NewOperation.OperationType = ERenderManagerOperationType::Op_TriDraw;
		NewOperation.Color = Tris[0].V0_Color.ToFColor(true);

		NewOperation.Tris.AddUninitialized(Tris.Num());
		int Counter = 0;
		FRenderManagerTri RenderTri;
		for (FCanvasUVTri Tri : Tris)
		{
			RenderTri.P1 = Tri.V0_Pos;
			RenderTri.P2 = Tri.V1_Pos;
			RenderTri.P3 = Tri.V2_Pos;
			NewOperation.Tris[Counter++] = RenderTri;
		}

		NewOperation.Material = Material;

		if (GetNetMode() < ENetMode::NM_Client)
			RenderOperationStore.Add(NewOperation);
		else
			LocalRenderOperationStore.Add(NewOperation);

		if (!DrawHandle.IsValid())
			GetWorld()->GetTimerManager().SetTimer(DrawHandle, this, &UVRRenderTargetManager::DrawPoll, DrawRate, true);

		// Send to server now
	}

	void DrawOperation(UCanvas* Canvas, const FRenderManagerOperation & Operation)
	{		
		if (LocalProxy.IsValid() && LocalProxy->OwnersID == Operation.OwnerID)
		{
			return;
		}

		switch (Operation.OperationType)
		{
		case ERenderManagerOperationType::Op_LineDraw:
		{
			FCanvasLineItem LineItem;
			LineItem.Origin = FVector(Operation.P1.X, Operation.P1.Y, 0.f);
			LineItem.EndPos = FVector(Operation.P2.X, Operation.P2.Y, 0.f);
			LineItem.LineThickness = (float)Operation.Thickness;
			LineItem.SetColor(Operation.Color.ReinterpretAsLinear());
			Canvas->DrawItem(LineItem);
		}break;
		case ERenderManagerOperationType::Op_TexDraw:
		{
			if (Operation.Texture && Operation.Texture->Resource)
			{
				//FTexture* RenderTextureResource = (RenderBase) ? RenderBase->Resource : GWhiteTexture;
				FCanvasTileItem TileItem(Operation.P1, Operation.Texture->Resource, FVector2D(Operation.Texture->GetSizeX(), Operation.Texture->GetSizeY()), FVector2D(0, 0), FVector2D(1.f, 1.f), ClearColor);
				TileItem.BlendMode = FCanvas::BlendToSimpleElementBlend(EBlendMode::BLEND_Translucent);
				Canvas->DrawItem(TileItem);
			}
		}break;
		case ERenderManagerOperationType::Op_TriDraw:
		{
			if (Operation.Tris.Num() && Operation.Material)
			{
				FCanvasTriangleItem TriangleItem(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector, NULL);
				TriangleItem.MaterialRenderProxy = Operation.Material->GetRenderProxy();

				FCanvasUVTri triStore;
				triStore.V0_Color = Operation.Color;
				triStore.V1_Color = Operation.Color;
				triStore.V2_Color = Operation.Color;

				TriangleItem.TriangleList.Reset(Operation.Tris.Num());
				TriangleItem.TriangleList.AddUninitialized(Operation.Tris.Num());
				uint32 Counter = 0;
				for (FRenderManagerTri Tri : Operation.Tris)
				{
					triStore.V0_Pos = Tri.P1;
					triStore.V1_Pos = Tri.P2;
					triStore.V2_Pos = Tri.P3;
					TriangleItem.TriangleList[Counter++] = triStore;
				}

				Canvas->DrawItem(TriangleItem);
			}
		}break;
		}

	}

	UFUNCTION()
		void DrawPoll()
	{
		if (!RenderOperationStore.Num() && !LocalRenderOperationStore.Num())
		{
			GetWorld()->GetTimerManager().ClearTimer(DrawHandle);
			return;
		}

		if (GetNetMode() < ENetMode::NM_Client)
		{
			SendDrawOperations(RenderOperationStore);
		}
		else
		{
			if (LocalRenderOperationStore.Num())
			{
				// Send operations to server
				if (LocalProxy.IsValid())
				{
					LocalProxy->SendLocalDrawOperations(LocalRenderOperationStore);
				}

				RenderOperationStore.Append(LocalRenderOperationStore);
				LocalRenderOperationStore.Empty();
			}
			
			DrawOperations();
		}
	}

	void DrawOperations()
	{

		if (bIsLoadingTextureBuffer)
		{
			if(!DrawHandle.IsValid())
				GetWorld()->GetTimerManager().SetTimer(DrawHandle, this, &UVRRenderTargetManager::DrawPoll, DrawRate, true);

			return;
		}

		UWorld* World = GetWorld();

		// Reference to the Render Target resource
		FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();

		// Retrieve a UCanvas form the world to avoid creating a new one each time
		UCanvas* CanvasToUse = World->GetCanvasForDrawMaterialToRenderTarget();

		// Creates a new FCanvas for rendering
		FCanvas RenderCanvas(
			RenderTargetResource,
			nullptr,
			World,
			World->FeatureLevel);

		// Setup the canvas with the FCanvas reference
		CanvasToUse->Init(RenderTarget->SizeX, RenderTarget->SizeY, nullptr, &RenderCanvas);
		CanvasToUse->Update();

		if (CanvasToUse)
		{
			for (const FRenderManagerOperation& opt : RenderOperationStore)
			{
				DrawOperation(CanvasToUse, opt);
			}

			RenderOperationStore.Empty();

			// Perform the drawing
			RenderCanvas.Flush_GameThread();

			// Cleanup the FCanvas reference, to delete it
			CanvasToUse->Canvas = NULL;
		}
	}

	UPROPERTY()
		FTimerHandle DrawHandle;

	UPROPERTY(Transient)
		bool bIsStoringImage;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		bool bInitiallyReplicateTexture;

	UPROPERTY(Transient)
		bool bIsLoadingTextureBuffer;

	// Maximum size of texture blobs to use for sending (size of chunks that it gets broken down into)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		int32 TextureBlobSize;

	// Maximum bytes per second to send, you will want to play around with this and the
	// MaxClientRate settings in config in order to balance the bandwidth and avoid saturation
	// If you raise this above the max replication size of a 65k byte size then you will need
	// To adjust the max size in engine network settings.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		int32 MaxBytesPerSecondRate;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "RenderTargetManager")
		UCanvasRenderTarget2D* RenderTarget;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		int32 RenderTargetWidth;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		int32 RenderTargetHeight;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		FColor ClearColor;

	UPROPERTY(Transient)
		TArray<FClientRepData> NetRelevancyLog;

	// Rate to poll for actor relevancy
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "RenderTargetManager")
		float PollRelevancyTime;

	FTimerHandle NetRelevancyTimer_Handle;

	UPROPERTY(Transient)
		FBPVRReplicatedTextureStore RenderTargetStore;

	UFUNCTION(BlueprintCallable, Category = "VRRenderTargetManager|UtilityFunctions")
		bool GenerateTrisFromBoxPlaneIntersection(UPrimitiveComponent* PrimToBoxCheck, FTransform WorldTransformOfPlane, const FPlane& LocalProjectionPlane, FVector2D PlaneSize, FColor UVColor, TArray<FCanvasUVTri>& OutTris);
	
	// Create the render target that we are managing
	void InitRenderTarget();

	// Update the list of players that we are checking for relevancy
	void UpdateRelevancyMap();

	// Decompress the render target data to a texture and copy it to our managed render target
	bool DeCompressRenderTarget2D();

	// Queues storing the render target image to our buffer
	void QueueImageStore();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	TQueue<FRenderDataStore*> RenderDataQueue;

};
