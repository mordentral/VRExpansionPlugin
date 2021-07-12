#include "VRGestureComponent.h"
#include "TimerManager.h"

DECLARE_CYCLE_STAT(TEXT("TickGesture ~ TickingGesture"), STAT_TickGesture, STATGROUP_TickGesture);

UVRGestureComponent::UVRGestureComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	//PrimaryComponentTick.bStartWithTickEnabled = false;
	//PrimaryComponentTick.TickGroup = TG_PrePhysics;
	//PrimaryComponentTick.bTickEvenWhenPaused = false;

	maxSlope = 3;// INT_MAX;
	//globalThreshold = 10.0f;
	SameSampleTolerance = 0.1f;
	bGestureChanged = false;
	MirroringHand = EVRGestureMirrorMode::GES_NoMirror;
	bDrawSplinesCurved = true;
	bGetGestureInWorldSpace = true;
	SplineMeshScaler = FVector2D(1.f);
}

void UGesturesDatabase::FillSplineWithGesture(FVRGesture &Gesture, USplineComponent * SplineComponent, bool bCenterPointsOnSpline, bool bScaleToBounds, float OptionalBounds, bool bUseCurvedPoints, bool bFillInSplineMeshComponents, UStaticMesh * Mesh, UMaterial * MeshMat)
{
	if (!SplineComponent || Gesture.Samples.Num() < 2)
		return;

	UWorld* InWorld = GEngine->GetWorldFromContextObject(SplineComponent, EGetWorldErrorMode::LogAndReturnNull);

	if (!InWorld)
		return;

	SplineComponent->ClearSplinePoints(false);

	FVector PointOffset = FVector::ZeroVector;
	float Scaler = 1.0f;
	if (bScaleToBounds && OptionalBounds > 0.0f)
	{
		Scaler = OptionalBounds / Gesture.GestureSize.GetSize().GetMax();
	}

	if (bCenterPointsOnSpline)
	{
		PointOffset = -Gesture.GestureSize.GetCenter();
	}

	int curIndex = 0;
	for (int i = Gesture.Samples.Num() - 1; i >= 0; --i)
	{
		SplineComponent->AddSplinePoint((Gesture.Samples[i] + PointOffset) * Scaler, ESplineCoordinateSpace::Local, false);
		curIndex++;
		SplineComponent->SetSplinePointType(curIndex, bUseCurvedPoints ? ESplinePointType::Curve : ESplinePointType::Linear, false);
	}

	// Update spline now
	SplineComponent->UpdateSpline();

	if (bFillInSplineMeshComponents && Mesh != nullptr && MeshMat != nullptr)
	{
		TArray<USplineMeshComponent *> CurrentSplineChildren;
		
		TArray<USceneComponent*> Children;
		SplineComponent->GetChildrenComponents(false, Children);
		for (auto Child : Children)
		{
			USplineMeshComponent* SplineMesh = Cast<USplineMeshComponent>(Child);
			if (SplineMesh != nullptr && !SplineMesh->IsPendingKill())
			{
				CurrentSplineChildren.Add(SplineMesh);
			}
		}

		if (CurrentSplineChildren.Num() > SplineComponent->GetNumberOfSplinePoints() - 1)
		{
			int diff = CurrentSplineChildren.Num() - (CurrentSplineChildren.Num() - (SplineComponent->GetNumberOfSplinePoints() -1));

			for (int i = CurrentSplineChildren.Num()- 1; i >= diff; --i)
			{
				if (!CurrentSplineChildren[i]->IsBeingDestroyed())
				{
					CurrentSplineChildren[i]->SetVisibility(false);
					CurrentSplineChildren[i]->Modify();
					CurrentSplineChildren[i]->DestroyComponent();
					CurrentSplineChildren.RemoveAt(i);
				}
			}
		}
		else
		{
			for (int i = CurrentSplineChildren.Num(); i < SplineComponent->GetNumberOfSplinePoints() -1; ++i)
			{
				USplineMeshComponent * newSplineMesh = NewObject<USplineMeshComponent>(SplineComponent);

				newSplineMesh->RegisterComponentWithWorld(InWorld);
				newSplineMesh->SetMobility(EComponentMobility::Movable);
				CurrentSplineChildren.Add(newSplineMesh);
				newSplineMesh->SetStaticMesh(Mesh);
				newSplineMesh->SetMaterial(0, (UMaterialInterface*)MeshMat);

				newSplineMesh->AttachToComponent(SplineComponent, FAttachmentTransformRules::SnapToTargetIncludingScale);
				newSplineMesh->SetVisibility(true);
			}
		}


		for(int i=0; i<SplineComponent->GetNumberOfSplinePoints() - 1; i++)
		{
			CurrentSplineChildren[i]->SetStartAndEnd(SplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local),
				SplineComponent->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::Local),
				SplineComponent->GetLocationAtSplinePoint(i + 1, ESplineCoordinateSpace::Local),
				SplineComponent->GetTangentAtSplinePoint(i + 1, ESplineCoordinateSpace::Local),
				true);
		}
	}

}

