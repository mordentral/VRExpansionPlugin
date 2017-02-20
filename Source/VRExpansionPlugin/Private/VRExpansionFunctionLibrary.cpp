// Fill out your copyright notice in the Description page of Project Settings.
#include "VRExpansionPluginPrivatePCH.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#endif

#include "VRExpansionFunctionLibrary.h"

//General Log
DEFINE_LOG_CATEGORY(VRExpansionFunctionLibraryLog);

UVRExpansionFunctionLibrary::UVRExpansionFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

//=============================================================================
UVRExpansionFunctionLibrary::~UVRExpansionFunctionLibrary()
{
#if STEAMVR_SUPPORTED_PLATFORM
	if(bInitialized)
		UnloadOpenVRModule();
#endif
}

bool UVRExpansionFunctionLibrary::GetIsActorMovable(AActor * ActorToCheck)
{
	if (!ActorToCheck)
		return false;

	if (USceneComponent * rootComp = ActorToCheck->GetRootComponent())
	{
		 return rootComp->Mobility == EComponentMobility::Movable;
	}

	return false;
}

void UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName(FName SlotType, AActor * Actor, FVector WorldLocation, float MaxRange, bool & bHadSlotInRange, FTransform & SlotWorldTransform)
{
	bHadSlotInRange = false;
	SlotWorldTransform = FTransform::Identity;

	if (!Actor)
		return;

	if (USceneComponent *rootComp = Actor->GetRootComponent())
	{
		float ClosestSlotDistance = -0.1f;

		TArray<FName> SocketNames = rootComp->GetAllSocketNames();

		FString GripIdentifier = SlotType.ToString();

		int foundIndex = 0;

		for (int i = 0; i < SocketNames.Num(); ++i)
		{
			if (SocketNames[i].ToString().Contains(GripIdentifier, ESearchCase::IgnoreCase, ESearchDir::FromStart))
			{
				float vecLen = (rootComp->GetSocketLocation(SocketNames[i]) - WorldLocation).Size();

				if (MaxRange >= vecLen && (ClosestSlotDistance < 0.0f || vecLen < ClosestSlotDistance))
				{
					ClosestSlotDistance = vecLen;
					bHadSlotInRange = true;
					foundIndex = i;
				}
			}
		}

		if (bHadSlotInRange)
			SlotWorldTransform = rootComp->GetSocketTransform(SocketNames[foundIndex]);
	}
}

void UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(FName SlotType, UPrimitiveComponent * Component, FVector WorldLocation, float MaxRange, bool & bHadSlotInRange, FTransform & SlotWorldTransform)
{
	bHadSlotInRange = false;
	SlotWorldTransform = FTransform::Identity;

	if (!Component)
		return;

	float ClosestSlotDistance = -0.1f;

	TArray<FName> SocketNames = Component->GetAllSocketNames();

	FString GripIdentifier = SlotType.ToString();

	int foundIndex = 0;

	for (int i = 0; i < SocketNames.Num(); ++i)
	{
		if (SocketNames[i].ToString().Contains(GripIdentifier, ESearchCase::IgnoreCase, ESearchDir::FromStart))
		{
			float vecLen = (Component->GetSocketLocation(SocketNames[i]) - WorldLocation).Size();

			if (MaxRange >= vecLen && (ClosestSlotDistance < 0.0f || vecLen < ClosestSlotDistance))
			{
				ClosestSlotDistance = vecLen;
				bHadSlotInRange = true;
				foundIndex = i;
			}
		}
	}

	if (bHadSlotInRange)
		SlotWorldTransform = Component->GetSocketTransform(SocketNames[foundIndex]);
}

FRotator UVRExpansionFunctionLibrary::GetHMDPureYaw(FRotator HMDRotation)
{
	return GetHMDPureYaw_I(HMDRotation);
}

bool UVRExpansionFunctionLibrary::OpenVRHandles()
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else
	if (IsLocallyControlled() && !bInitialized)
		bInitialized = LoadOpenVRModule();
	else if (bInitialized)
		return true;
	else
		bInitialized = false;

	return bInitialized;
#endif
}

bool UVRExpansionFunctionLibrary::CloseVRHandles()
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else
	if (bInitialized)
	{
		UnloadOpenVRModule();
		bInitialized = false;
		return true;
	}
	else
		return false;
