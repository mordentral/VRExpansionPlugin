// Copyright Epic Games, Inc. All Rights Reserved.

#include "HandSocketComponentDetails.h"
#include "HandSocketVisualizer.h"
//#include "PropertyEditing.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "IDetailsView.h"

#include "Developer/AssetTools/Public/IAssetTools.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "Editor/ContentBrowser/Public/IContentBrowserSingleton.h"
#include "Editor/ContentBrowser/Public/ContentBrowserModule.h"
#include "AnimationUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/MessageDialog.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"

#include "Animation/AnimData/AnimDataModel.h"

#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"

#define LOCTEXT_NAMESPACE "HandSocketComponentDetails"

FText SCreateHandAnimationDlg::LastUsedAssetPath;

static bool PromptUserForAssetPath(FString& AssetPath, FString& AssetName)
{
	TSharedRef<SCreateHandAnimationDlg> NewAnimDlg = SNew(SCreateHandAnimationDlg);
	if (NewAnimDlg->ShowModal() != EAppReturnType::Cancel)
	{
		AssetPath = NewAnimDlg->GetFullAssetPath();
		AssetName = NewAnimDlg->GetAssetName();
		return true;
	}

	return false;
}

TWeakObjectPtr<UAnimSequence> FHandSocketComponentDetails::SaveAnimationAsset(const FString& InAssetPath, const FString& InAssetName)
{

	TWeakObjectPtr<UAnimSequence> FinalAnimation;

	// Replace when this moves to custom display
	if (!HandSocketComponent.IsValid())
		return FinalAnimation;

	/*if (!HandSocketComponent->HandVisualizerComponent)// || !HandSocketComponent->HandVisualizerComponent->SkeletalMesh || !HandSocketComponent->HandVisualizerComponent->SkeletalMesh->Skeleton)
	{
		return false;
	}*/

	if (!HandSocketComponent->HandTargetAnimation && (!HandSocketComponent->VisualizationMesh || !HandSocketComponent->VisualizationMesh->GetSkeleton()))
	{
		return FinalAnimation;
	}

	// create the asset
	FText InvalidPathReason;
	bool const bValidPackageName = FPackageName::IsValidLongPackageName(InAssetPath, false, &InvalidPathReason);
	if (bValidPackageName == false)
	{
		UE_LOG(LogAnimation, Log, TEXT("%s is an invalid asset path, prompting user for new asset path. Reason: %s"), *InAssetPath, *InvalidPathReason.ToString());
	}

	FString ValidatedAssetPath = InAssetPath;
	FString ValidatedAssetName = InAssetName;

	UObject* Parent = bValidPackageName ? CreatePackage(*ValidatedAssetPath) : nullptr;
	if (Parent == nullptr)
	{
		// bad or no path passed in, do the popup
		if (PromptUserForAssetPath(ValidatedAssetPath, ValidatedAssetName) == false)
		{
			return FinalAnimation;
		}

		Parent = CreatePackage(*ValidatedAssetPath);
	}

	UObject* const Object = LoadObject<UObject>(Parent, *ValidatedAssetName, nullptr, LOAD_Quiet, nullptr);
	// if object with same name exists, warn user
	if (Object)
	{
		EAppReturnType::Type ReturnValue = FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("Error_AssetExist", "Asset with same name exists. Do you wish to overwrite it?"));
		if (ReturnValue == EAppReturnType::No)
		{
			return FinalAnimation; // failed
		}
	}

	UAnimSequence* BaseAnimation = HandSocketComponent->HandTargetAnimation;
	TArray<FTransform> LocalPoses;

	if (!BaseAnimation)
	{
		LocalPoses = HandSocketComponent->VisualizationMesh->GetRefSkeleton().GetRefBonePose();
	}

	// If not, create new one now.
	UAnimSequence* const NewSeq = NewObject<UAnimSequence>(Parent, *ValidatedAssetName, RF_Public | RF_Standalone);
	if (NewSeq)
	{
		// set skeleton
		if (BaseAnimation)
		{
			NewSeq->SetSkeleton(BaseAnimation->GetSkeleton());
		}
		else
		{
			NewSeq->SetSkeleton(HandSocketComponent->VisualizationMesh->GetSkeleton());
		}

		// Notify the asset registry
		FAssetRegistryModule::AssetCreated(NewSeq);
		//StartRecord(Component, NewSeq);

		//return true;
		UAnimSequence* AnimationObject = NewSeq;

		IAnimationDataController& AnimController = AnimationObject->GetController();
		{
			IAnimationDataController::FScopedBracket ScopedBracket(AnimController, LOCTEXT("SaveAnimationAsset_VRE", "Creating Animation Sequence based on hand pose"));
			AnimationObject->ResetAnimation();
			if (BaseAnimation)
			{
				AnimationObject->BoneCompressionSettings = BaseAnimation->BoneCompressionSettings;
			}
			else
			{
				AnimationObject->BoneCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
			}

			AnimController.InitializeModel();
			AnimController.RemoveAllBoneTracks(false);
			

			//checkf(MovieScene, TEXT("No Movie Scene found for SequencerDataModel"));
			//AnimController.SetPlayLength(4.f);
			AnimController.SetNumberOfFrames(FFrameNumber(1), false);

			// Set frame rate to 1 to 1 as we don't animate
			AnimController.SetFrameRate(FFrameRate(1, 1));

			TArray<FName> TrackNames;
			const IAnimationDataModel* BaseDataModel = BaseAnimation ? BaseAnimation->GetController().GetModel() : nullptr;

			if (BaseAnimation)
			{
				if (BaseDataModel)
				{
					BaseDataModel->GetBoneTrackNames(TrackNames);
					for (FName TrackName : TrackNames)
					{
						AnimController.AddBoneCurve(TrackName);
					}
				}
				else
				{
					return FinalAnimation;
				}
			}
			else
			{
				int numBones = HandSocketComponent->VisualizationMesh->GetRefSkeleton().GetNum();
				for (int i = 0; i < LocalPoses.Num() && i < numBones; ++i)
				{
					AnimController.AddBoneCurve(HandSocketComponent->VisualizationMesh->GetRefSkeleton().GetBoneName(i));
					//AnimController.AddBoneTrack(HandSocketComponent->VisualizationMesh->GetRefSkeleton().GetBoneName(i));
				}
			}

			if (BaseAnimation)
			{
				AnimationObject->RetargetSource = BaseAnimation->RetargetSource;
			}
			else
			{
				AnimationObject->RetargetSource = HandSocketComponent->VisualizationMesh ? HandSocketComponent->VisualizationMesh->GetSkeleton()->GetRetargetSourceForMesh(HandSocketComponent->VisualizationMesh) : NAME_None;
			}

			const IAnimationDataModel* DataModel = AnimController.GetModel();

			/// SAVE POSE
			if (BaseAnimation && DataModel && BaseDataModel)
			{
				for (int32 TrackIndex = 0; TrackIndex < /*DataModel->GetBoneAnimationTracks().Num()*/BaseDataModel->GetNumBoneTracks(); ++TrackIndex)
				{
					FName TrackName = TrackIndex < TrackNames.Num() ? TrackNames[TrackIndex] : NAME_None;
					if (!BaseDataModel->IsValidBoneTrackName(TrackName))
					{
						continue;
					}

					FTransform FinalTrans = BaseDataModel->GetBoneTrackTransform(TrackName, 0);
					//FTransform FinalTrans(Rot, Loc, Scale);

					FQuat DeltaQuat = FQuat::Identity;
					for (FBPVRHandPoseBonePair& HandPair : HandSocketComponent->CustomPoseDeltas)
					{
						if (HandPair.BoneName == TrackName)
						{
							DeltaQuat = HandPair.DeltaPose;
							break;
						}
					}

					FinalTrans.ConcatenateRotation(DeltaQuat);
					FinalTrans.NormalizeRotation();

					//FRawAnimSequenceTrack& RawNewTrack = DataModel->GetBoneTrackByIndex(TrackIndex).InternalTrackData;
					AnimController.SetBoneTrackKeys(TrackName, { FinalTrans.GetTranslation() }, { FinalTrans.GetRotation() }, { FinalTrans.GetScale3D() });
				}
			}
			else if(DataModel)
			{
				USkeletalMesh* SkeletalMesh = HandSocketComponent->VisualizationMesh;
				FReferenceSkeleton RefSkeleton = SkeletalMesh->GetRefSkeleton();	
				USkeleton* AnimSkeleton = SkeletalMesh->GetSkeleton();

				for (int32 TrackIndex = 0; TrackIndex < RefSkeleton.GetNum(); ++TrackIndex)
				{
					
					FName TrackName = RefSkeleton.GetBoneName(TrackIndex);
					if (!DataModel->IsValidBoneTrackName(TrackName))
					{
						continue;
					}

					int32 BoneTreeIndex = RefSkeleton.FindBoneIndex(TrackName);

					// verify if this bone exists in skeleton
					//int32 BoneTreeIndex = DataModel->GetBoneTrackByIndex(TrackIndex).BoneTreeIndex;

					if (BoneTreeIndex != INDEX_NONE)
					{

						int32 BoneIndex = BoneTreeIndex;//AnimSkeleton->GetMeshBoneIndexFromSkeletonBoneIndex(SkeletalMesh, BoneTreeIndex);
						//int32 ParentIndex = SkeletalMesh->RefSkeleton.GetParentIndex(BoneIndex);
						FTransform LocalTransform = LocalPoses[BoneIndex];
						//FTransform LocalTransform = RefSkeleton.GetRefBonePose()[BoneIndex];

						FName BoneName = AnimSkeleton->GetReferenceSkeleton().GetBoneName(BoneIndex);

						FQuat DeltaQuat = FQuat::Identity;
						for (FBPVRHandPoseBonePair& HandPair : HandSocketComponent->CustomPoseDeltas)
						{
							if (HandPair.BoneName == BoneName)
							{
								DeltaQuat = HandPair.DeltaPose;
							}
						}

						LocalTransform.ConcatenateRotation(DeltaQuat);
						LocalTransform.NormalizeRotation();

						AnimController.SetBoneTrackKeys(BoneName, { LocalTransform.GetTranslation() }, { LocalTransform.GetRotation() }, { LocalTransform.GetScale3D() });
					}
				}
			}

			AnimController.NotifyPopulated();
		}
		/// END SAVE POSE 
		/// 
		/// 
		/// 

		// init notifies
		AnimationObject->InitializeNotifyTrack();
		//#TODO: 5.1, need to figure out what they replaced this with
		//PRAGMA_DISABLE_DEPRECATION_WARNINGS
			//AnimationObject->PostProcessSequence();
		//PRAGMA_ENABLE_DEPRECATION_WARNINGS
		AnimationObject->MarkPackageDirty();

		//if (bAutoSaveAsset)
		{
			UPackage* const Package = AnimationObject->GetOutermost();
			FString const PackageName = Package->GetName();
			FString const PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

			double StartTime = FPlatformTime::Seconds();

			FSavePackageArgs PackageArguments;
			PackageArguments.SaveFlags = RF_Standalone;
			PackageArguments.SaveFlags = SAVE_NoError;
			UPackage::SavePackage(Package, NULL, *PackageFileName, PackageArguments);
			//UPackage::SavePackage(Package, NULL, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_NoError);

			double ElapsedTime = FPlatformTime::Seconds() - StartTime;
			UE_LOG(LogAnimation, Log, TEXT("Animation Recorder saved %s in %0.2f seconds"), *PackageName, ElapsedTime);
		}

		FinalAnimation = AnimationObject;
		return FinalAnimation;
	}

	return FinalAnimation;
}
TSharedRef< IDetailCustomization > FHandSocketComponentDetails::MakeInstance()
{
    return MakeShareable(new FHandSocketComponentDetails);
}