void UVRGestureComponent::BeginRecording(bool bRunDetection, bool bFlattenGesture, bool bDrawGesture, bool bDrawAsSpline, int SamplingHTZ, int SampleBufferSize, float ClampingTolerance)
{
	RecordingBufferSize = SampleBufferSize;
	RecordingDelta = 1.0f / SamplingHTZ;
	RecordingClampingTolerance = ClampingTolerance;
	bDrawRecordingGesture = bDrawGesture;
	bDrawRecordingGestureAsSpline = bDrawAsSpline;
	bRecordingFlattenGesture = bFlattenGesture;
	GestureLog.GestureSize.Init();

	// Reinit the drawing spline
	if (!bDrawAsSpline || !bDrawGesture)
		RecordingGestureDraw.Clear(); // Not drawing or not as a spline, remove the components if they exist
	else
	{
		RecordingGestureDraw.Reset(); // Otherwise just clear points and hide mesh components

		if (RecordingGestureDraw.SplineComponent == nullptr)
		{
			RecordingGestureDraw.SplineComponent = NewObject<USplineComponent>(GetAttachParent());
			RecordingGestureDraw.SplineComponent->RegisterComponentWithWorld(GetWorld());
			RecordingGestureDraw.SplineComponent->SetMobility(EComponentMobility::Movable);
			RecordingGestureDraw.SplineComponent->AttachToComponent(GetAttachParent(), FAttachmentTransformRules::KeepRelativeTransform);
			RecordingGestureDraw.SplineComponent->ClearSplinePoints(true);
		}
	}

	// Reset does the reserve already
	GestureLog.Samples.Reset(RecordingBufferSize);

	CurrentState = bRunDetection ? EVRGestureState::GES_Detecting : EVRGestureState::GES_Recording;

	if (TargetCharacter != nullptr)
	{
		OriginatingTransform = TargetCharacter->OffsetComponentToWorld;
	}
	else if (AVRBaseCharacter * own = Cast<AVRBaseCharacter>(GetOwner()))
	{
		TargetCharacter = own;
		OriginatingTransform = TargetCharacter->OffsetComponentToWorld;
	}
	else
		OriginatingTransform = this->GetComponentTransform();

	StartVector = OriginatingTransform.InverseTransformPosition(this->GetComponentLocation());
	this->SetComponentTickEnabled(true);

	if (!TickGestureTimer_Handle.IsValid())
		GetWorld()->GetTimerManager().SetTimer(TickGestureTimer_Handle, this, &UVRGestureComponent::TickGesture, RecordingDelta, true);
}