#endif
}

bool UVRExpansionFunctionLibrary::LoadOpenVRModule()
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else
#if PLATFORM_WINDOWS
#if PLATFORM_64BITS

	if (!(GEngine->HMDDevice.IsValid() && (GEngine->HMDDevice->GetHMDDeviceType() == EHMDDeviceType::DT_SteamVR)))
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Failed to load OpenVR library, HMD device either not connected or not using SteamVR"));
		return false;
	}

	FString RootOpenVRPath;
	TCHAR VROverridePath[MAX_PATH];
	FPlatformMisc::GetEnvironmentVariable(TEXT("VR_OVERRIDE"), VROverridePath, MAX_PATH);

	if (FCString::Strlen(VROverridePath) > 0)
	{
		RootOpenVRPath = FString::Printf(TEXT("%s\\bin\\win64\\"), VROverridePath);
	}
	else
	{
		RootOpenVRPath = FPaths::EngineDir() / FString::Printf(TEXT("Binaries/ThirdParty/OpenVR/%s/Win64/"), OPENVR_SDK_VER);
	}

	FPlatformProcess::PushDllDirectory(*RootOpenVRPath);
	OpenVRDLLHandle = FPlatformProcess::GetDllHandle(*(RootOpenVRPath + "openvr_api.dll"));
	FPlatformProcess::PopDllDirectory(*RootOpenVRPath);
#else
	FString RootOpenVRPath = FPaths::EngineDir() / FString::Printf(TEXT("Binaries/ThirdParty/OpenVR/%s/Win32/"), OPENVR_SDK_VER);
	FPlatformProcess::PushDllDirectory(*RootOpenVRPath);
	OpenVRDLLHandle = FPlatformProcess::GetDllHandle(*(RootOpenVRPath + "openvr_api.dll"));
	FPlatformProcess::PopDllDirectory(*RootOpenVRPath);
#endif
#elif PLATFORM_MAC
	OpenVRDLLHandle = FPlatformProcess::GetDllHandle(TEXT("libopenvr_api.dylib"));
#endif	//PLATFORM_WINDOWS

	if (!OpenVRDLLHandle)
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Failed to load OpenVR library."));
		return false;
	}

	//@todo steamvr: Remove GetProcAddress() workaround once we update to Steamworks 1.33 or higher
	//VRInitFn = (pVRInit)FPlatformProcess::GetDllExport(OpenVRDLLHandle, TEXT("VR_Init"));
	//VRShutdownFn = (pVRShutdown)FPlatformProcess::GetDllExport(OpenVRDLLHandle, TEXT("VR_Shutdown"));
	//VRIsHmdPresentFn = (pVRIsHmdPresent)FPlatformProcess::GetDllExport(OpenVRDLLHandle, TEXT("VR_IsHmdPresent"));
//VRGetStringForHmdErrorFn = (pVRGetStringForHmdError)FPlatformProcess::GetDllExport(OpenVRDLLHandle, TEXT("VR_GetStringForHmdError"));
	VRGetGenericInterfaceFn = (pVRGetGenericInterface)FPlatformProcess::GetDllExport(OpenVRDLLHandle, TEXT("VR_GetGenericInterface"));

	if (!VRGetGenericInterfaceFn)
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Failed to GetProcAddress() on openvr_api.dll"));
		UnloadOpenVRModule();
		return false;
	}

	// Grab the chaperone
	vr::EVRInitError ChaperoneErr = vr::VRInitError_None;
	//VRChaperone = (vr::IVRChaperone*)vr::VR_GetGenericInterface(vr::IVRChaperone_Version, &ChaperoneErr);
	//VRChaperone = (vr::IVRChaperone*)(*VRGetGenericInterfaceFn)(vr::IVRChaperone_Version, &ChaperoneErr);
	/*if ((VRChaperone != nullptr) && (ChaperoneErr == vr::VRInitError_None))
	{
		//ChaperoneBounds = FChaperoneBounds(VRChaperone);
	}
	else
	{
		VRChaperone = nullptr;
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Failed to initialize Chaperone system"));
	}*/

	return true;
#endif
}

