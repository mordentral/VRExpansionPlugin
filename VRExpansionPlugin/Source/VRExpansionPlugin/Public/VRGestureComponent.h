

#pragma once

#include "CoreMinimal.h"
//#include "Engine/Engine.h"
#include "VRBPDatatypes.h"
#include "Engine/DataAsset.h"
#include "Components/SceneComponent.h"

//#include "Engine/EngineTypes.h"
//#include "Engine/EngineBaseTypes.h"
#include "TimerManager.h"
#include "VRGestureComponent.generated.h"

DECLARE_STATS_GROUP(TEXT("TICKGesture"), STATGROUP_TickGesture, STATCAT_Advanced);

class USplineMeshComponent;
class USplineComponent;
class AVRBaseCharacter;


UENUM(Blueprintable)
enum class EVRGestureState : uint8
{
	GES_None,
	GES_Recording,
	GES_Detecting
};


UENUM(Blueprintable)
enum class EVRGestureMirrorMode : uint8
{
	GES_NoMirror,
	GES_MirrorLeft,
	GES_MirrorRight,
	GES_MirrorBoth
};

UENUM(Blueprintable)
enum class EVRGestureFlattenAxis : uint8
{
	GES_FlattenX,
	GES_FlattenY,
	GES_FlattenZ,
	GES_DontFlatten
};

USTRUCT(BlueprintType, Category = "VRGestures")
struct VREXPANSIONPLUGIN_API FVRGestureSettings
{
	GENERATED_BODY()
public:

	// Minimum length to start recognizing this gesture at
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture|Advanced")
		int Minimum_Gesture_Length;

	// Maximum distance between the last observations before throwing out this gesture, raise this to make it easier to start checking this gesture
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture|Advanced")
		float firstThreshold;

	// Full threshold before detecting the gesture, raise this to lower accuracy but make it easier to detect this gesture
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture|Advanced")
		float FullThreshold;

	// If set to left/right, will mirror the detected gesture if the gesture component is set to match that value
	// If set to Both mode, the gesture will be checked both normal and mirrored and the best match will be chosen
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture|Advanced")
		EVRGestureMirrorMode MirrorMode;

	// If enabled this gesture will be checked when inside a DB
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture|Advanced")
		bool bEnabled;

	// If enabled this gesture will have sample data scaled to it when recognizing (if false you will want to record the gesture without scaling)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture|Advanced")
		bool bEnableScaling;

	FVRGestureSettings()
	{
		Minimum_Gesture_Length = 1;
		firstThreshold = 20.0f;
		FullThreshold = 20.0f;
		MirrorMode = EVRGestureMirrorMode::GES_NoMirror;
		bEnabled = true;
		bEnableScaling = true;
	}
};

USTRUCT(BlueprintType, Category = "VRGestures")
struct VREXPANSIONPLUGIN_API FVRGesture
{
	GENERATED_BODY()
public:

	// Name of the recorded gesture
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture")
	FString Name;

	// Enum uint8 for end user use
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture")
	uint8 GestureType;

	// Samples in the recorded gesture
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "VRGesture")
	TArray<FVector> Samples;

	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = "VRGesture")
	FBox GestureSize;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture")
		FVRGestureSettings GestureSettings;

	FVRGesture()
	{
		GestureType = 0;
		GestureSize = FBox();
	}

	void CalculateSizeOfGesture(bool bAllowResizing = false, float TargetExtentSize = 1.f)
	{
		FVector NewSample;
		for (int i = 0; i < Samples.Num(); ++i)
		{
			NewSample = Samples[i];
			GestureSize.Max.X = FMath::Max(NewSample.X, GestureSize.Max.X);
			GestureSize.Max.Y = FMath::Max(NewSample.Y, GestureSize.Max.Y);
			GestureSize.Max.Z = FMath::Max(NewSample.Z, GestureSize.Max.Z);

			GestureSize.Min.X = FMath::Min(NewSample.X, GestureSize.Min.X);
			GestureSize.Min.Y = FMath::Min(NewSample.Y, GestureSize.Min.Y);
			GestureSize.Min.Z = FMath::Min(NewSample.Z, GestureSize.Min.Z);
		}

		if (bAllowResizing)
		{
			FVector BoxSize = GestureSize.GetSize();
			float Scaler = TargetExtentSize / BoxSize.GetMax();

			for (int i = 0; i < Samples.Num(); ++i)
			{
				Samples[i] *= Scaler;
			}

			GestureSize.Min *= Scaler;
			GestureSize.Max *= Scaler;
		}
	}
};