void FHandSocketComponentDetails::OnHandRelativeUpdated(IDetailLayoutBuilder* LayoutBuilder)
{

	if (!HandSocketComponent.IsValid())
	{
		return;
	}

	HandSocketComponent->Modify();
	if (AActor* Owner = HandSocketComponent->GetOwner())
	{
		Owner->Modify();
	}

	TSharedPtr<FComponentVisualizer> Visualizer = GUnrealEd->FindComponentVisualizer(HandSocketComponent->GetClass());
	FHandSocketVisualizer* HandVisualizer = (FHandSocketVisualizer*)Visualizer.Get();

	if (HandVisualizer)
	{
		if (UHandSocketComponent* RefHand = HandVisualizer->GetCurrentlyEditingComponent())
		{
			RefHand->HandRelativePlacement = HandSocketComponent->HandRelativePlacement;
		}
	}

	FComponentVisualizer::NotifyPropertyModified(HandSocketComponent.Get(), FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement)));
}

void FHandSocketComponentDetails::OnLeftDominantUpdated(IDetailLayoutBuilder* LayoutBuilder)
{

	if (!HandSocketComponent.IsValid())
	{
		return;
	}

	// Default to always flipping this
	//if (HandSocketComponent->bFlipForLeftHand)
	{
		FTransform relTrans = HandSocketComponent->GetRelativeTransform();
		FTransform HandPlacement = HandSocketComponent->GetHandRelativePlacement();

		if (HandSocketComponent->bDecoupleMeshPlacement)
		{
			relTrans = FTransform::Identity;
		}

		FTransform ReturnTrans = (HandPlacement * relTrans);

		HandSocketComponent->MirrorHandTransform(ReturnTrans, relTrans);
	
		HandSocketComponent->Modify();
		if (AActor* Owner = HandSocketComponent->GetOwner())
		{
			Owner->Modify();
		}
		ReturnTrans = ReturnTrans.GetRelativeTransform(relTrans);
		HandSocketComponent->HandRelativePlacement = ReturnTrans;

		TSharedPtr<FComponentVisualizer> Visualizer = GUnrealEd->FindComponentVisualizer(HandSocketComponent->GetClass());
		FHandSocketVisualizer* HandVisualizer = (FHandSocketVisualizer*)Visualizer.Get();

		if (HandVisualizer)
		{
			if (UHandSocketComponent* RefHand = HandVisualizer->GetCurrentlyEditingComponent())
			{
				RefHand->HandRelativePlacement = HandSocketComponent->HandRelativePlacement;
				//FComponentVisualizer::NotifyPropertyModified(RefHand, FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement)));
			}
		}

		FComponentVisualizer::NotifyPropertyModified(HandSocketComponent.Get(), FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement)));
	}
}