void UVRExpansionFunctionLibrary::UnloadOpenVRModule()
{
#if STEAMVR_SUPPORTED_PLATFORM
	if (OpenVRDLLHandle != nullptr)
	{
		FPlatformProcess::FreeDllHandle(OpenVRDLLHandle);
		OpenVRDLLHandle = nullptr;
		//(*VRShutdownFn)();
	}
#endif
}

EBPHMDWornState UVRExpansionFunctionLibrary::GetIsHMDWorn()
{
	if (GEngine && GEngine->HMDDevice.IsValid())
	{
		return ((EBPHMDWornState)GEngine->HMDDevice->GetHMDWornState());
	}

	return EBPHMDWornState::Unknown;
}

bool UVRExpansionFunctionLibrary::GetIsHMDConnected()
{
	if (GEngine && GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHMDConnected())
		return true;

	return false;
}

EBPHMDDeviceType UVRExpansionFunctionLibrary::GetHMDType()
{
	if (GEngine && GEngine->HMDDevice.IsValid())
	{
		switch (GEngine->HMDDevice->GetHMDDeviceType())
		{
		case EHMDDeviceType::DT_ES2GenericStereoMesh: return EBPHMDDeviceType::DT_ES2GenericStereoMesh; break;
		case EHMDDeviceType::DT_GearVR: return EBPHMDDeviceType::DT_GearVR; break;
		case EHMDDeviceType::DT_Morpheus: return EBPHMDDeviceType::DT_Morpheus; break;
		case EHMDDeviceType::DT_OculusRift: return EBPHMDDeviceType::DT_OculusRift; break;
		case EHMDDeviceType::DT_SteamVR: return EBPHMDDeviceType::DT_SteamVR; break;
	
		// Return unknown if not a matching enum, may need to add new entries in the copied enum if the original adds new ones in this case
		default: return EBPHMDDeviceType::DT_Unknown; break;
		}
	}

	return EBPHMDDeviceType::DT_Unknown;
}


bool UVRExpansionFunctionLibrary::IsInVREditorPreviewOrGame()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine);
		return EdEngine->bUseVRPreviewForPlayWorld;
	}
#endif

	// Is not an editor build, default to true here
	return true;
}