void UVRGestureComponent::CaptureGestureFrame()
{
	FVector NewSample = OriginatingTransform.InverseTransformPosition(this->GetComponentLocation()) - StartVector;

	if (bRecordingFlattenGesture)
		NewSample.X = 0;

	if (RecordingClampingTolerance > 0.0f)
	{
		NewSample.X = FMath::GridSnap(NewSample.X, RecordingClampingTolerance);
		NewSample.Y = FMath::GridSnap(NewSample.Y, RecordingClampingTolerance);
		NewSample.Z = FMath::GridSnap(NewSample.Z, RecordingClampingTolerance);
	}

	// Add in newest sample at beginning (reverse order)
	if (NewSample != FVector::ZeroVector && (GestureLog.Samples.Num() < 1 || !GestureLog.Samples[0].Equals(NewSample, SameSampleTolerance)))
	{
		bool bClearLatestSpline = false;
		// Pop off oldest sample
		if (GestureLog.Samples.Num() >= RecordingBufferSize)
		{
			GestureLog.Samples.Pop(false);
			bClearLatestSpline = true;
		}
		
		GestureLog.GestureSize.Max.X = FMath::Max(NewSample.X, GestureLog.GestureSize.Max.X);
		GestureLog.GestureSize.Max.Y = FMath::Max(NewSample.Y, GestureLog.GestureSize.Max.Y);
		GestureLog.GestureSize.Max.Z = FMath::Max(NewSample.Z, GestureLog.GestureSize.Max.Z);

		GestureLog.GestureSize.Min.X = FMath::Min(NewSample.X, GestureLog.GestureSize.Min.X);
		GestureLog.GestureSize.Min.Y = FMath::Min(NewSample.Y, GestureLog.GestureSize.Min.Y);
		GestureLog.GestureSize.Min.Z = FMath::Min(NewSample.Z, GestureLog.GestureSize.Min.Z);


		if (bDrawRecordingGesture && bDrawRecordingGestureAsSpline && SplineMesh != nullptr && SplineMaterial != nullptr)
		{

			if (bClearLatestSpline)
				RecordingGestureDraw.ClearLastPoint();

			RecordingGestureDraw.SplineComponent->AddSplinePoint(NewSample, ESplineCoordinateSpace::Local, false);
			int SplineIndex = RecordingGestureDraw.SplineComponent->GetNumberOfSplinePoints() - 1;
			RecordingGestureDraw.SplineComponent->SetSplinePointType(SplineIndex, bDrawSplinesCurved ? ESplinePointType::Curve : ESplinePointType::Linear, true);

			bool bFoundEmptyMesh = false;
			USplineMeshComponent * MeshComp = nullptr;
			int MeshIndex = 0;

			for (int i = 0; i < RecordingGestureDraw.SplineMeshes.Num(); i++)
			{
				MeshIndex = i;
				MeshComp = RecordingGestureDraw.SplineMeshes[i];
				if (MeshComp == nullptr)
				{
					RecordingGestureDraw.SplineMeshes[i] = NewObject<USplineMeshComponent>(RecordingGestureDraw.SplineComponent);
					MeshComp = RecordingGestureDraw.SplineMeshes[i];

					MeshComp->RegisterComponentWithWorld(GetWorld());
					MeshComp->SetMobility(EComponentMobility::Movable);
					MeshComp->SetStaticMesh(SplineMesh);
					MeshComp->SetMaterial(0, (UMaterialInterface*)SplineMaterial);
					bFoundEmptyMesh = true;
					break;
				}
				else if (!MeshComp->IsVisible())
				{
					bFoundEmptyMesh = true;
					break;
				}
			}

			if (!bFoundEmptyMesh)
			{
				USplineMeshComponent * newSplineMesh = NewObject<USplineMeshComponent>(RecordingGestureDraw.SplineComponent);
				MeshComp = newSplineMesh;
				MeshComp->RegisterComponentWithWorld(GetWorld());
				MeshComp->SetMobility(EComponentMobility::Movable);
				RecordingGestureDraw.SplineMeshes.Add(MeshComp);
				MeshIndex = RecordingGestureDraw.SplineMeshes.Num() - 1;
				MeshComp->SetStaticMesh(SplineMesh);
				MeshComp->SetMaterial(0, (UMaterialInterface*)SplineMaterial);
				if (!bGetGestureInWorldSpace && TargetCharacter)
					MeshComp->AttachToComponent(TargetCharacter->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			}

			if (MeshComp != nullptr)
			{
				// Fill in last mesh component tangent and end pos
				if (RecordingGestureDraw.LastIndexSet != MeshIndex && RecordingGestureDraw.SplineMeshes[RecordingGestureDraw.LastIndexSet] != nullptr)
				{
					RecordingGestureDraw.SplineMeshes[RecordingGestureDraw.LastIndexSet]->SetEndPosition(NewSample, false);
					RecordingGestureDraw.SplineMeshes[RecordingGestureDraw.LastIndexSet]->SetEndTangent(RecordingGestureDraw.SplineComponent->GetTangentAtSplinePoint(SplineIndex, ESplineCoordinateSpace::Local), true);
				}

				MeshComp->SetStartScale(SplineMeshScaler);
				MeshComp->SetEndScale(SplineMeshScaler);

				MeshComp->SetStartAndEnd(NewSample,
					RecordingGestureDraw.SplineComponent->GetTangentAtSplinePoint(SplineIndex, ESplineCoordinateSpace::Local),
					NewSample,
					FVector::ZeroVector,
					true);

				if (bGetGestureInWorldSpace)
					MeshComp->SetWorldLocationAndRotation(OriginatingTransform.TransformPosition(StartVector), OriginatingTransform.GetRotation());
				else
					MeshComp->SetRelativeLocationAndRotation(/*OriginatingTransform.TransformPosition(*/StartVector/*)*/, FQuat::Identity/*OriginatingTransform.GetRotation()*/);

				RecordingGestureDraw.LastIndexSet = MeshIndex;
				MeshComp->SetVisibility(true);
			}
		
		}

		GestureLog.Samples.Insert(NewSample, 0);
		bGestureChanged = true;
	}
}

void UVRGestureComponent::TickGesture()
{
	SCOPE_CYCLE_COUNTER(STAT_TickGesture);

	switch (CurrentState)
	{
	case EVRGestureState::GES_Detecting:
	{
		CaptureGestureFrame();
		RecognizeGesture(GestureLog);
		bGestureChanged = false;
	}break;

	case EVRGestureState::GES_Recording:
	{
		CaptureGestureFrame();
	}break;

	case EVRGestureState::GES_None:
	default: {}break;
	}

	if (bDrawRecordingGesture)
	{
		if (!bDrawRecordingGestureAsSpline)
		{
			FTransform DrawTransform = FTransform(StartVector) * OriginatingTransform;
			// Setting the lifetime to the recording htz now, should remove the flicker.
			DrawDebugGesture(this, DrawTransform, GestureLog, FColor::White, false, 0, RecordingDelta, 0.0f);
		}
	}
}

void UVRGestureComponent::RecognizeGesture(FVRGesture inputGesture)
{
	if (!GesturesDB || inputGesture.Samples.Num() < 1 || !bGestureChanged)
		return;

	float minDist = MAX_FLT;

	int OutGestureIndex = -1;
	bool bMirrorGesture = false;

	FVector Size = inputGesture.GestureSize.GetSize();
	float Scaler = GesturesDB->TargetGestureScale / Size.GetMax();
	float FinalScaler = Scaler;

	for (int i = 0; i < GesturesDB->Gestures.Num(); i++)
	{
		FVRGesture &exampleGesture = GesturesDB->Gestures[i];

		if (!exampleGesture.GestureSettings.bEnabled || exampleGesture.Samples.Num() < 1 || inputGesture.Samples.Num() < exampleGesture.GestureSettings.Minimum_Gesture_Length)
			continue;

		FinalScaler = exampleGesture.GestureSettings.bEnableScaling ? Scaler : 1.f;

		bMirrorGesture = (MirroringHand != EVRGestureMirrorMode::GES_NoMirror && MirroringHand != EVRGestureMirrorMode::GES_MirrorBoth && MirroringHand == exampleGesture.GestureSettings.MirrorMode);

		if (GetGestureDistance(inputGesture.Samples[0] * FinalScaler, exampleGesture.Samples[0], bMirrorGesture) < FMath::Square(exampleGesture.GestureSettings.firstThreshold))
		{
			float d = dtw(inputGesture, exampleGesture, bMirrorGesture, FinalScaler) / (exampleGesture.Samples.Num());
			if (d < minDist && d < FMath::Square(exampleGesture.GestureSettings.FullThreshold))
			{
				minDist = d;
				OutGestureIndex = i;
			}
		}
		else if (exampleGesture.GestureSettings.MirrorMode == EVRGestureMirrorMode::GES_MirrorBoth)
		{
			bMirrorGesture = true;
			if (GetGestureDistance(inputGesture.Samples[0] * FinalScaler, exampleGesture.Samples[0], bMirrorGesture) < FMath::Square(exampleGesture.GestureSettings.firstThreshold))
			{
				float d = dtw(inputGesture, exampleGesture, bMirrorGesture, FinalScaler) / (exampleGesture.Samples.Num());
				if (d < minDist && d < FMath::Square(exampleGesture.GestureSettings.FullThreshold))
				{
					minDist = d;
					OutGestureIndex = i;
				}
			}
		}

		/*if (exampleGesture.MirrorMode == EVRGestureMirrorMode::GES_MirrorBoth)
		{
			bMirrorGesture = true;

			if (GetGestureDistance(inputGesture.Samples[0], exampleGesture.Samples[0], bMirrorGesture) < FMath::Square(exampleGesture.GestureSettings.firstThreshold))
			{
				float d = dtw(inputGesture, exampleGesture, bMirrorGesture) / (exampleGesture.Samples.Num());
				if (d < minDist && d < FMath::Square(exampleGesture.GestureSettings.FullThreshold))
				{
					minDist = d;
					OutGestureIndex = i;
				}
			}
		}*/
	}

	if (/*minDist < FMath::Square(globalThreshold) && */OutGestureIndex != -1)
	{
		OnGestureDetected(GesturesDB->Gestures[OutGestureIndex].GestureType, /*minDist,*/ GesturesDB->Gestures[OutGestureIndex].Name, OutGestureIndex, GesturesDB);
		OnGestureDetected_Bind.Broadcast(GesturesDB->Gestures[OutGestureIndex].GestureType, /*minDist,*/ GesturesDB->Gestures[OutGestureIndex].Name, OutGestureIndex, GesturesDB);
		ClearRecording(); // Clear the recording out, we don't want to detect this gesture again with the same data
		RecordingGestureDraw.Reset();
	}
}

float UVRGestureComponent::dtw(FVRGesture seq1, FVRGesture seq2, bool bMirrorGesture, float Scaler)
{

	// #TODO: Skip copying the array and reversing it in the future, we only ever use the reversed value.
	// So pre-reverse it and keep it stored like that on init. When we do the initial sample we can check off of the first index instead of last then

	// Should also be able to get SizeSquared for values and compared to squared thresholds instead of doing the full SQRT calc.

	// Getting number of average samples recorded over of a gesture (top down) may be able to achieve a basic % completed check
	// to see how far into detecting a gesture we are, this would require ignoring the last position threshold though....

	int RowCount = seq1.Samples.Num() + 1;
	int ColumnCount = seq2.Samples.Num() + 1;

	TArray<float> LookupTable;
	LookupTable.AddZeroed(ColumnCount * RowCount);

	TArray<int> SlopeI;
	SlopeI.AddZeroed(ColumnCount * RowCount);
	TArray<int> SlopeJ;
	SlopeJ.AddZeroed(ColumnCount * RowCount);

	for (int i = 1; i < (ColumnCount * RowCount); i++)
	{
		LookupTable[i] = MAX_FLT;
	}
	// Don't need to do this, it is already handled by add zeroed
	//tab[0, 0] = 0;

	int icol = 0, icolneg = 0;

	// Dynamic computation of the DTW matrix.
	for (int i = 1; i < RowCount; i++)
	{
		for (int j = 1; j < ColumnCount; j++)
		{
			icol = i * ColumnCount;
			icolneg = icol - ColumnCount;// (i - 1) * ColumnCount;

			if (
				LookupTable[icol + (j - 1)] < LookupTable[icolneg + (j - 1)] &&
				LookupTable[icol + (j - 1)] < LookupTable[icolneg + j] &&
				SlopeI[icol + (j - 1)] < maxSlope)
			{
				LookupTable[icol + j] = GetGestureDistance(seq1.Samples[i - 1] * Scaler, seq2.Samples[j - 1], bMirrorGesture) + LookupTable[icol + j - 1];
				SlopeI[icol + j] = SlopeJ[icol + j - 1] + 1;
				SlopeJ[icol + j] = 0;
			}
			else if (
				LookupTable[icolneg + j] < LookupTable[icolneg + j - 1] &&
				LookupTable[icolneg + j] < LookupTable[icol + j - 1] &&
				SlopeJ[icolneg + j] < maxSlope)
			{
				LookupTable[icol + j] = GetGestureDistance(seq1.Samples[i - 1] * Scaler, seq2.Samples[j - 1], bMirrorGesture) + LookupTable[icolneg + j];
				SlopeI[icol + j] = 0;
				SlopeJ[icol + j] = SlopeJ[icolneg + j] + 1;
			}
			else
			{
				LookupTable[icol + j] = GetGestureDistance(seq1.Samples[i - 1] * Scaler, seq2.Samples[j - 1], bMirrorGesture) + LookupTable[icolneg + j - 1];
				SlopeI[icol + j] = 0;
				SlopeJ[icol + j] = 0;
			}
		}
	}

	// Find best between seq2 and an ending (postfix) of seq1.
	float bestMatch = FLT_MAX;

	for (int i = 1; i < seq1.Samples.Num() + 1/* - seq2.Minimum_Gesture_Length*/; i++)
	{
		if (LookupTable[(i*ColumnCount) + seq2.Samples.Num()] < bestMatch)
		bestMatch = LookupTable[(i*ColumnCount) + seq2.Samples.Num()];
	}

	return bestMatch;
}

void UVRGestureComponent::DrawDebugGesture(UObject* WorldContextObject, FTransform &StartTransform, FVRGesture GestureToDraw, FColor const& Color, bool bPersistentLines, uint8 DepthPriority, float LifeTime, float Thickness)
{
#if ENABLE_DRAW_DEBUG

	UWorld* InWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);

	if (InWorld != nullptr)
	{
		// no debug line drawing on dedicated server
		if (GEngine->GetNetMode(InWorld) != NM_DedicatedServer && GestureToDraw.Samples.Num() > 1)
		{
			bool bMirrorGesture = (MirroringHand != EVRGestureMirrorMode::GES_NoMirror && MirroringHand == GestureToDraw.GestureSettings.MirrorMode);
			FVector MirrorVector = FVector(1.f, -1.f, 1.f); // Only mirroring on Y axis to flip Left/Right

															// this means foreground lines can't be persistent 
			ULineBatchComponent* const LineBatcher = (InWorld ? ((DepthPriority == SDPG_Foreground) ? InWorld->ForegroundLineBatcher : ((bPersistentLines || (LifeTime > 0.f)) ? InWorld->PersistentLineBatcher : InWorld->LineBatcher)) : NULL);

			if (LineBatcher != NULL)
			{
				float const LineLifeTime = (LifeTime > 0.f) ? LifeTime : LineBatcher->DefaultLifeTime;

				TArray<FBatchedLine> Lines;
				FBatchedLine Line;
				Line.Color = Color;
				Line.Thickness = Thickness;
				Line.RemainingLifeTime = LineLifeTime;
				Line.DepthPriority = DepthPriority;

				FVector FirstLoc = bMirrorGesture ? GestureToDraw.Samples[GestureToDraw.Samples.Num() - 1] * MirrorVector : GestureToDraw.Samples[GestureToDraw.Samples.Num() - 1];

				for (int i = GestureToDraw.Samples.Num() - 2; i >= 0; --i)
				{
					Line.Start = bMirrorGesture ? GestureToDraw.Samples[i] * MirrorVector : GestureToDraw.Samples[i];

					Line.End = FirstLoc;
					FirstLoc = Line.Start;

					Line.End = StartTransform.TransformPosition(Line.End);
					Line.Start = StartTransform.TransformPosition(Line.Start);

					Lines.Add(Line);
				}

				LineBatcher->DrawLines(Lines);
			}
		}
	}
#endif
}