void FHandSocketComponentDetails::OnLockedStateUpdated(IDetailLayoutBuilder* LayoutBuilder)
{

	if (!HandSocketComponent.IsValid())
	{
		return;
	}

	if (HandSocketComponent->bDecoupleMeshPlacement)
	{
		//FTransform RelTrans = HandSocketComponent->GetRelativeTransform();
		//FTransform WorldTrans = HandSocketComponent->GetComponentTransform();
		//if (USceneComponent* ParentComp = HandSocketComponent->GetAttachParent())
		{

			HandSocketComponent->Modify();
			if (AActor* Owner = HandSocketComponent->GetOwner())
			{
				Owner->Modify();
			}

			HandSocketComponent->HandRelativePlacement = HandSocketComponent->HandRelativePlacement * HandSocketComponent->GetRelativeTransform();// HandSocketComponent->GetComponentTransform();
			HandSocketComponent->bDecoupled = true;

			TSharedPtr<FComponentVisualizer> Visualizer = GUnrealEd->FindComponentVisualizer(HandSocketComponent->GetClass());
			FHandSocketVisualizer* HandVisualizer = (FHandSocketVisualizer*)Visualizer.Get();

			if (HandVisualizer)
			{
				if (UHandSocketComponent* RefHand = HandVisualizer->GetCurrentlyEditingComponent())
				{
					RefHand->HandRelativePlacement = HandSocketComponent->HandRelativePlacement;
					RefHand->bDecoupled = true;
					//FComponentVisualizer::NotifyPropertyModified(RefHand, FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement)));
				}
			}
		}
	}
	else
	{
		//if (USceneComponent* ParentComp = HandSocketComponent->GetAttachParent())
		{
			HandSocketComponent->Modify();
			if (AActor* Owner = HandSocketComponent->GetOwner())
			{
				Owner->Modify();
			}
			HandSocketComponent->HandRelativePlacement = HandSocketComponent->HandRelativePlacement.GetRelativeTransform(HandSocketComponent->GetRelativeTransform());
			HandSocketComponent->bDecoupled = false;
			
			TSharedPtr<FComponentVisualizer> Visualizer = GUnrealEd->FindComponentVisualizer(HandSocketComponent->GetClass());
			FHandSocketVisualizer* HandVisualizer = (FHandSocketVisualizer*)Visualizer.Get();

			if (HandVisualizer)
			{
				if (UHandSocketComponent* RefHand = HandVisualizer->GetCurrentlyEditingComponent())
				{
					RefHand->HandRelativePlacement = HandSocketComponent->HandRelativePlacement;
					RefHand->bDecoupled = false;
					//FComponentVisualizer::NotifyPropertyModified(RefHand, FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement)));
				}
			}
		}
	}

	TArray<FProperty*> PropertiesToModify;
	PropertiesToModify.Add(FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement)));
	PropertiesToModify.Add(FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bDecoupled)));
	FComponentVisualizer::NotifyPropertiesModified(HandSocketComponent.Get(), PropertiesToModify);
}

void FHandSocketComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Hide the SplineCurves property
	//TSharedPtr<IPropertyHandle> HandPlacementProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement));
	//HandPlacementProperty->MarkHiddenByCustomization();


	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	if (ObjectsBeingCustomized.Num() == 1)
	{
		UHandSocketComponent* CurrentHandSocket = Cast<UHandSocketComponent>(ObjectsBeingCustomized[0]);
		if (CurrentHandSocket != NULL)
		{
			if (HandSocketComponent != CurrentHandSocket)
			{
				TSharedPtr<FComponentVisualizer> Visualizer = GUnrealEd->FindComponentVisualizer(CurrentHandSocket->GetClass());
				FHandSocketVisualizer* HandVisualizer = (FHandSocketVisualizer*)Visualizer.Get();

				if (HandVisualizer)
				{
					HandVisualizer->CurrentlySelectedBoneIdx = INDEX_NONE;
					HandVisualizer->CurrentlySelectedBone = NAME_None;
					HandVisualizer->HandPropertyPath = FComponentPropertyPath();
					//HandVisualizer->OldHandSocketComp = CurrentHandSocket;
				}

				HandSocketComponent = CurrentHandSocket;
			}
		}
	}

	/*const TArray< TWeakObjectPtr<UObject> >& SelectedObjects = DetailBuilder.GetSelectedObjects();
	for (int32 ObjectIndex = 0; ObjectIndex < SelectedObjects.Num(); ++ObjectIndex)
	{
		const TWeakObjectPtr<UObject>& CurrentObject = SelectedObjects[ObjectIndex];
		if (CurrentObject.IsValid())
		{
			UHandSocketComponent* CurrentHandSocket = Cast<UHandSocketComponent>(CurrentObject.Get());
			if (CurrentHandSocket != NULL)
			{
				if (HandSocketComponent != CurrentHandSocket)
				{
					TSharedPtr<FComponentVisualizer> Visualizer = GUnrealEd->FindComponentVisualizer(CurrentHandSocket->GetClass());
					FHandSocketVisualizer* HandVisualizer = (FHandSocketVisualizer*)Visualizer.Get();

					if (HandVisualizer)
					{
						HandVisualizer->CurrentlySelectedBoneIdx = INDEX_NONE;
						HandVisualizer->CurrentlySelectedBone = NAME_None;
						HandVisualizer->HandPropertyPath = FComponentPropertyPath();
						//HandVisualizer->OldHandSocketComp = CurrentHandSocket;
					}

					HandSocketComponent = CurrentHandSocket;
				}
				break;
			}
		}
	}*/

	DetailBuilder.HideCategory(FName("ComponentTick"));
	DetailBuilder.HideCategory(FName("GameplayTags"));
	DetailBuilder.HideCategory(FName("VRGripInterface"));
	DetailBuilder.HideCategory(FName("VRGripInterface|Replication"));
	DetailBuilder.HideCategory(FName("Tags"));
	DetailBuilder.HideCategory(FName("AssetUserData"));
	DetailBuilder.HideCategory(FName("Events"));
	DetailBuilder.HideCategory(FName("Activation"));
	DetailBuilder.HideCategory(FName("Cooking"));
	DetailBuilder.HideCategory(FName("ComponentReplication"));

	TSharedPtr<IPropertyHandle> LockedLocationProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bDecoupleMeshPlacement));
	TSharedPtr<IPropertyHandle> HandRelativePlacementProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement));
	TSharedPtr<IPropertyHandle> LeftHandDominateProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bLeftHandDominant));

	FSimpleDelegate OnHandRelativeChangedDelegate = FSimpleDelegate::CreateSP(this, &FHandSocketComponentDetails::OnHandRelativeUpdated, &DetailBuilder);
	HandRelativePlacementProperty->SetOnPropertyValueChanged(OnHandRelativeChangedDelegate);

	FSimpleDelegate OnLockedStateChangedDelegate = FSimpleDelegate::CreateSP(this, &FHandSocketComponentDetails::OnLockedStateUpdated, &DetailBuilder);
	LockedLocationProperty->SetOnPropertyValueChanged(OnLockedStateChangedDelegate);

	FSimpleDelegate OnLeftDominateChangedDelegate = FSimpleDelegate::CreateSP(this, &FHandSocketComponentDetails::OnLeftDominantUpdated, &DetailBuilder);
	LeftHandDominateProperty->SetOnPropertyValueChanged(OnLeftDominateChangedDelegate);

	TSharedPtr<IPropertyHandle> ShowVisualizationProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bShowVisualizationMesh));

	FSimpleDelegate OnShowVisChangedDelegate = FSimpleDelegate::CreateSP(this, &FHandSocketComponentDetails::OnUpdateShowMesh, &DetailBuilder);
	ShowVisualizationProperty->SetOnPropertyValueChanged(OnShowVisChangedDelegate);

	DetailBuilder.EditCategory("Hand Animation")
		.AddCustomRow(LOCTEXT("UpdateHandSocketRow", "Save Current Pose"))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("UpdateHandSocketText", "Save Current Pose"))
		]
	.ValueContent()
		.MaxDesiredWidth(125.f)
		.MinDesiredWidth(125.f)
		[
			SNew(SButton)
			.ContentPadding(2)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.OnClicked(this, &FHandSocketComponentDetails::OnUpdateSavePose)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("UpdateHandSocketButton", "Save"))
		]
		];
}

