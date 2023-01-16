// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "HeadMountedDisplayTypes.h"
#if defined(OPENXR_SUPPORTED)
#include "OpenXRCore.h"
#include "OpenXRHMD.h"
#endif
#include "OpenXRExpansionTypes.h"
#include "HeadMountedDisplayFunctionLibrary.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "OpenXRExpansionFunctionLibrary.generated.h"

#if !defined(OPENXR_SUPPORTED)
class FOpenXRHMD;
#endif

DECLARE_LOG_CATEGORY_EXTERN(OpenXRExpansionFunctionLibraryLog, Log, All);

// This needs to be updated as the original gets changed, that or hope they make the original blueprint accessible.
UENUM(Blueprintable)
enum class EBPOpenXRControllerDeviceType : uint8
{
	DT_SimpleController,
	DT_ValveIndexController,
	DT_ViveController,
	DT_ViveProController,
	//DT_CosmosController,
	DT_DaydreamController,
	DT_OculusTouchController,
	DT_OculusGoController,
	DT_MicrosoftMotionController,
	DT_MicrosoftXboxController,
	DT_PicoNeo3Controller,
	DT_UnknownController
};

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class OPENXREXPANSIONPLUGIN_API UOpenXRExpansionFunctionLibrary : public UBlueprintFunctionLibrary
{
	//GENERATED_BODY()
	GENERATED_BODY()

public:
	UOpenXRExpansionFunctionLibrary(const FObjectInitializer& ObjectInitializer);

	~UOpenXRExpansionFunctionLibrary();
public:

	static FOpenXRHMD* GetOpenXRHMD()
	{
#if defined(OPENXR_SUPPORTED)
		static FName SystemName(TEXT("OpenXR"));
		if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
		{
			return static_cast<FOpenXRHMD*>(GEngine->XRSystem.Get());
		}
#endif

		return nullptr;
	}

	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
		static bool GetOpenXRHandPose(FBPOpenXRActionSkeletalData& HandPoseContainer, UOpenXRHandPoseComponent* HandPoseComponent, bool bGetMockUpPose = false)
	{
		FXRMotionControllerData MotionControllerData;

		if (bGetMockUpPose)
		{
			GetMockUpControllerData(MotionControllerData, HandPoseContainer);
			return true;
		}

		UHeadMountedDisplayFunctionLibrary::GetMotionControllerData((UObject*)HandPoseComponent, HandPoseContainer.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left ? EControllerHand::Left : EControllerHand::Right, MotionControllerData);

		if (MotionControllerData.bValid)
		{
			HandPoseContainer.SkeletalTransforms.Empty(MotionControllerData.HandKeyPositions.Num());
			FTransform ParentTrans = FTransform::Identity;

			if (MotionControllerData.DeviceVisualType == EXRVisualType::Controller)
			{
				ParentTrans = FTransform(MotionControllerData.GripRotation, MotionControllerData.GripPosition, FVector(1.f));
			}
			else // EXRVisualType::Hand visual type
			{
				ParentTrans = FTransform(MotionControllerData.HandKeyRotations[(uint8)EHandKeypoint::Palm], MotionControllerData.HandKeyPositions[(uint8)EHandKeypoint::Palm], FVector(1.f));
			}

			for (int i = 0; i < MotionControllerData.HandKeyPositions.Num(); ++i)
			{
				// Convert to component space, we convert then to parent space later when applying it
				HandPoseContainer.SkeletalTransforms.Add(FTransform(MotionControllerData.HandKeyRotations[i].GetNormalized(), MotionControllerData.HandKeyPositions[i], FVector(1.f)).GetRelativeTransform(ParentTrans));
			}

			//if (bGetCurlValues)
			{
				GetFingerCurlValues(HandPoseContainer);
			}

			HandPoseContainer.bHasValidData = true;
			return true;
		}

		HandPoseContainer.bHasValidData = false;
		return false;
	}

	//UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
	static void GetFingerCurlValues(FBPOpenXRActionSkeletalData& HandPoseContainer)
	{
		// Fail if the count is too low
		if (HandPoseContainer.SkeletalTransforms.Num() < EHandKeypointCount)
			return;

		if (HandPoseContainer.FingerCurls.Num() < 5)
		{
			HandPoseContainer.FingerCurls.AddZeroed(5);
		}

		HandPoseContainer.FingerCurls[0] = GetCurlValueForBoneRoot(HandPoseContainer, EHandKeypoint::ThumbMetacarpal);
		HandPoseContainer.FingerCurls[1] = GetCurlValueForBoneRoot(HandPoseContainer, EHandKeypoint::IndexProximal);
		HandPoseContainer.FingerCurls[2] = GetCurlValueForBoneRoot(HandPoseContainer, EHandKeypoint::MiddleProximal);
		HandPoseContainer.FingerCurls[3] = GetCurlValueForBoneRoot(HandPoseContainer, EHandKeypoint::RingProximal);
		HandPoseContainer.FingerCurls[4] = GetCurlValueForBoneRoot(HandPoseContainer, EHandKeypoint::LittleProximal);
	}

	inline static float GetCurlValueForBoneRoot(FBPOpenXRActionSkeletalData& HandPoseContainer, EHandKeypoint RootBone)
	{
		float Angle1 = 0.0f;
		float Angle2 = 0.0f;
		float Angle1Curl = 0.0f;
		float Angle2Curl = 0.0f;

		if (RootBone == EHandKeypoint::ThumbMetacarpal)
		{
			FVector Prox = HandPoseContainer.SkeletalTransforms[(uint8)RootBone].GetRotation().GetForwardVector();
			FVector Inter = HandPoseContainer.SkeletalTransforms[(uint8)RootBone + 1].GetRotation().GetForwardVector();
			FVector Distal = HandPoseContainer.SkeletalTransforms[(uint8)RootBone + 2].GetRotation().GetForwardVector();

			Prox = FVector::VectorPlaneProject(Prox, FVector::UpVector);
			Inter = FVector::VectorPlaneProject(Inter, FVector::UpVector);
			Distal = FVector::VectorPlaneProject(Distal, FVector::UpVector);

			Angle1 = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Inter, Distal)));
			Angle2 = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Prox, Inter)));

			Angle1Curl = (Angle1 - 10.0f) / 64.0f;
			Angle2Curl = (Angle2 - 20.0f) / 42.0f;
		}
		else
		{
			FVector Prox = HandPoseContainer.SkeletalTransforms[(uint8)RootBone].GetRotation().GetForwardVector();
			FVector Inter = HandPoseContainer.SkeletalTransforms[(uint8)RootBone + 1].GetRotation().GetForwardVector();
			FVector Distal = HandPoseContainer.SkeletalTransforms[(uint8)RootBone + 2].GetRotation().GetForwardVector();


			// We don't use the Y (splay) value, only X and Z plane
			
			Prox = FVector::VectorPlaneProject(Prox, FVector::RightVector);
			Inter = FVector::VectorPlaneProject(Inter, FVector::RightVector);
			Distal = FVector::VectorPlaneProject(Distal, FVector::RightVector);

			Angle1 = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct( Inter, Distal )));
			Angle2 = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct( Prox,  Inter )));

			Angle1Curl = (Angle1 - 10.0f) / 60.0f;
			Angle2Curl = (Angle2 - 10.0f) / 100.0f;
		}

		// Can lower number of variables by doing these

		float FinalAngleAvg = FMath::Clamp((Angle1Curl + Angle2Curl) / 2.0f, 0.0f, 1.0f);

		//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, FString::Printf(TEXT("Finger Curl %f"), IndexCurl));
		return FinalAngleAvg;

	}

	//UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
	static void ConvertHandTransformsSpaceAndBack(TArray<FTransform>& OutTransforms, const TArray<FTransform>& WorldTransforms)
	{
		// Fail if the count is too low
		if (WorldTransforms.Num() < EHandKeypointCount)
			return;

		if (OutTransforms.Num() < WorldTransforms.Num())
		{
			OutTransforms.Empty(WorldTransforms.Num());
			OutTransforms.AddUninitialized(WorldTransforms.Num());
		}

		// Bone/Parent map
		int32 BoneParents[26] =
		{
			// Manually build the parent hierarchy starting at the wrist which has no parent (-1)
			1,	// Palm -> Wrist
			-1,	// Wrist -> None
			1,	// ThumbMetacarpal -> Wrist
			2,	// ThumbProximal -> ThumbMetacarpal
			3,	// ThumbDistal -> ThumbProximal
			4,	// ThumbTip -> ThumbDistal

			1,	// IndexMetacarpal -> Wrist
			6,	// IndexProximal -> IndexMetacarpal
			7,	// IndexIntermediate -> IndexProximal
			8,	// IndexDistal -> IndexIntermediate
			9,	// IndexTip -> IndexDistal

			1,	// MiddleMetacarpal -> Wrist
			11,	// MiddleProximal -> MiddleMetacarpal
			12,	// MiddleIntermediate -> MiddleProximal
			13,	// MiddleDistal -> MiddleIntermediate
			14,	// MiddleTip -> MiddleDistal

			1,	// RingMetacarpal -> Wrist
			16,	// RingProximal -> RingMetacarpal
			17,	// RingIntermediate -> RingProximal
			18,	// RingDistal -> RingIntermediate
			19,	// RingTip -> RingDistal

			1,	// LittleMetacarpal -> Wrist
			21,	// LittleProximal -> LittleMetacarpal
			22,	// LittleIntermediate -> LittleProximal
			23,	// LittleDistal -> LittleIntermediate
			24,	// LittleTip -> LittleDistal
		};

		// Convert transforms to parent space
		// The hand tracking transforms are in world space.
		for (int32 Index = 0; Index < EHandKeypointCount; ++Index)
		{
			const FTransform& BoneTransform = WorldTransforms[Index];
			int32 ParentIndex = BoneParents[Index];
			int32 ParentParent = -1;

			if (ParentIndex > 0)
			{
				ParentParent = BoneParents[ParentIndex];
			}

			if (ParentIndex < 0)
			{
				// We are at the root, so use it.
				OutTransforms[Index] = BoneTransform;
			}
			else
			{
				// Merging missing metacarpal bone into the transform
				if (ParentParent == 1) // Wrist
				{
					OutTransforms[Index] = BoneTransform.GetRelativeTransform(WorldTransforms[ParentParent]);
				}
				else
				{
					OutTransforms[Index] = BoneTransform.GetRelativeTransform(WorldTransforms[ParentIndex]);
				}
			}
		}

		// Check on the easy component space conversion first
		{
			for (int32 Index = 0; Index < EHandKeypointCount; ++Index)
			{
				const FTransform& BoneTransform = WorldTransforms[Index];
				int32 ParentIndex = BoneParents[Index];
				int32 ParentParent = -1;

				if (ParentIndex > 0)
				{
					ParentParent = BoneParents[ParentIndex];
				}

				if (ParentIndex > 0)
				{
					if (ParentParent == 1)
					{
						OutTransforms[Index] = OutTransforms[Index] * OutTransforms[ParentParent];
					}
					else
					{
						OutTransforms[Index] = OutTransforms[Index] * OutTransforms[ParentIndex];
					}
				}
			}
		}
	}

	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
	static void GetMockUpControllerData(FXRMotionControllerData& MotionControllerData, FBPOpenXRActionSkeletalData& SkeletalMappingData, bool bOpenHand = false)
	{


		TArray<FQuat> HandRotationsClosed = {
			// Closed palm
			FQuat(-6.9388939039072284e-18f,-2.7755575615628914e-17f,-5.5511151231257827e-17f,1.0000000181623150),
			FQuat(0.0010158333104005046f,-0.031842494413126823f,0.0082646248419453450f,-0.99945823120983279),
			FQuat(0.49284713488980170f,-0.22607130273287540f,-0.44054329019960731f,0.71548243409346490),
			FQuat(-0.60572821120045273f,-0.017497510794223320f,0.10943807503774633f,-0.78791529705201735),
			FQuat(-0.61281359708005512f,-0.23245447006924613f,-0.058766632856478873f,-0.75297472319193537),
			FQuat(-0.61281359708005512f,-0.23245447006924613f,-0.058766632856478873f,-0.75297472319193537),
			FQuat(-0.11514346379358367f,-0.029229397612833233f,-0.076351329575539195f,0.98997885626789228),
			FQuat(0.079333339113826437f,-0.72590009974051883f,0.050346047334316274f,-0.68135202233808378),
			FQuat(0.0032539550806410245f,-0.97123336736010268f,0.098921247251489333f,0.21658665949113542),
			FQuat(-0.064585069112672477f,-0.57963374972897053f,0.075445998290538954f,0.80880246209406348),
			FQuat(0.064585069112672477f,0.57963374972897053f,-0.075445998290538954f,-0.80880246209406348),
			FQuat(-7.7702472130181111e-08f,9.8656527815210726e-08f,1.3838491007001075e-06f,1.0000000181613493),
			FQuat(0.085300231549708214f,-0.74048187833139134f,0.058532016219761618f,-0.66406663653752407),
			FQuat(0.011595964719175678f,-0.98786834549923641f,0.099110835214894707f,0.11899052159928070),
			FQuat(-0.063530326287074640f,-0.56012988021281451f,0.076145543493668810f,0.82244775363868539),
			FQuat(0.030477923994508188f,0.55662337489051250f,-0.098559802475379960f,-0.82433459003921772),
			FQuat(-0.025910339872061101f,0.052311401725670628f,0.042953782692297438f,0.99737013210455761),
			FQuat(0.11635892750396379f,-0.74717191584145570f,-0.026909776929005647f,-0.65381238012001353),
			FQuat(0.098078041656806447f,-0.98532297068866348f,0.071135591008198620f,0.12024601945419003),
			FQuat(0.0028091467060491482f,-0.52817558741956572f,0.11864668261714340f,0.84080060571895254),
			FQuat(-0.0028091467060491482f,0.52817558741956572f,-0.11864668261714340f,-0.84080060571895254),
			FQuat(-0.11913260527111892f,0.10934177100849747f,0.11664955310670821f,0.97992077106196507),
			FQuat(-0.18696185363314571f,0.78123174637415782f,0.043590203318890380f,0.59398834520762267),
			FQuat(0.15903913486884827f,-0.97287570092949460f,0.091728860868847406f,0.14073122087670015),
			FQuat(0.035141532005084519f,-0.48251853052338572f,0.18910886397987722f,0.85450501128943579),
			FQuat(0.035141532005084519f,-0.48251853052338572f,0.18910886397987722f,0.85450501128943579)
		};
		TArray<FQuat> HandRotationsOpen = {
			// Open Hand
			FQuat(0.167000905f,-0.670308471f,0.304047525f,-0.656011939f),
			FQuat(-0.129862994f,0.736467659f,-0.315623045f,0.584065497f),
			FQuat(-0.030090153f,0.121532254f,-0.840178490f,0.527659237f),
			FQuat(-0.126470163f,-0.262596279f,0.816956878f,-0.497623593f),
			FQuat(-0.102322638f,-0.249194950f,0.821705163f,-0.502227187f),
			FQuat(-0.102322638f,-0.249194950f,0.821705163f,-0.502227187f),
			FQuat(-0.277370781f,0.686735749f,-0.258646101f,0.620130479f),
			FQuat(0.193366051f,-0.808131576f,0.260262072f,-0.491728455f),
			FQuat(0.145547777f,-0.854364336f,0.317562312f,-0.384749293f),
			FQuat(0.107193023f,-0.879853010f,0.321882188f,-0.332806766f),
			FQuat(0.107193023f,-0.879853010f,0.321882188f,-0.332806766f),
			FQuat(0.166999936f,-0.670307159f,0.304047883f,-0.656013489f),
			FQuat(0.206125781f,-0.815250278f,0.203173012f,-0.501596987f),
			FQuat(0.164493740f,-0.890369833f,0.257293820f,-0.337613612f),
			FQuat(0.114019498f,-0.937856555f,0.283619940f,-0.164267495f),
			FQuat(0.107336335f,-0.925720870f,0.321023613f,-0.168710276f),
			FQuat(0.156629071f,-0.719596088f,0.210793152f,-0.642817795f),
			FQuat(0.194258988f,-0.858762920f,0.127883837f,-0.456546605f),
			FQuat(0.166189745f,-0.930981100f,0.161785051f,-0.281922638f),
			FQuat(0.119936436f,-0.970744252f,0.189072192f,-0.086731322f),
			FQuat(0.119936436f,-0.970744252f,0.189072192f,-0.086731322f),
			FQuat(0.160288095f,-0.812664807f,0.100923792f,-0.551087677f),
			FQuat(0.207311243f,-0.870556056f,0.056644741f,-0.442656904f),
			FQuat(0.191506147f,-0.944826961f,0.096772499f,-0.247511998f),
			FQuat(0.116890728f,-0.981477261f,0.138804480f,-0.061412390f),
			FQuat(0.116890728f,-0.981477261f,0.138804480f,-0.061412390f)
		};

		MotionControllerData.HandKeyRotations = /*SkeletalMappingData.TargetHand != EVRSkeletalHandIndex::EActionHandIndex_Left ? HandRotationsOpen :*/ HandRotationsClosed;

		TArray<FVector> HandPositionsClosed = {
			// Closed palm - Left
			FVector(0.0000000000000000f,0.0000000000000000f,0.0000000000000000f),
			FVector(-2.8690212431406792f,0.70708009295073815f,-0.47404338536985718f),
			FVector(-1.1322360817697272f,-2.1125772981974671f,-1.2278703475596775f),
			FVector(0.92697682070144727f,-5.5601677377459957f,-1.6753327187355360f),
			FVector(4.0987554778339428f,-6.0520138168462640f,-2.1960898756852747f),
			FVector(5.5854053809842918f,-5.4247506634349065f,-2.6631245417791525f),
			FVector(-1.6026417502203387f,-1.4646945203797794f,-0.17057236434820122f),
			FVector(5.7352432311721007f,-2.5389617545260998f,0.39061644722637634f),
			FVector(5.4801464829170561f,-3.3344912297783416f,-3.8566611419550343f),
			FVector(2.9179605371815693f,-3.2311985822561073f,-2.6652727318443148f),
			FVector(3.2708935578922342f,-3.0117453368521279f,-1.6311186720587312f),
			FVector(-1.1935619377191149f,-1.8034793735494103e-05f,1.4147048846974153e-06f),
			FVector(5.9028526610092893f,1.3513666817788206e-05f,-4.3170989212359956e-06f),
			FVector(5.4567759872551527f,-0.87968929643487392f,-4.1965100581882382f),
			FVector(2.2252652348065158f,-0.87742006725177069f,-3.4067970851427791f),
			FVector(2.6916877869696085f,-0.62360084690574125f,-2.2285708116727738f),
			FVector(-1.1796822503165614f,1.3653411443775685f,-0.17694011615865479f),
			FVector(5.3502831208188670f,1.9121382570769896f,-0.87930289382919313f),
			FVector(4.8743654830862742f,1.3526757302541959f,-4.8457258101076217f),
			FVector(2.1622015314362244f,0.85068796311660544f,-4.1307752690132205f),
			FVector(2.6021369184528194f,1.0596020074600654f,-3.1860412064934174f),
			FVector(-1.2905361603753163f,2.6108535683555365f,-0.46423293549223010f),
			FVector(4.6820094577722964f,3.8858425146699327f,-1.9880098921962746f),
			FVector(4.0115118130532306f,3.1678700881616777f,-4.8092930847360869f),
			FVector(2.3757993445967389f,2.6579252395479291f,-4.2645319235961239f),
			FVector(2.7329133227289351f,2.8811366857469527f,-3.6179750674182261f)
		};

		// Open Hand
		TArray<FVector> HandPositionsOpen = {
			FVector(-1014.001f,-478.278f,212.902f),
			FVector(-1013.516f,-476.006f,214.688f),
			FVector(-1016.362f,-479.642f,215.119f),
			FVector(-1018.145f,-483.254f,214.805f),
			FVector(-1019.682f,-485.682f,213.284f),
			FVector(-1020.480f,-486.982f,212.581f),
			FVector(-1014.360f,-478.927f,215.169f),
			FVector(-1014.932f,-484.146f,209.902f),
			FVector(-1016.872f,-486.643f,206.852f),
			FVector(-1018.771f,-488.058f,205.231f),
			FVector(-1019.613f,-488.507f,204.655f),
			FVector(-1013.901f,-477.534f,213.831f),
			FVector(-1014.494f,-481.954f,208.310f),
			FVector(-1016.269f,-484.282f,205.146f),
			FVector(-1018.657f,-485.834f,203.427f),
			FVector(-1019.846f,-486.231f,203.113f),
			FVector(-1013.816f,-476.436f,213.006f),
			FVector(-1014.637f,-479.707f,207.344f),
			FVector(-1016.703f,-481.540f,204.355f),
			FVector(-1018.962f,-482.692f,203.000f),
			FVector(-1019.978f,-482.975f,202.870f),
			FVector(-1013.845f,-475.325f,212.363f),
			FVector(-1015.993f,-477.665f,206.928f),
			FVector(-1017.571f,-478.907f,204.670f),
			FVector(-1019.033f,-479.652f,203.887f),
			FVector(-1019.778f,-479.842f,203.819f)
		};

		MotionControllerData.HandKeyPositions = /*SkeletalMappingData.TargetHand != EVRSkeletalHandIndex::EActionHandIndex_Left ? HandPositionsOpen : */HandPositionsClosed;

		if (SkeletalMappingData.TargetHand != EVRSkeletalHandIndex::EActionHandIndex_Left)
		{
			MotionControllerData.GripPosition = FVector(-1018.305f, -478.019f, 209.872f);
			MotionControllerData.GripRotation = FQuat(-0.116352126f, 0.039430488f, -0.757644236f, 0.641001403f);
		}
		else
		{
			MotionControllerData.GripPosition = FVector(-1202.619f, -521.077f, 283.076f);
			MotionControllerData.GripRotation = FQuat(0.040843058f, 0.116659224f, 0.980030060f, -0.155767411f);
		}

		MotionControllerData.DeviceName = TEXT("OpenXR");

		SkeletalMappingData.bHasValidData = true;
		SkeletalMappingData.SkeletalTransforms.Empty(SkeletalMappingData.SkeletalTransforms.Num());
		FTransform ParentTrans = FTransform(MotionControllerData.GripRotation, MotionControllerData.GripPosition, FVector(1.f));
		for (int i = 0; i < MotionControllerData.HandKeyPositions.Num(); i++)
		{
			SkeletalMappingData.SkeletalTransforms.Add(FTransform(MotionControllerData.HandKeyRotations[i], MotionControllerData.HandKeyPositions[i], FVector(1.f)).GetRelativeTransform(ParentTrans));
		}
	}

	// Get a list of all currently tracked devices and their types, index in the array is their device index
	// Returns failed if the openXR query failed (no interaction profile yet or openXR is not running)
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true", ExpandEnumAsExecs = "Result"))
		static void GetXRMotionControllerType(FString& TrackingSystemName, EBPOpenXRControllerDeviceType& DeviceType, EBPXRResultSwitch &Result);
};