/**
* Items Database DataAsset, here we can save all of our game items
*/
UCLASS(BlueprintType, Category = "VRGestures")
class VREXPANSIONPLUGIN_API UGesturesDatabase : public UDataAsset
{
	GENERATED_BODY()
public:

	// Gestures in this database
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
	TArray <FVRGesture> Gestures;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		float TargetGestureScale;

	UGesturesDatabase()
	{
		TargetGestureScale = 100.0f;
	}

	// Recalculate size of gestures and re-scale them to the TargetGestureScale (if bScaleToDatabase is true)
	UFUNCTION(BlueprintCallable, Category = "VRGestures")
		void RecalculateGestures(bool bScaleToDatabase = true);

	// Fills a spline component with a gesture, optionally also generates spline mesh components for it (uses ones already attached if possible)
	UFUNCTION(BlueprintCallable, Category = "VRGestures")
		void FillSplineWithGesture(UPARAM(ref)FVRGesture &Gesture, USplineComponent * SplineComponent, bool bCenterPointsOnSpline = true, bool bScaleToBounds = false, float OptionalBounds = 0.0f, bool bUseCurvedPoints = true, bool bFillInSplineMeshComponents = true, UStaticMesh * Mesh = nullptr, UMaterial * MeshMat = nullptr);

	// Imports a spline as a gesture, Segment len is the max segment length (will break lines up into lengths of this size)
	UFUNCTION(BlueprintCallable, Category = "VRGestures")
		bool ImportSplineAsGesture(USplineComponent * HostSplineComponent, FString GestureName, bool bKeepSplineCurves = true, float SegmentLen = 10.0f, bool bScaleToDatabase = true);

};


USTRUCT(BlueprintType, Category = "VRGestures")
struct VREXPANSIONPLUGIN_API FVRGestureSplineDraw
{
	GENERATED_BODY()
public:

	UPROPERTY()
		TObjectPtr<USplineComponent> SplineComponent;

	UPROPERTY()
	TArray<TObjectPtr<USplineMeshComponent>> SplineMeshes;

	int LastIndexSet;
	int NextIndexCleared;

	// Marches through the array and clears the last point
	void ClearLastPoint();

	// Hides all spline meshes and re-inits the spline component
	void Reset();

	void Clear();

	FVRGestureSplineDraw();

	~FVRGestureSplineDraw();
};