void FHandSocketComponentDetails::OnUpdateShowMesh(IDetailLayoutBuilder* LayoutBuilder)
{
	if (!HandSocketComponent.IsValid())
		return;

	TSharedPtr<FComponentVisualizer> Visualizer = GUnrealEd->FindComponentVisualizer(HandSocketComponent->GetClass());
	FHandSocketVisualizer* HandVisualizer = (FHandSocketVisualizer*)Visualizer.Get();

	if (HandVisualizer)
	{
		HandVisualizer->CurrentlySelectedBoneIdx = INDEX_NONE;
		HandVisualizer->CurrentlySelectedBone = NAME_None;
		HandVisualizer->HandPropertyPath = FComponentPropertyPath();
	}
}

FReply FHandSocketComponentDetails::OnUpdateSavePose()
{
	if (HandSocketComponent.IsValid() && HandSocketComponent->CustomPoseDeltas.Num() > 0)
	{
		if (HandSocketComponent->HandTargetAnimation || HandSocketComponent->VisualizationMesh)
		{
			// Save Animation Pose here
			FString AssetPath;
			FString AssetName;
			PromptUserForAssetPath(AssetPath, AssetName);
			TWeakObjectPtr<UAnimSequence> NewAnim = SaveAnimationAsset(AssetPath, AssetName);

			// Finally remove the deltas
			if (NewAnim.IsValid())
			{
				HandSocketComponent->Modify();
				if (AActor* Owner = HandSocketComponent->GetOwner())
				{
					Owner->Modify();
				}

				HandSocketComponent->HandTargetAnimation = NewAnim.Get();
				HandSocketComponent->CustomPoseDeltas.Empty();
				HandSocketComponent->bUseCustomPoseDeltas = false;

				TSharedPtr<FComponentVisualizer> Visualizer = GUnrealEd->FindComponentVisualizer(HandSocketComponent->GetClass());
				FHandSocketVisualizer* HandVisualizer = (FHandSocketVisualizer*)Visualizer.Get();

				if (HandVisualizer)
				{
					if (UHandSocketComponent* RefHand = HandVisualizer->GetCurrentlyEditingComponent())
					{
						RefHand->HandTargetAnimation = NewAnim.Get();
						RefHand->CustomPoseDeltas.Empty();
						RefHand->bUseCustomPoseDeltas = false;
						/*TArray<FProperty*> PropertiesToModify;
						PropertiesToModify.Add(FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandTargetAnimation)));
						PropertiesToModify.Add(FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bUseCustomPoseDeltas)));
						PropertiesToModify.Add(FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, CustomPoseDeltas)));
						FComponentVisualizer::NotifyPropertiesModified(RefHand, PropertiesToModify);*/
					}
				}
				
				// Modify all of the properties at once
				TArray<FProperty*> PropertiesToModify;
				PropertiesToModify.Add(FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandTargetAnimation)));
				PropertiesToModify.Add(FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bUseCustomPoseDeltas)));
				PropertiesToModify.Add(FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, CustomPoseDeltas)));
				FComponentVisualizer::NotifyPropertiesModified(HandSocketComponent.Get(), PropertiesToModify);
			}
		}
	}

	return FReply::Handled();
}