void UGesturesDatabase::RecalculateGestures(bool bScaleToDatabase)
{
	for (int i = 0; i < Gestures.Num(); ++i)
	{
		Gestures[i].CalculateSizeOfGesture(bScaleToDatabase, TargetGestureScale);
	}
}

bool UGesturesDatabase::ImportSplineAsGesture(USplineComponent * HostSplineComponent, FString GestureName, bool bKeepSplineCurves, float SegmentLen, bool bScaleToDatabase)
{
	FVRGesture NewGesture;

	if (HostSplineComponent->GetNumberOfSplinePoints() < 2)
		return false;

	NewGesture.Name = GestureName;

	FVector FirstPointPos = HostSplineComponent->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local);

	float LastDistance = 0.f;
	float ThisDistance = 0.f;
	FVector LastDistanceV;
	FVector ThisDistanceV;
	FVector DistNormal;
	float DistAlongSegment = 0.f;

	// Realign to xForward on the gesture, normally splines lay out as X to the right
	FTransform Realignment = FTransform(FRotator(0.f, 90.f, 0.f), -FirstPointPos);

	// Prefill the first point
	NewGesture.Samples.Add(Realignment.TransformPosition(HostSplineComponent->GetLocationAtSplinePoint(HostSplineComponent->GetNumberOfSplinePoints() - 1, ESplineCoordinateSpace::Local)));

	// Inserting in reverse order -2 so we start one down
	for (int i = HostSplineComponent->GetNumberOfSplinePoints() - 2; i >= 0; --i)
	{
		if (bKeepSplineCurves)
		{
			LastDistance = HostSplineComponent->GetDistanceAlongSplineAtSplinePoint(i + 1);
			ThisDistance = HostSplineComponent->GetDistanceAlongSplineAtSplinePoint(i);

			DistAlongSegment = FMath::Abs(ThisDistance - LastDistance);
		}
		else
		{
			LastDistanceV = Realignment.TransformPosition(HostSplineComponent->GetLocationAtSplinePoint(i + 1, ESplineCoordinateSpace::Local));
			ThisDistanceV = Realignment.TransformPosition(HostSplineComponent->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local));

			DistAlongSegment = FVector::Dist(ThisDistanceV, LastDistanceV);
			DistNormal = ThisDistanceV - LastDistanceV;
			DistNormal.Normalize();
		}


		float SegmentCount = FMath::FloorToFloat(DistAlongSegment / SegmentLen);
		float OverFlow = FMath::Fmod(DistAlongSegment, SegmentLen);

		if (SegmentCount < 1)
		{
			SegmentCount++;
		}

		float DistPerSegment = (DistAlongSegment / SegmentCount);

		for (int j = 0; j < SegmentCount; j++)
		{
			if (j == SegmentCount - 1 && i > 0)
				DistPerSegment += OverFlow;

			if (bKeepSplineCurves)
			{
				LastDistance -= DistPerSegment;
				if (j == SegmentCount - 1 && i > 0)
				{
					LastDistance = ThisDistance;
				}
				FVector loc = Realignment.TransformPosition(HostSplineComponent->GetLocationAtDistanceAlongSpline(LastDistance, ESplineCoordinateSpace::Local));

				if (!loc.IsNearlyZero())
					NewGesture.Samples.Add(loc);
			}
			else
			{
				LastDistanceV += DistPerSegment * DistNormal;

				if (j == SegmentCount - 1 && i > 0)
				{
					LastDistanceV = ThisDistanceV;
				}

				if (!LastDistanceV.IsNearlyZero())
					NewGesture.Samples.Add(LastDistanceV);
			}
		}
	}

	NewGesture.CalculateSizeOfGesture(bScaleToDatabase, this->TargetGestureScale);
	Gestures.Add(NewGesture);
	return true;
}