void UVRExpansionFunctionLibrary::NonAuthorityMinimumAreaRectangle(class UObject* WorldContextObject, const TArray<FVector>& InVerts, const FVector& SampleSurfaceNormal, FVector& OutRectCenter, FRotator& OutRectRotation, float& OutSideLengthX, float& OutSideLengthY, bool bDebugDraw)
{
	float MinArea = -1.f;
	float CurrentArea = -1.f;
	FVector SupportVectorA, SupportVectorB;
	FVector RectSideA, RectSideB;
	float MinDotResultA, MinDotResultB, MaxDotResultA, MaxDotResultB;
	FVector TestEdge;
	float TestEdgeDot = 0.f;
	FVector PolyNormal(0.f, 0.f, 1.f);
	TArray<int32> PolyVertIndices;

	// Bail if we receive an empty InVerts array
	if (InVerts.Num() == 0)
	{
		return;
	}

	// Compute the approximate normal of the poly, using the direction of SampleSurfaceNormal for guidance
	PolyNormal = (InVerts[InVerts.Num() / 3] - InVerts[0]) ^ (InVerts[InVerts.Num() * 2 / 3] - InVerts[InVerts.Num() / 3]);
	if ((PolyNormal | SampleSurfaceNormal) < 0.f)
	{
		PolyNormal = -PolyNormal;
	}

	// Transform the sample points to 2D
	FMatrix SurfaceNormalMatrix = FRotationMatrix::MakeFromZX(PolyNormal, FVector(1.f, 0.f, 0.f));
	TArray<FVector> TransformedVerts;
	OutRectCenter = FVector(0.f);
	for (int32 Idx = 0; Idx < InVerts.Num(); ++Idx)
	{
		OutRectCenter += InVerts[Idx];
		TransformedVerts.Add(SurfaceNormalMatrix.InverseTransformVector(InVerts[Idx]));
	}
	OutRectCenter /= InVerts.Num();

	// Compute the convex hull of the sample points
	ConvexHull2D::ComputeConvexHull(TransformedVerts, PolyVertIndices);

	// Minimum area rectangle as computed by http://www.geometrictools.com/Documentation/MinimumAreaRectangle.pdf
	for (int32 Idx = 1; Idx < PolyVertIndices.Num() - 1; ++Idx)
	{
		SupportVectorA = (TransformedVerts[PolyVertIndices[Idx]] - TransformedVerts[PolyVertIndices[Idx - 1]]).GetSafeNormal();
		SupportVectorA.Z = 0.f;
		SupportVectorB.X = -SupportVectorA.Y;
		SupportVectorB.Y = SupportVectorA.X;
		SupportVectorB.Z = 0.f;
		MinDotResultA = MinDotResultB = MaxDotResultA = MaxDotResultB = 0.f;

		for (int TestVertIdx = 1; TestVertIdx < PolyVertIndices.Num(); ++TestVertIdx)
		{
			TestEdge = TransformedVerts[PolyVertIndices[TestVertIdx]] - TransformedVerts[PolyVertIndices[0]];
			TestEdgeDot = SupportVectorA | TestEdge;
			if (TestEdgeDot < MinDotResultA)
			{
				MinDotResultA = TestEdgeDot;
			}
			else if (TestEdgeDot > MaxDotResultA)
			{
				MaxDotResultA = TestEdgeDot;
			}

			TestEdgeDot = SupportVectorB | TestEdge;
			if (TestEdgeDot < MinDotResultB)
			{
				MinDotResultB = TestEdgeDot;
			}
			else if (TestEdgeDot > MaxDotResultB)
			{
				MaxDotResultB = TestEdgeDot;
			}
		}

		CurrentArea = (MaxDotResultA - MinDotResultA) * (MaxDotResultB - MinDotResultB);
		if (MinArea < 0.f || CurrentArea < MinArea)
		{
			MinArea = CurrentArea;
			RectSideA = SupportVectorA * (MaxDotResultA - MinDotResultA);
			RectSideB = SupportVectorB * (MaxDotResultB - MinDotResultB);
		}
	}

	RectSideA = SurfaceNormalMatrix.TransformVector(RectSideA);
	RectSideB = SurfaceNormalMatrix.TransformVector(RectSideB);
	OutRectRotation = FRotationMatrix::MakeFromZX(PolyNormal, RectSideA).Rotator();
	OutSideLengthX = RectSideA.Size();
	OutSideLengthY = RectSideB.Size();

#if ENABLE_DRAW_DEBUG
	if (bDebugDraw)
	{
		UWorld* World = (WorldContextObject) ? GEngine->GetWorldFromContextObject(WorldContextObject) : nullptr;
		if (World != nullptr)
		{
			DrawDebugSphere(World, OutRectCenter, 10.f, 12, FColor::Yellow, true);
			DrawDebugCoordinateSystem(World, OutRectCenter, SurfaceNormalMatrix.Rotator(), 100.f, true);
			DrawDebugLine(World, OutRectCenter - RectSideA * 0.5f + FVector(0, 0, 10.f), OutRectCenter + RectSideA * 0.5f + FVector(0, 0, 10.f), FColor::Green, true, -1, 0, 5.f);
			DrawDebugLine(World, OutRectCenter - RectSideB * 0.5f + FVector(0, 0, 10.f), OutRectCenter + RectSideB * 0.5f + FVector(0, 0, 10.f), FColor::Blue, true, -1, 0, 5.f);
		}
		else
		{
			FFrame::KismetExecutionMessage(TEXT("WorldContext required for MinimumAreaRectangle to draw a debug visualization."), ELogVerbosity::Warning);
		}
	}
#endif
}