void SCreateHandAnimationDlg::Construct(const FArguments& InArgs)
{
	AssetPath = FText::FromString(FPackageName::GetLongPackagePath(InArgs._DefaultAssetPath.ToString()));
	AssetName = FText::FromString(FPackageName::GetLongPackageAssetName(InArgs._DefaultAssetPath.ToString()));

	if (AssetPath.IsEmpty())
	{
		AssetPath = LastUsedAssetPath;
		// still empty?
		if (AssetPath.IsEmpty())
		{
			AssetPath = FText::FromString(TEXT("/Game"));
		}
	}
	else
	{
		LastUsedAssetPath = AssetPath;
	}

	if (AssetName.IsEmpty())
	{
		// find default name for them
		FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
		FString OutPackageName, OutAssetName;
		FString PackageName = AssetPath.ToString() + TEXT("/NewAnimation");

		AssetToolsModule.Get().CreateUniqueAssetName(PackageName, TEXT(""), OutPackageName, OutAssetName);
		AssetName = FText::FromString(OutAssetName);
	}

	FPathPickerConfig PathPickerConfig;
	PathPickerConfig.DefaultPath = AssetPath.ToString();
	PathPickerConfig.OnPathSelected = FOnPathSelected::CreateSP(this, &SCreateHandAnimationDlg::OnPathChange);
	PathPickerConfig.bAddDefaultPath = true;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	SWindow::Construct(SWindow::FArguments()
		.Title(LOCTEXT("SCreateHandAnimationDlg_Title", "Create New Animation Object"))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		//.SizingRule( ESizingRule::Autosized )
		.ClientSize(FVector2D(450, 450))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot() // Add user input block
		.Padding(2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SelectPath", "Select Path to create animation"))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
		]

	+ SVerticalBox::Slot()
		.FillHeight(1)
		.Padding(3)
		[
			ContentBrowserModule.Get().CreatePathPicker(PathPickerConfig)
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, 10, 0)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AnimationName", "Animation Name"))
		]

	+ SHorizontalBox::Slot()
		[
			SNew(SEditableTextBox)
			.Text(AssetName)
		.OnTextCommitted(this, &SCreateHandAnimationDlg::OnNameChange)
		.MinDesiredWidth(250)
		]
		]
		]
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(5)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(FAppStyle::GetMargin("StandardDialog.SlotPadding"))
		.MinDesiredSlotWidth(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotWidth"))
		.MinDesiredSlotHeight(FAppStyle::GetFloat("StandardDialog.MinDesiredSlotHeight"))
		+ SUniformGridPanel::Slot(0, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
		.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
		.Text(LOCTEXT("OK", "OK"))
		.OnClicked(this, &SCreateHandAnimationDlg::OnButtonClick, EAppReturnType::Ok)
		]
	+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
		.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
		.Text(LOCTEXT("Cancel", "Cancel"))
		.OnClicked(this, &SCreateHandAnimationDlg::OnButtonClick, EAppReturnType::Cancel)
		]
		]
		]);
}