void FVRGestureSplineDraw::ClearLastPoint()
{
	SplineComponent->RemoveSplinePoint(0, false);

	if (SplineMeshes.Num() < NextIndexCleared + 1)
		NextIndexCleared = 0;

	SplineMeshes[NextIndexCleared]->SetVisibility(false);
	NextIndexCleared++;
}

void FVRGestureSplineDraw::Reset()
{
	if (SplineComponent != nullptr)
		SplineComponent->ClearSplinePoints(true);

	for (int i = SplineMeshes.Num() - 1; i >= 0; --i)
	{
		if (SplineMeshes[i] != nullptr)
			SplineMeshes[i]->SetVisibility(false);
		else
			SplineMeshes.RemoveAt(i);
	}

	LastIndexSet = 0;
	NextIndexCleared = 0;
}

void FVRGestureSplineDraw::Clear()
{
	for (int i = 0; i < SplineMeshes.Num(); ++i)
	{
		if (SplineMeshes[i] != nullptr && !SplineMeshes[i]->IsBeingDestroyed())
		{
			SplineMeshes[i]->Modify();
			SplineMeshes[i]->DestroyComponent();
		}
	}
	SplineMeshes.Empty();

	if (SplineComponent != nullptr)
	{
		SplineComponent->DestroyComponent();
		SplineComponent = nullptr;
	}

	LastIndexSet = 0;
	NextIndexCleared = 0;
}