bool UVRExpansionFunctionLibrary::GetVRControllerPropertyString(EVRControllerProperty_String PropertyToRetrieve, int32 DeviceID, FString & StringValue)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else

	if (!bInitialized)
		return false;

	if (!(GEngine->HMDDevice.IsValid() && (GEngine->HMDDevice->GetHMDDeviceType() == EHMDDeviceType::DT_SteamVR)))
		return false;

	vr::HmdError HmdErr;
	vr::IVRSystem * VRSystem = (vr::IVRSystem*)(*VRGetGenericInterfaceFn)(vr::IVRSystem_Version, &HmdErr);
	//vr::IVRSystem * VRSystem = (vr::IVRSystem*)vr::VR_GetGenericInterface(vr::IVRSystem_Version, &HmdErr);

	if (!VRSystem)
		return false;

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	char charvalue[vr::k_unMaxPropertyStringSize];
	uint32_t buffersize = 255;
	uint32_t ret = VRSystem->GetStringTrackedDeviceProperty(DeviceID, (vr::ETrackedDeviceProperty) (((int32)PropertyToRetrieve) + 3000), charvalue, buffersize, &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
		return false;

	StringValue = FString(ANSI_TO_TCHAR(charvalue));
	return true;

#endif
}

bool UVRExpansionFunctionLibrary::GetVRDevicePropertyString(EVRDeviceProperty_String PropertyToRetrieve, int32 DeviceID, FString & StringValue)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else

	if (!bInitialized)
		return false;

	if (!(GEngine->HMDDevice.IsValid() && (GEngine->HMDDevice->GetHMDDeviceType() == EHMDDeviceType::DT_SteamVR)))
		return false;

	vr::HmdError HmdErr;
	vr::IVRSystem * VRSystem = (vr::IVRSystem*)(*VRGetGenericInterfaceFn)(vr::IVRSystem_Version, &HmdErr);
	//vr::IVRSystem * VRSystem = (vr::IVRSystem*)vr::VR_GetGenericInterface(vr::IVRSystem_Version, &HmdErr);

	if (!VRSystem)
		return false;

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	char charvalue[vr::k_unMaxPropertyStringSize];
	uint32_t buffersize = 255;
	uint32_t ret = VRSystem->GetStringTrackedDeviceProperty(DeviceID, (vr::ETrackedDeviceProperty) (((int32)PropertyToRetrieve) + 1000), charvalue, buffersize, &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
		return false;

	StringValue = FString(ANSI_TO_TCHAR(charvalue));
	return true;

#endif
}

bool UVRExpansionFunctionLibrary::GetVRDevicePropertyBool(EVRDeviceProperty_Bool PropertyToRetrieve, int32 DeviceID, bool & BoolValue)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else

	if (!bInitialized)
		return false;

	if (!(GEngine->HMDDevice.IsValid() && (GEngine->HMDDevice->GetHMDDeviceType() == EHMDDeviceType::DT_SteamVR)))
		return false;

	vr::HmdError HmdErr;
	vr::IVRSystem * VRSystem = (vr::IVRSystem*)(*VRGetGenericInterfaceFn)(vr::IVRSystem_Version, &HmdErr);
	//vr::IVRSystem * VRSystem = (vr::IVRSystem*)vr::VR_GetGenericInterface(vr::IVRSystem_Version, &HmdErr);

	if (!VRSystem)
		return false;

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	bool ret = VRSystem->GetBoolTrackedDeviceProperty(DeviceID, (vr::ETrackedDeviceProperty) (((int32)PropertyToRetrieve) + 1000), &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
		return false;

	BoolValue = ret;
	return true;

#endif
}

bool UVRExpansionFunctionLibrary::GetVRDevicePropertyFloat(EVRDeviceProperty_Float PropertyToRetrieve, int32 DeviceID, float & FloatValue)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else

	if (!bInitialized)
		return false;

	if (!(GEngine->HMDDevice.IsValid() && (GEngine->HMDDevice->GetHMDDeviceType() == EHMDDeviceType::DT_SteamVR)))
		return false;

	vr::HmdError HmdErr;
	vr::IVRSystem * VRSystem = (vr::IVRSystem*)(*VRGetGenericInterfaceFn)(vr::IVRSystem_Version, &HmdErr);
	//vr::IVRSystem * VRSystem = (vr::IVRSystem*)vr::VR_GetGenericInterface(vr::IVRSystem_Version, &HmdErr);

	if (!VRSystem)
		return false;

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	float ret = VRSystem->GetFloatTrackedDeviceProperty(DeviceID, (vr::ETrackedDeviceProperty) (((int32)PropertyToRetrieve) + 1000), &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
		return false;

	FloatValue = ret;
	return true;

#endif
}