/** Delegate for notification when the lever state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FVRGestureDetectedSignature, uint8, GestureType, FString, DetectedGestureName, int, DetectedGestureIndex, UGesturesDatabase *, GestureDataBase, FVector, OriginalUnscaledGestureSize);

/**
* A scene component that can sample its positions to record / track VR gestures
* Core code is from https://social.msdn.microsoft.com/Forums/en-US/4a428391-82df-445a-a867-557f284bd4b1/dynamic-time-warping-to-recognize-gestures?forum=kinectsdk
* I would also like to acknowledge RuneBerg as he appears to have used the same core codebase and I discovered that halfway through implementing this
* If this algorithm should not prove stable enough I will likely look into using a more complex and faster one in the future, I have several modifications
* to the base DTW algorithm noted from a few research papers. I only implemented this one first as it was a single header file and the quickest to implement.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRGestureComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UVRGestureComponent(const FObjectInitializer& ObjectInitializer);


	// Size of obeservations vectors.
	//int dim; // Not needed, this is just dimensionality
	// Can be used for arrays of samples (IE: multiple points), could add back in eventually
	// if I decide to support three point tracked gestures or something at some point, but its a waste for single point.

	UFUNCTION(BlueprintImplementableEvent, Category = "BaseVRCharacter")
		void OnGestureDetected(uint8 GestureType, FString &DetectedGestureName, int & DetectedGestureIndex, UGesturesDatabase * GestureDatabase, FVector OriginalUnscaledGestureSize);

	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "VRGestures")
		FVRGestureDetectedSignature OnGestureDetected_Bind;

	// Known sequences
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		TObjectPtr<UGesturesDatabase> GesturesDB;

	// Tolerance within we throw out duplicate samples
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		float SameSampleTolerance;

	// If a gesture is set to match this value then detection will mirror the gesture
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		EVRGestureMirrorMode MirroringHand;

	// Tolerance within we throw out duplicate samples
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		TObjectPtr<AVRBaseCharacter> TargetCharacter;

	FVRGestureSplineDraw RecordingGestureDraw;

	// Should we draw splines curved or straight
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		bool bDrawSplinesCurved;

	// If false will get the gesture in relative space instead
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		bool bGetGestureInWorldSpace;

	// Mesh to use when drawing splines
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		TObjectPtr<UStaticMesh> SplineMesh;

	// Scaler to apply to the spline mesh components
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		FVector2D SplineMeshScaler;

	// Material to use when drawing splines
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		TObjectPtr<UMaterialInterface> SplineMaterial;

	// HTZ to run recording at for detection and saving - now being used as a frame time instead of a HTZ
	float RecordingDelta;

	// Number of samples to keep in memory during detection
	int RecordingBufferSize;

	float RecordingClampingTolerance = 0.0f;
	EVRGestureFlattenAxis RecordingFlattenAxis = EVRGestureFlattenAxis::GES_DontFlatten;
	bool bDrawRecordingGesture = false;
	bool bDrawRecordingGestureAsSpline = false;
	bool bGestureChanged = false;

	// Handle to our update timer
	FTimerHandle TickGestureTimer_Handle;

	// Maximum vertical or horizontal steps in a row in the lookup table before throwing out a gesture
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
	int maxSlope;

	UPROPERTY(BlueprintReadOnly, Category = "VRGestures")
	EVRGestureState CurrentState;

	// Currently recording gesture
	UPROPERTY(BlueprintReadOnly, Category = "VRGestures")
	FVRGesture GestureLog;

	inline float GetGestureDistance(FVector Seq1, FVector Seq2, bool bMirrorGesture = false)
	{
		if (bMirrorGesture)
		{
			return FVector::DistSquared(Seq1, FVector(Seq2.X, -Seq2.Y, Seq2.Z));
		}

		return FVector::DistSquared(Seq1, Seq2);
	}

	virtual void BeginDestroy() override;

	// Recalculates a gestures size and re-scales it to the given database
	UFUNCTION(BlueprintCallable, Category = "VRGestures")
		void RecalculateGestureSize(UPARAM(ref) FVRGesture & InputGesture, UGesturesDatabase * GestureDB);

	// Draw a gesture with a debug line batch
	UFUNCTION(BlueprintCallable, Category = "VRGestures", meta = (WorldContext = "WorldContextObject"))
		void DrawDebugGesture(UObject* WorldContextObject, UPARAM(ref)FTransform& StartTransform, FVRGesture GestureToDraw, FColor const& Color, bool bPersistentLines = false, uint8 DepthPriority = 0, float LifeTime = -1.f, float Thickness = 0.f);

	FVector StartVector;
	FTransform OriginatingTransform;
	FTransform ParentRelativeTransform;

	/* Function to begin recording a gesture for detection or saving
	*
	* bRunDetection: Should we detect gestures or only record them
	* bFlattenGestue: Should we flatten the gesture into 2 dimensions (more stable detection and recording, less pretty visually)
	* bDrawGesture: Should we draw the gesture during recording of it
	* bDrawAsSpline: If true we will use spline meshes, if false we will draw as debug lines
	* SamplingHTZ: How many times a second we will record a gesture point, recording is done with a timer now, i would steer away 
	* from htz > possible frames as that could cause double timer updates with how timers are implemented.
	* SampleBufferSize: How many points we will store in history at a time
	* ClampingTolerance: If larger than 0.0, we will clamp points to a grid of this size
	*/
	UFUNCTION(BlueprintCallable, Category = "VRGestures")
		void BeginRecording(bool bRunDetection, EVRGestureFlattenAxis FlattenAxis = EVRGestureFlattenAxis::GES_FlattenX, bool bDrawGesture = true, bool bDrawAsSpline = false, int SamplingHTZ = 30, int SampleBufferSize = 60, float ClampingTolerance = 0.01f);

	// Ends recording and returns the recorded gesture
	UFUNCTION(BlueprintCallable, Category = "VRGestures")
		FVRGesture EndRecording();

	// Clears the current recording
	UFUNCTION(BlueprintCallable, Category = "VRGestures")
		void ClearRecording();

	// Saves a VRGesture to the database, if Scale To Database is true then it will scale the data
	UFUNCTION(BlueprintCallable, Category = "VRGestures")
		void SaveRecording(UPARAM(ref) FVRGesture &Recording, FString RecordingName, bool bScaleRecordingToDatabase = true);

	void CaptureGestureFrame();

	// Ticks the logic from the gameplay timer.
	void TickGesture();


	// Recognize gesture in the given sequence.
	// It will always assume that the gesture ends on the last observation of that sequence.
	// If the distance between the last observations of each sequence is too great, or if the overall DTW distance between the two sequences is too great, no gesture will be recognized.
	void RecognizeGesture(FVRGesture inputGesture);


	// Compute the min DTW distance between seq2 and all possible endings of seq1.
	float dtw(FVRGesture seq1, FVRGesture seq2, bool bMirrorGesture = false, float Scaler = 1.f);

};