FVRGestureSplineDraw::FVRGestureSplineDraw()
{
	SplineComponent = nullptr;
	NextIndexCleared = 0;
	LastIndexSet = 0;
}

FVRGestureSplineDraw::~FVRGestureSplineDraw()
{
	Clear();
}

void UVRGestureComponent::BeginDestroy()
{
	Super::BeginDestroy();
	RecordingGestureDraw.Clear();
	if (TickGestureTimer_Handle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(TickGestureTimer_Handle);
	}
}

void UVRGestureComponent::RecalculateGestureSize(FVRGesture & InputGesture, UGesturesDatabase * GestureDB)
{
	if (GestureDB != nullptr)
		InputGesture.CalculateSizeOfGesture(true, GestureDB->TargetGestureScale);
	else
		InputGesture.CalculateSizeOfGesture(false);
}

FVRGesture UVRGestureComponent::EndRecording()
{
	if (TickGestureTimer_Handle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(TickGestureTimer_Handle);
	}

	this->SetComponentTickEnabled(false);
	CurrentState = EVRGestureState::GES_None;

	// Reset the recording gesture
	RecordingGestureDraw.Reset();

	return GestureLog;
}

void UVRGestureComponent::ClearRecording()
{
	GestureLog.Samples.Reset(RecordingBufferSize);
}

void UVRGestureComponent::SaveRecording(FVRGesture &Recording, FString RecordingName, bool bScaleRecordingToDatabase)
{
	if (GesturesDB)
	{
		Recording.CalculateSizeOfGesture(bScaleRecordingToDatabase, GesturesDB->TargetGestureScale);
		Recording.Name = RecordingName;
		GesturesDB->Gestures.Add(Recording);
	}
}