UTexture2D * UVRExpansionFunctionLibrary::GetVRDeviceModelAndTexture(UObject* WorldContextObject, EBPSteamVRTrackedDeviceType DeviceType, TArray<UProceduralMeshComponent *> ProceduralMeshComponentsToFill, bool bCreateCollision, EAsyncBlueprintResultSwitch &Result/*, TArray<uint8> & OutRawTexture, bool bReturnRawTexture*/)
{

#if !STEAMVR_SUPPORTED_PLATFORM
	Result = EAsyncBlueprintResultSwitch::OnFailure;
	return NULL;
	UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
#else

	if (!bInitialized)
	{
		Result = EAsyncBlueprintResultSwitch::OnFailure;
		return NULL;
	}

	/*if (!(GEngine->HMDDevice.IsValid() && (GEngine->HMDDevice->GetHMDDeviceType() == EHMDDeviceType::DT_SteamVR)))
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Get HMD Device!!"));
		bSucceeded = false;
		return nullptr;
	}*/

/*	FSteamVRHMD* SteamVRHMD = (FSteamVRHMD*)(GEngine->HMDDevice.Get());
	if (!SteamVRHMD || !SteamVRHMD->IsStereoEnabled())
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Get HMD Device!!"));
		bSucceeded = false;
		return nullptr;
	}*/

	vr::HmdError HmdErr;
	vr::IVRSystem * VRSystem = (vr::IVRSystem*)(*VRGetGenericInterfaceFn)(vr::IVRSystem_Version, &HmdErr);
	//vr::IVRSystem * VRSystem = (vr::IVRSystem*)vr::VR_GetGenericInterface(vr::IVRSystem_Version, &HmdErr);

	if (!VRSystem)
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("VRSystem InterfaceErrorCode %i"), (int32)HmdErr);
	}

	vr::IVRRenderModels * VRRenderModels = (vr::IVRRenderModels*)(*VRGetGenericInterfaceFn)(vr::IVRRenderModels_Version, &HmdErr);
	//vr::IVRRenderModels * VRRenderModels = (vr::IVRRenderModels*)vr::VR_GetGenericInterface(vr::IVRRenderModels_Version, &HmdErr);

	if (!VRRenderModels)
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Render Models InterfaceErrorCode %i"), (int32)HmdErr);
	}


	if (!VRSystem || !VRRenderModels)
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Get Interfaces!!"));
		Result = EAsyncBlueprintResultSwitch::OnFailure;
		return nullptr;
	}

	TArray<int32> TrackedIDs;

	USteamVRFunctionLibrary::GetValidTrackedDeviceIds((ESteamVRTrackedDeviceType)DeviceType, TrackedIDs);
	if (TrackedIDs.Num() == 0)
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Get Tracked Devices!!"));
		Result = EAsyncBlueprintResultSwitch::OnFailure;
		return nullptr;
	}

	int32 DeviceID = TrackedIDs[0];

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	char RenderModelName[vr::k_unMaxPropertyStringSize];
	uint32_t buffersize = 255;
	uint32_t ret = VRSystem->GetStringTrackedDeviceProperty(DeviceID, vr::ETrackedDeviceProperty::Prop_RenderModelName_String, RenderModelName, buffersize, &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Get Render Model Name String!!"));
		Result = EAsyncBlueprintResultSwitch::OnFailure;
		return nullptr;
	}

	//uint32_t numComponents = VRRenderModels->GetComponentCount("vr_controller_vive_1_5");
	//UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("NumComponents: %i"), (int32)numComponents);
	// if numComponents > 0 load each, otherwise load the main one only

	vr::RenderModel_t *RenderModel = NULL;

	//VRRenderModels->LoadRenderModel()
	vr::EVRRenderModelError ModelErrorCode = VRRenderModels->LoadRenderModel_Async(RenderModelName, &RenderModel);
	
	if (ModelErrorCode != vr::EVRRenderModelError::VRRenderModelError_None)
	{
		if (ModelErrorCode != vr::EVRRenderModelError::VRRenderModelError_Loading)
		{
			UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Load Model!!"));
			Result = EAsyncBlueprintResultSwitch::OnFailure;
		}
		else
			Result = EAsyncBlueprintResultSwitch::AsyncLoading;

		return nullptr;
	}

	if (!RenderModel)
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Load Model!!"));
		Result = EAsyncBlueprintResultSwitch::OnFailure;
		return nullptr;
	}

	vr::TextureID_t texID = RenderModel->diffuseTextureId;
	vr::RenderModel_TextureMap_t * texture = NULL;

	UTexture2D* OutTexture = nullptr;
	vr::EVRRenderModelError TextureErrorCode = VRRenderModels->LoadTexture_Async(texID, &texture);

	if (TextureErrorCode != vr::EVRRenderModelError::VRRenderModelError_None)
	{
		if (TextureErrorCode != vr::EVRRenderModelError::VRRenderModelError_Loading)
		{
			UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Load Texture!!"));
			Result = EAsyncBlueprintResultSwitch::OnFailure;
		}
		else
			Result = EAsyncBlueprintResultSwitch::AsyncLoading;

		return nullptr;
	}

	if (!texture)
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Load Texture!!"));
		Result = EAsyncBlueprintResultSwitch::OnFailure;
		return nullptr;
	}

	if (ProceduralMeshComponentsToFill.Num() > 0)
	{
		TArray<FVector> vertices;
		TArray<int32> triangles;
		TArray<FVector> normals;
		TArray<FVector2D> UV0;
		TArray<FColor> vertexColors;
		TArray<FProcMeshTangent> tangents;
		
		vr::HmdVector3_t vPosition;
		vr::HmdVector3_t vNormal;
		for (uint32_t i = 0; i < RenderModel->unVertexCount; ++i)
		{
			vPosition = RenderModel->rVertexData[i].vPosition;
			vertices.Add(FVector(vPosition.v[2], vPosition.v[1], vPosition.v[0]));

			vNormal = RenderModel->rVertexData[i].vNormal;
			normals.Add(FVector(vNormal.v[2], vNormal.v[1], vNormal.v[0]));

			UV0.Add(FVector2D(RenderModel->rVertexData[i].rfTextureCoord[0], RenderModel->rVertexData[i].rfTextureCoord[1]));
		}

		for (uint32_t i = 0; i < RenderModel->unTriangleCount * 3; i += 3)
		{
			triangles.Add(RenderModel->rIndexData[i]);
			triangles.Add(RenderModel->rIndexData[i + 1]);
			triangles.Add(RenderModel->rIndexData[i + 2]);
		}

		float scale = UHeadMountedDisplayFunctionLibrary::GetWorldToMetersScale(WorldContextObject);
		for (int i = 0; i < ProceduralMeshComponentsToFill.Num(); ++i)
		{
			ProceduralMeshComponentsToFill[i]->ClearAllMeshSections();
			ProceduralMeshComponentsToFill[i]->CreateMeshSection(1, vertices, triangles, normals, UV0, vertexColors, tangents, bCreateCollision);
			ProceduralMeshComponentsToFill[i]->SetMeshSectionVisible(1, true);
			ProceduralMeshComponentsToFill[i]->SetWorldScale3D(FVector(scale, scale, scale));
		}
	}

	uint32 Width = texture->unWidth;
	uint32 Height = texture->unHeight;

	OutTexture = UTexture2D::CreateTransient(Width, Height, PF_R8G8B8A8);

	uint8* MipData = (uint8*)OutTexture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, (void*)texture->rubTextureMapData, Height * Width * 4);
	OutTexture->PlatformData->Mips[0].BulkData.Unlock();

	//Setting some Parameters for the Texture and finally returning it
	OutTexture->PlatformData->NumSlices = 1;
	OutTexture->NeverStream = true;
	OutTexture->UpdateResource();

	/*if (bReturnRawTexture)
	{
		OutRawTexture.AddUninitialized(Height * Width * 4);
		FMemory::Memcpy(OutRawTexture.GetData(), (void*)texture->rubTextureMapData, Height * Width * 4);
	}*/

	Result = EAsyncBlueprintResultSwitch::OnSuccess;
	VRRenderModels->FreeTexture(texture);

	VRRenderModels->FreeRenderModel(RenderModel);
	return OutTexture;
#endif
}

bool UVRExpansionFunctionLibrary::EqualEqual_FBPActorGripInformation(const FBPActorGripInformation &A, const FBPActorGripInformation &B)
{
	return A == B;
}