void SCreateHandAnimationDlg::OnNameChange(const FText& NewName, ETextCommit::Type CommitInfo)
{
	AssetName = NewName;
}

void SCreateHandAnimationDlg::OnPathChange(const FString& NewPath)
{
	AssetPath = FText::FromString(NewPath);
	LastUsedAssetPath = AssetPath;
}

FReply SCreateHandAnimationDlg::OnButtonClick(EAppReturnType::Type ButtonID)
{
	UserResponse = ButtonID;

	if (ButtonID != EAppReturnType::Cancel)
	{
		if (!ValidatePackage())
		{
			// reject the request
			return FReply::Handled();
		}
	}

	RequestDestroyWindow();

	return FReply::Handled();
}

/** Ensures supplied package name information is valid */
bool SCreateHandAnimationDlg::ValidatePackage()
{
	FText Reason;
	FString FullPath = GetFullAssetPath();

	if (!FPackageName::IsValidLongPackageName(FullPath, false, &Reason)
		|| !FName(*AssetName.ToString()).IsValidObjectName(Reason))
	{
		FMessageDialog::Open(EAppMsgType::Ok, Reason);
		return false;
	}

	return true;
}

EAppReturnType::Type SCreateHandAnimationDlg::ShowModal()
{
	GEditor->EditorAddModalWindow(SharedThis(this));
	return UserResponse;
}

FString SCreateHandAnimationDlg::GetAssetPath()
{
	return AssetPath.ToString();
}

FString SCreateHandAnimationDlg::GetAssetName()
{
	return AssetName.ToString();
}

FString SCreateHandAnimationDlg::GetFullAssetPath()
{
	return AssetPath.ToString() + "/" + AssetName.ToString();
}

#undef LOCTEXT_NAMESPACE
