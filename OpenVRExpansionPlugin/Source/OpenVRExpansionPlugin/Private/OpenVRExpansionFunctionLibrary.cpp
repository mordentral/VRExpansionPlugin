// Fill out your copyright notice in the Description page of Project Settings.
#include "OpenVRExpansionFunctionLibrary.h"
//#include "EngineMinimal.h"
#include "Engine/Engine.h"
#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Rendering/Texture2DResource.h"
#include "RenderUtils.h"
#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#endif

//General Log
DEFINE_LOG_CATEGORY(OpenVRExpansionFunctionLibraryLog);

#if STEAMVR_SUPPORTED_PLATFORM

//pVRGetGenericInterface UOpenVRExpansionFunctionLibrary::VRGetGenericInterfaceFn = nullptr;
FBPOpenVRCameraHandle UOpenVRExpansionFunctionLibrary::OpenCamera = FBPOpenVRCameraHandle();
#endif

UOpenVRExpansionFunctionLibrary::UOpenVRExpansionFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

//=============================================================================
UOpenVRExpansionFunctionLibrary::~UOpenVRExpansionFunctionLibrary()
{
#if STEAMVR_SUPPORTED_PLATFORM
	//if(VRGetGenericInterfaceFn)
	//	UnloadOpenVRModule();
#endif
}

EBPOpenVRHMDDeviceType UOpenVRExpansionFunctionLibrary::GetOpenVRHMDType()
{

	EBPOpenVRHMDDeviceType DeviceType = EBPOpenVRHMDDeviceType::DT_Unknown;

	if (GEngine && GEngine->XRSystem.IsValid())
	{
		// Already declared in some of our includes here
		//static const FName SteamVRSysName(TEXT("SteamVR"));
		static const FName OculusSystemName(TEXT("OculusHMD"));
		//static const FName OSVRSystemName(TEXT("OSVR"));

		FName DeviceName(NAME_None);
		DeviceName = GEngine->XRSystem->GetSystemName();


		if (DeviceName == SteamVRSystemName)
			DeviceType = EBPOpenVRHMDDeviceType::DT_SteamVR;
		else if (DeviceName == OculusSystemName)
			DeviceType = EBPOpenVRHMDDeviceType::DT_OculusHMD;
		//else if (DeviceName == OSVRSystemName)
			//DeviceType = EBPOpenVRHMDDeviceType::DT_OSVR;
	}

#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Get OpenVRHMDType returning default value as this is not a steamvr supported platform!!!"));
	return DeviceType;
#else

	// If not, will early out with the type (OSVR / OculusHMD)
	if (DeviceType == EBPOpenVRHMDDeviceType::DT_SteamVR)
	{
		FString DeviceModelNumber;
		EBPOVRResultSwitch Result;

		// Using index 0 as it is always HMD
		UOpenVRExpansionFunctionLibrary::GetVRDevicePropertyString(EVRDeviceProperty_String::Prop_ModelNumber_String_1001, vr::k_unTrackedDeviceIndex_Hmd, DeviceModelNumber, Result);
		if (Result == EBPOVRResultSwitch::OnSucceeded)
		{
			//UE_LOG(OpenVRExpansionFunctionLibraryLog, Display, TEXT("OpenVRDeviceType - Prop_ModelNumber_String_1001: %s"), *DeviceModelNumber);

			//#TODO: Screw this eventually, need it to be actual string value comparisons for each model
			// This is the hacky workaround for now
			/*
				"Vive MV";
				"Vive Cosmos"
				"VIVE_Pro MV"
				"Oculus Rift CV1";
				"Lenovo Explorer";
				"HP Windows Mixed Reality Headset";
				"Samsung Windows Mixed Reality 800ZAA";
				"Acer AH100";
			*/

			// Manufacturer name
			/*WindowsMR*/

			if (DeviceModelNumber.Find("index", ESearchCase::IgnoreCase) != INDEX_NONE)
			{
				DeviceType = EBPOpenVRHMDDeviceType::DT_ValveIndex;
			}
			else if (DeviceModelNumber.Find("vive_cosmos", ESearchCase::IgnoreCase) != INDEX_NONE)
			{
				DeviceType = EBPOpenVRHMDDeviceType::DT_ViveCosmos;
			}
			else if (DeviceModelNumber.Find("vive", ESearchCase::IgnoreCase) != INDEX_NONE)
			{
				DeviceType = EBPOpenVRHMDDeviceType::DT_Vive;
			}
			else if ((DeviceModelNumber.Find("oculus quest", ESearchCase::IgnoreCase) != INDEX_NONE) ||
					(DeviceModelNumber.Find("miramar", ESearchCase::IgnoreCase) != INDEX_NONE))
			{
				DeviceType = EBPOpenVRHMDDeviceType::DT_OculusQuestHMD;
			}
			else if (DeviceModelNumber.Find("oculus", ESearchCase::IgnoreCase) != INDEX_NONE)
			{
				DeviceType = EBPOpenVRHMDDeviceType::DT_OculusHMD;
			}
			else if (
				DeviceModelNumber.Find("Mixed Reality", ESearchCase::IgnoreCase) != INDEX_NONE ||
				DeviceModelNumber.Find("Acer", ESearchCase::IgnoreCase) != INDEX_NONE ||
				DeviceModelNumber.Find("Lenovo", ESearchCase::IgnoreCase) != INDEX_NONE
				)
			{
				DeviceType = EBPOpenVRHMDDeviceType::DT_WindowsMR;
			}	
			else
			{
				// Check for manufacturer name for windows MR
				UOpenVRExpansionFunctionLibrary::GetVRDevicePropertyString(EVRDeviceProperty_String::Prop_ManufacturerName_String_1005, vr::k_unTrackedDeviceIndex_Hmd, DeviceModelNumber, Result);
				if (Result == EBPOVRResultSwitch::OnSucceeded)
				{
					if (DeviceModelNumber.Find("WindowsMR", ESearchCase::IgnoreCase) != INDEX_NONE)
					{
						DeviceType = EBPOpenVRHMDDeviceType::DT_WindowsMR;
					}
				}
				else
				{
					DeviceType = EBPOpenVRHMDDeviceType::DT_Unknown;
#if WITH_EDITOR
					UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Tell VRE about unhandled HMD model type: %s !!!"), *DeviceModelNumber);
#endif
				}
			}
		}
		else
		{
			UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Get OpenVRHMDType failed to get the OpenVR property string!!!"));
		}
	}

	return DeviceType;
#endif
}

EBPOpenVRControllerDeviceType UOpenVRExpansionFunctionLibrary::GetOpenVRControllerType()
{

	EBPOpenVRControllerDeviceType DeviceType = EBPOpenVRControllerDeviceType::DT_UnknownController;

#if !STEAMVR_SUPPORTED_PLATFORM
	return DeviceType;
#else

	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		return DeviceType;
	}

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		return DeviceType;
	}

	int32 DeviceIndexOut = INDEX_NONE;
	int32 FallbackIndex = INDEX_NONE;

	for (uint32 DeviceIndex = 0; DeviceIndex < vr::k_unMaxTrackedDeviceCount; ++DeviceIndex)
	{
		const vr::ETrackedDeviceClass DeviceClass = VRSystem->GetTrackedDeviceClass(DeviceIndex);
		if (DeviceClass == vr::ETrackedDeviceClass::TrackedDeviceClass_Controller)
		{
			// NOTE: GetControllerRoleForTrackedDeviceIndex() only seems to return a valid role if the device is on and being tracked
			const vr::ETrackedControllerRole ControllerRole = VRSystem->GetControllerRoleForTrackedDeviceIndex(DeviceIndex);
			if (ControllerRole != vr::TrackedControllerRole_LeftHand && ControllerRole != vr::TrackedControllerRole_RightHand)
			{
				continue;
			}

			DeviceIndexOut = DeviceIndex;
			break;
		}
	}

	if (DeviceIndexOut == INDEX_NONE)
	{
		return DeviceType;
	}

	EBPOVRResultSwitch Result;

	FString DeviceModelNumber;
	UOpenVRExpansionFunctionLibrary::GetVRDevicePropertyString(EVRDeviceProperty_String::Prop_ModelNumber_String_1001, DeviceIndexOut, DeviceModelNumber, Result);
	if (Result == EBPOVRResultSwitch::OnSucceeded)
	{
		if (DeviceModelNumber.Find("knuckles", ESearchCase::IgnoreCase) != INDEX_NONE || DeviceModelNumber.Find("index", ESearchCase::IgnoreCase) != INDEX_NONE)
		{
			DeviceType = EBPOpenVRControllerDeviceType::DT_IndexController;
		}
		else if (DeviceModelNumber.Find("cosmos", ESearchCase::IgnoreCase) != INDEX_NONE)
		{
			DeviceType = EBPOpenVRControllerDeviceType::DT_CosmosController;
		}
		else if (DeviceModelNumber.Find("VIVE", ESearchCase::IgnoreCase) != INDEX_NONE) // Vive Wand
		{
			DeviceType = EBPOpenVRControllerDeviceType::DT_ViveController;
		}
		else if ((DeviceModelNumber.Find("oculus quest", ESearchCase::IgnoreCase) != INDEX_NONE) ||
				(DeviceModelNumber.Find("miramar", ESearchCase::IgnoreCase) != INDEX_NONE))
		{
			DeviceType = EBPOpenVRControllerDeviceType::DT_QuestController;
		}
		else if (DeviceModelNumber.Find("oculus rift cv1", ESearchCase::IgnoreCase) != INDEX_NONE) // Oculus Rift CV1
		{
			DeviceType = EBPOpenVRControllerDeviceType::DT_RiftController;
		}		
		else if (DeviceModelNumber.Find("oculus rift s", ESearchCase::IgnoreCase) != INDEX_NONE) // Oculus Rift CV1
		{
			DeviceType = EBPOpenVRControllerDeviceType::DT_RiftSController;
		}
		else if (DeviceModelNumber.Find("windowsmr", ESearchCase::IgnoreCase) != INDEX_NONE) // Oculus Rift CV1
		{
			DeviceType = EBPOpenVRControllerDeviceType::DT_WMRController;
		}
		else
		{
#if WITH_EDITOR
			UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Tell VRE about unhandled controller model type: %s !!!"), *DeviceModelNumber);
			// Add other controllers here
#endif
		}
	}

	return DeviceType;
#endif
}

bool UOpenVRExpansionFunctionLibrary::HasVRCamera(EOpenVRCameraFrameType FrameType, int32 &Width, int32 &Height)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else
	Width = 0;
	Height = 0;

	//if (!VRGetGenericInterfaceFn)
	//	return false;

	// Don't run anything if no HMD and if the HMD is not a steam type
	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
		return false;

	vr::IVRTrackedCamera* VRCamera = vr::VRTrackedCamera();


	if (!VRCamera)
		return false;

	bool pHasCamera;
	vr::EVRTrackedCameraError CamError = VRCamera->HasCamera(vr::k_unTrackedDeviceIndex_Hmd, &pHasCamera);

	if (CamError != vr::EVRTrackedCameraError::VRTrackedCameraError_None)
		return false;

	uint32 WidthOut;
	uint32 HeightOut;
	uint32 FrameBufferSize;
	CamError = VRCamera->GetCameraFrameSize(vr::k_unTrackedDeviceIndex_Hmd, (vr::EVRTrackedCameraFrameType)FrameType, &WidthOut, &HeightOut, &FrameBufferSize);

	Width = WidthOut;
	Height = HeightOut;

	return pHasCamera;

#endif
}

void UOpenVRExpansionFunctionLibrary::AcquireVRCamera(FBPOpenVRCameraHandle & CameraHandle, EBPOVRResultSwitch & Result)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	Result = EBPOVRResultSwitch::OnFailed;
	return;
#else

	// Don't run anything if no HMD and if the HMD is not a steam type
	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	// If already have a valid camera handle
	if (OpenCamera.IsValid())
	{
		CameraHandle = OpenCamera;
		Result = EBPOVRResultSwitch::OnSucceeded;
		return;
	}

	vr::IVRTrackedCamera* VRCamera = vr::VRTrackedCamera();

	if (!VRCamera)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::EVRTrackedCameraError CamError = VRCamera->AcquireVideoStreamingService(vr::k_unTrackedDeviceIndex_Hmd, &CameraHandle.pCameraHandle);

	if (CamError != vr::EVRTrackedCameraError::VRTrackedCameraError_None)
		CameraHandle.pCameraHandle = INVALID_TRACKED_CAMERA_HANDLE;

	if (CameraHandle.pCameraHandle == INVALID_TRACKED_CAMERA_HANDLE)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	OpenCamera = CameraHandle;
	Result = EBPOVRResultSwitch::OnSucceeded;
	return;
#endif
}

void UOpenVRExpansionFunctionLibrary::ReleaseVRCamera(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle, EBPOVRResultSwitch & Result)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	Result = EBPOVRResultSwitch::OnFailed;
	return;
#else

	// Don't run anything if no HMD and if the HMD is not a steam type
	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	if (!CameraHandle.IsValid())
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}


	vr::IVRTrackedCamera* VRCamera = vr::VRTrackedCamera();

	if (!VRCamera)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::EVRTrackedCameraError CamError = VRCamera->ReleaseVideoStreamingService(CameraHandle.pCameraHandle);
	CameraHandle.pCameraHandle = INVALID_TRACKED_CAMERA_HANDLE;

	OpenCamera = CameraHandle;
	Result = EBPOVRResultSwitch::OnSucceeded;
	return;

#endif
}

bool UOpenVRExpansionFunctionLibrary::IsValid(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle)
{
	return (CameraHandle.IsValid());
}


UTexture2D * UOpenVRExpansionFunctionLibrary::CreateCameraTexture2D(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle, EOpenVRCameraFrameType FrameType, EBPOVRResultSwitch & Result)
{
#if !STEAMVR_SUPPORTED_PLATFORM 
	Result = EBPOVRResultSwitch::OnFailed;
	return nullptr;
#else

	// Don't run anything if no HMD and if the HMD is not a steam type
	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return nullptr;
	}

	if (!CameraHandle.IsValid() || !FApp::CanEverRender())
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return nullptr;
	}

	vr::IVRTrackedCamera* VRCamera = vr::VRTrackedCamera();

	if (!VRCamera)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return nullptr;
	}

	uint32 Width;
	uint32 Height;
	uint32 FrameBufferSize;
	vr::EVRTrackedCameraError CamError = VRCamera->GetCameraFrameSize(vr::k_unTrackedDeviceIndex_Hmd, (vr::EVRTrackedCameraFrameType)FrameType, &Width, &Height, &FrameBufferSize);

	if (Width > 0 && Height > 0)
	{

		UTexture2D * NewRenderTarget2D = UTexture2D::CreateTransient(Width, Height, EPixelFormat::PF_R8G8B8A8);//NewObject<UTexture2D>(GetWorld());
		check(NewRenderTarget2D);

		//Setting some Parameters for the Texture and finally returning it
		NewRenderTarget2D->PlatformData->SetNumSlices(1);
		NewRenderTarget2D->NeverStream = true;
		NewRenderTarget2D->UpdateResource();

		Result = EBPOVRResultSwitch::OnSucceeded;
		return NewRenderTarget2D;
	}

	Result = EBPOVRResultSwitch::OnFailed;
	return nullptr;
#endif
}

void UOpenVRExpansionFunctionLibrary::GetVRCameraFrame(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle, EOpenVRCameraFrameType FrameType, EBPOVRResultSwitch & Result, UTexture2D * TargetRenderTarget)
{
#if !STEAMVR_SUPPORTED_PLATFORM 
	Result = EBPOVRResultSwitch::OnFailed;
	return;
#else

	// Don't run anything if no HMD and if the HMD is not a steam type
	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	if (!TargetRenderTarget || !CameraHandle.IsValid())
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::IVRTrackedCamera* VRCamera = vr::VRTrackedCamera();

	if (!VRCamera)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	uint32 Width;
	uint32 Height;
	uint32 FrameBufferSize;
	vr::EVRTrackedCameraError CamError = VRCamera->GetCameraFrameSize(vr::k_unTrackedDeviceIndex_Hmd, (vr::EVRTrackedCameraFrameType)FrameType, &Width, &Height, &FrameBufferSize);

	if (CamError != vr::EVRTrackedCameraError::VRTrackedCameraError_None || Width <= 0 || Height <= 0)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	// Make sure formats are correct
	check(FrameBufferSize == (Width * Height * GPixelFormats[EPixelFormat::PF_R8G8B8A8].BlockBytes));


	// Need to bring this back after moving from render target to this
	// Update the format if required, this is in case someone made a new render target NOT with my custom function
	// Enforces correct buffer size for the camera feed
	if (TargetRenderTarget->GetSizeX() != Width || TargetRenderTarget->GetSizeY() != Height || TargetRenderTarget->GetPixelFormat() != EPixelFormat::PF_R8G8B8A8)
	{
		TargetRenderTarget->PlatformData->SizeX = Width;
		TargetRenderTarget->PlatformData->SizeY = Height;
		TargetRenderTarget->PlatformData->PixelFormat = EPixelFormat::PF_R8G8B8A8;
		
		// Allocate first mipmap.
		int32 NumBlocksX = Width / GPixelFormats[EPixelFormat::PF_R8G8B8A8].BlockSizeX;
		int32 NumBlocksY = Height / GPixelFormats[EPixelFormat::PF_R8G8B8A8].BlockSizeY;

		TargetRenderTarget->PlatformData->Mips[0].SizeX = Width;
		TargetRenderTarget->PlatformData->Mips[0].SizeY = Height;
		TargetRenderTarget->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		TargetRenderTarget->PlatformData->Mips[0].BulkData.Realloc(NumBlocksX * NumBlocksY * GPixelFormats[EPixelFormat::PF_R8G8B8A8].BlockBytes);
		TargetRenderTarget->PlatformData->Mips[0].BulkData.Unlock();
	}
	
	vr::CameraVideoStreamFrameHeader_t CamHeader;
	uint8* pData = new uint8[FrameBufferSize];

	CamError = VRCamera->GetVideoStreamFrameBuffer(CameraHandle.pCameraHandle, (vr::EVRTrackedCameraFrameType)FrameType, pData, FrameBufferSize, &CamHeader, sizeof(vr::CameraVideoStreamFrameHeader_t));

	// No frame available = still on spin / wake up
	if (CamError != vr::EVRTrackedCameraError::VRTrackedCameraError_None || CamError == vr::EVRTrackedCameraError::VRTrackedCameraError_NoFrameAvailable)
	{
		delete[] pData;
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	/*ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
		UpdateDynamicTextureCode,
		UTexture2D*, pTexture, TargetRenderTarget,
		const uint8*, pData, pData,
		{
			FUpdateTextureRegion2D region;
			region.SrcX = 0;
			region.SrcY = 0;
			region.DestX = 0;
			region.DestY = 0;
			region.Width = pTexture->GetSizeX();// TEX_WIDTH;
			region.Height = pTexture->GetSizeY();//TEX_HEIGHT;

			FTexture2DResource* resource = (FTexture2DResource*)pTexture->Resource;
			RHIUpdateTexture2D(resource->GetTexture2DRHI(), 0, region, region.Width * GPixelFormats[pTexture->GetPixelFormat()].BlockBytes, pData);
			delete[] pData;
		});*/

	UTexture2D* TexturePtr = TargetRenderTarget;
	const uint8* TextureData = pData;
	ENQUEUE_RENDER_COMMAND(OpenVRExpansionPlugin_GetVRCameraFrame)(
		[TexturePtr, TextureData](FRHICommandList& RHICmdList)
	{
		FUpdateTextureRegion2D region;
		region.SrcX = 0;
		region.SrcY = 0;
		region.DestX = 0;
		region.DestY = 0;
		region.Width = TexturePtr->GetSizeX();// TEX_WIDTH;
		region.Height = TexturePtr->GetSizeY();//TEX_HEIGHT;

		FTexture2DResource* resource = (FTexture2DResource*)TexturePtr->Resource;
		RHIUpdateTexture2D(resource->GetTexture2DRHI(), 0, region, region.Width * GPixelFormats[TexturePtr->GetPixelFormat()].BlockBytes/*TEX_PIXEL_SIZE_IN_BYTES*/, TextureData);
		delete[] TextureData;
	});

	// Letting the enqueued command free the memory
	Result = EBPOVRResultSwitch::OnSucceeded;
	return;
#endif
}

void UOpenVRExpansionFunctionLibrary::GetVRDevicePropertyString(EVRDeviceProperty_String PropertyToRetrieve, int32 DeviceID, FString & StringValue, EBPOVRResultSwitch & Result)
{

#if !STEAMVR_SUPPORTED_PLATFORM
	Result = EBPOVRResultSwitch::OnFailed;
	return;
#else

	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	vr::ETrackedDeviceProperty EnumPropertyValue = VREnumToString(TEXT("EVRDeviceProperty_String"), static_cast<uint8>(PropertyToRetrieve));
	if (EnumPropertyValue == vr::ETrackedDeviceProperty::Prop_Invalid)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	char charvalue[vr::k_unMaxPropertyStringSize];
	uint32_t buffersize = vr::k_unMaxPropertyStringSize;
	uint32_t ret = VRSystem->GetStringTrackedDeviceProperty(DeviceID, EnumPropertyValue, charvalue, buffersize, &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	StringValue = FString(ANSI_TO_TCHAR(charvalue));
	Result = EBPOVRResultSwitch::OnSucceeded;
	return;

#endif
}

void UOpenVRExpansionFunctionLibrary::GetVRDevicePropertyBool(EVRDeviceProperty_Bool PropertyToRetrieve, int32 DeviceID, bool & BoolValue, EBPOVRResultSwitch & Result)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	Result = EBPOVRResultSwitch::OnFailed;
	return;
#else

	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	vr::ETrackedDeviceProperty EnumPropertyValue = VREnumToString(TEXT("EVRDeviceProperty_Bool"), static_cast<uint8>(PropertyToRetrieve));
	if (EnumPropertyValue == vr::ETrackedDeviceProperty::Prop_Invalid)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	bool ret = VRSystem->GetBoolTrackedDeviceProperty(DeviceID, EnumPropertyValue, &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	BoolValue = ret;
	Result = EBPOVRResultSwitch::OnSucceeded;
	return;

#endif
}

void UOpenVRExpansionFunctionLibrary::GetVRDevicePropertyFloat(EVRDeviceProperty_Float PropertyToRetrieve, int32 DeviceID, float & FloatValue, EBPOVRResultSwitch & Result)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	Result = EBPOVRResultSwitch::OnFailed;
	return;
#else

	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	vr::ETrackedDeviceProperty EnumPropertyValue = VREnumToString(TEXT("EVRDeviceProperty_Float"), static_cast<uint8>(PropertyToRetrieve));
	if (EnumPropertyValue == vr::ETrackedDeviceProperty::Prop_Invalid)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	float ret = VRSystem->GetFloatTrackedDeviceProperty(DeviceID, EnumPropertyValue, &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	FloatValue = ret;
	Result = EBPOVRResultSwitch::OnSucceeded;
	return;

#endif
}

void UOpenVRExpansionFunctionLibrary::GetVRDevicePropertyInt32(EVRDeviceProperty_Int32 PropertyToRetrieve, int32 DeviceID, int32 & IntValue, EBPOVRResultSwitch & Result)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	Result = EBPOVRResultSwitch::OnFailed;
	return;
#else

	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	vr::ETrackedDeviceProperty EnumPropertyValue = VREnumToString(TEXT("EVRDeviceProperty_Int32"), static_cast<uint8>(PropertyToRetrieve));
	if (EnumPropertyValue == vr::ETrackedDeviceProperty::Prop_Invalid)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	int32 ret = VRSystem->GetInt32TrackedDeviceProperty(DeviceID, EnumPropertyValue, &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	IntValue = ret;
	Result = EBPOVRResultSwitch::OnSucceeded;
	return;

#endif
}

void UOpenVRExpansionFunctionLibrary::GetVRDevicePropertyUInt64(EVRDeviceProperty_UInt64 PropertyToRetrieve, int32 DeviceID, FString & UInt64Value, EBPOVRResultSwitch & Result)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	Result = EBPOVRResultSwitch::OnFailed;
	return;
#else

	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	vr::ETrackedDeviceProperty EnumPropertyValue = VREnumToString(TEXT("EVRDeviceProperty_UInt64"), static_cast<uint8>(PropertyToRetrieve));
	if (EnumPropertyValue == vr::ETrackedDeviceProperty::Prop_Invalid)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	uint64 ret = VRSystem->GetUint64TrackedDeviceProperty(DeviceID, EnumPropertyValue, &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	UInt64Value = FString::Printf(TEXT("%llu"), ret);
	Result = EBPOVRResultSwitch::OnSucceeded;
	return;

#endif
}

void UOpenVRExpansionFunctionLibrary::GetVRDevicePropertyMatrix34AsTransform(EVRDeviceProperty_Matrix34 PropertyToRetrieve, int32 DeviceID, FTransform & TransformValue, EBPOVRResultSwitch & Result)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	Result = EBPOVRResultSwitch::OnFailed;
	return;
#else

	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

	vr::ETrackedDeviceProperty EnumPropertyValue = VREnumToString(TEXT("EVRDeviceProperty_Matrix34"), static_cast<uint8>(PropertyToRetrieve));
	if (EnumPropertyValue == vr::ETrackedDeviceProperty::Prop_Invalid)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	vr::HmdMatrix34_t ret = VRSystem->GetMatrix34TrackedDeviceProperty(DeviceID, EnumPropertyValue, &pError);

	if (pError != vr::TrackedPropertyError::TrackedProp_Success)
	{
		Result = EBPOVRResultSwitch::OnFailed;
		return;
	}

	TransformValue = FTransform(ToFMatrix(ret));
	Result = EBPOVRResultSwitch::OnSucceeded;
	return;

#endif
}


EBPOpenVRTrackedDeviceClass UOpenVRExpansionFunctionLibrary::GetOpenVRDeviceType(int32 OpenVRDeviceIndex)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return EBPOpenVRTrackedDeviceClass::TrackedDeviceClass_Invalid;
#else

	if (OpenVRDeviceIndex < 0 || OpenVRDeviceIndex > (vr::k_unMaxTrackedDeviceCount - 1))
		return EBPOpenVRTrackedDeviceClass::TrackedDeviceClass_Invalid;

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VRSystem InterfaceErrored in GetOpenVRDevices"));
		return EBPOpenVRTrackedDeviceClass::TrackedDeviceClass_Invalid;
	}

	return (EBPOpenVRTrackedDeviceClass)VRSystem->GetTrackedDeviceClass(OpenVRDeviceIndex);
#endif
}

void UOpenVRExpansionFunctionLibrary::GetOpenVRDevices(TArray<EBPOpenVRTrackedDeviceClass> &FoundDevices)
{
#if !STEAMVR_SUPPORTED_PLATFORM
#else

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VRSystem InterfaceErrored in GetOpenVRDevices"));
		return;
	}

	vr::ETrackedDeviceClass DeviceClass = vr::ETrackedDeviceClass::TrackedDeviceClass_Invalid;
	for (vr::TrackedDeviceIndex_t deviceIndex = vr::k_unTrackedDeviceIndex_Hmd; deviceIndex < vr::k_unMaxTrackedDeviceCount; ++deviceIndex)
	{
		DeviceClass = VRSystem->GetTrackedDeviceClass(deviceIndex);

		if (VRSystem->GetTrackedDeviceClass(deviceIndex) != vr::ETrackedDeviceClass::TrackedDeviceClass_Invalid)
			FoundDevices.Add((EBPOpenVRTrackedDeviceClass)DeviceClass);
	}
#endif
}

void UOpenVRExpansionFunctionLibrary::GetOpenVRDevicesByType(EBPOpenVRTrackedDeviceClass TypeToRetreive, TArray<int32> &FoundIndexs)
{
#if !STEAMVR_SUPPORTED_PLATFORM
#else

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VRSystem InterfaceErrored in GetOpenVRDevices"));
		return;
	}

	for (vr::TrackedDeviceIndex_t deviceIndex = vr::k_unTrackedDeviceIndex_Hmd; deviceIndex < vr::k_unMaxTrackedDeviceCount; ++deviceIndex)
	{
		if (VRSystem->GetTrackedDeviceClass(deviceIndex) == (vr::ETrackedDeviceClass)TypeToRetreive)
			FoundIndexs.Add(deviceIndex);
	}
#endif
}

bool UOpenVRExpansionFunctionLibrary::IsOpenVRDeviceConnected(int32 OpenVRDeviceIndex)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VRSystem InterfaceErrored in IsOpenVRDeviceConnected"));
		return false;
	}

	return VRSystem->IsTrackedDeviceConnected(OpenVRDeviceIndex);

#endif
}

UTexture2D * UOpenVRExpansionFunctionLibrary::GetVRDeviceModelAndTexture(UObject* WorldContextObject, FString RenderModelNameOverride, FString& RenderModelNameOut, EBPOpenVRTrackedDeviceClass DeviceType, TArray<UProceduralMeshComponent *> ProceduralMeshComponentsToFill, bool bCreateCollision, EAsyncBlueprintResultSwitch &Result, int32 OverrideDeviceID)
{

#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
	Result = EAsyncBlueprintResultSwitch::OnFailure;
	return NULL;
#else

	vr::IVRSystem* VRSystem = vr::VRSystem();

	if (!VRSystem)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VRSystem InterfaceErrored"));
	}

	vr::IVRRenderModels* VRRenderModels = vr::VRRenderModels();

	if (!VRRenderModels)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Render Models Errored"));
	}


	if (!VRSystem || !VRRenderModels)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Get Interfaces!!"));
		Result = EAsyncBlueprintResultSwitch::OnFailure;
		return nullptr;
	}

	char RenderModelName[vr::k_unMaxPropertyStringSize];
	FMemory::Memzero(&RenderModelName, vr::k_unMaxPropertyStringSize);

	if (!RenderModelNameOverride.IsEmpty())
	{
		int len = RenderModelNameOverride.Len();
		FMemory::Memcpy(&RenderModelName, TCHAR_TO_ANSI(*RenderModelNameOverride), len > vr::k_unMaxPropertyStringSize ? vr::k_unMaxPropertyStringSize : len);
		RenderModelNameOut = RenderModelNameOverride;
	}
	else
	{
		int32 DeviceID = 0;
		if (OverrideDeviceID != -1)
		{
			DeviceID = (uint32)OverrideDeviceID;
			if (OverrideDeviceID > (vr::k_unMaxTrackedDeviceCount - 1) || VRSystem->GetTrackedDeviceClass(DeviceID) == vr::k_unTrackedDeviceIndexInvalid)
			{
				UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Override Tracked Device Was Missing!!"));
				Result = EAsyncBlueprintResultSwitch::OnFailure;
				return nullptr;
			}
		}
		else
		{
			TArray<int32> FoundIDs;
			UOpenVRExpansionFunctionLibrary::GetOpenVRDevicesByType(DeviceType, FoundIDs);

			if (FoundIDs.Num() == 0)
			{
				UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Get Tracked Devices!!"));
				Result = EAsyncBlueprintResultSwitch::OnFailure;
				return nullptr;
			}

			DeviceID = FoundIDs[0];
		}

		vr::TrackedPropertyError pError = vr::TrackedPropertyError::TrackedProp_Success;

		uint32_t buffersize = vr::k_unMaxPropertyStringSize;
		uint32_t ret = VRSystem->GetStringTrackedDeviceProperty(DeviceID, vr::ETrackedDeviceProperty::Prop_RenderModelName_String, RenderModelName, buffersize, &pError);

		if (pError != vr::TrackedPropertyError::TrackedProp_Success)
		{
			UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Get Render Model Name String!!"));
			Result = EAsyncBlueprintResultSwitch::OnFailure;
			return nullptr;

		}

		RenderModelNameOut = FString(ANSI_TO_TCHAR(RenderModelName));
	}

	//uint32_t numComponents = VRRenderModels->GetComponentCount("vr_controller_vive_1_5");
	//UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("NumComponents: %i"), (int32)numComponents);
	// if numComponents > 0 load each, otherwise load the main one only

	vr::RenderModel_t *RenderModel = NULL;

	//VRRenderModels->LoadRenderModel()
	vr::EVRRenderModelError ModelErrorCode = VRRenderModels->LoadRenderModel_Async(RenderModelName, &RenderModel);
	
	if (ModelErrorCode != vr::EVRRenderModelError::VRRenderModelError_None)
	{
		if (ModelErrorCode != vr::EVRRenderModelError::VRRenderModelError_Loading)
		{
			UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Load Model!!"));
			Result = EAsyncBlueprintResultSwitch::OnFailure;
		}
		else
			Result = EAsyncBlueprintResultSwitch::AsyncLoading;

		return nullptr;
	}

	if (!RenderModel)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Load Model!!"));
		Result = EAsyncBlueprintResultSwitch::OnFailure;
		return nullptr;
	}

	vr::TextureID_t texID = RenderModel->diffuseTextureId;
	vr::RenderModel_TextureMap_t * texture = NULL;
	UTexture2D* OutTexture = nullptr;

	if (texID != vr::INVALID_TEXTURE_ID)
	{
		//UTexture2DDynamic * OutTexture = nullptr;
		vr::EVRRenderModelError TextureErrorCode = VRRenderModels->LoadTexture_Async(texID, &texture);

		if (TextureErrorCode != vr::EVRRenderModelError::VRRenderModelError_None)
		{
			if (TextureErrorCode != vr::EVRRenderModelError::VRRenderModelError_Loading)
			{
				UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Load Texture!!"));
				Result = EAsyncBlueprintResultSwitch::OnFailure;
			}
			else
				Result = EAsyncBlueprintResultSwitch::AsyncLoading;

			return nullptr;
		}

		if (!texture)
		{
			UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Couldn't Load Texture!!"));
			Result = EAsyncBlueprintResultSwitch::OnFailure;
			return nullptr;
		}
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

		vertices.Reserve(RenderModel->unVertexCount);
		normals.Reserve(RenderModel->unVertexCount);
		UV0.Reserve(RenderModel->unVertexCount);
	
		for (uint32_t i = 0; i < RenderModel->unVertexCount; ++i)
		{
			vPosition = RenderModel->rVertexData[i].vPosition;
			// OpenVR y+ Up, +x Right, -z Going away
			// UE4 z+ up, +y right, +x forward

			vertices.Add(FVector(-vPosition.v[2], vPosition.v[0], vPosition.v[1]));

			vNormal = RenderModel->rVertexData[i].vNormal;

			normals.Add(FVector(-vNormal.v[2], vNormal.v[0], vNormal.v[1]));

			UV0.Add(FVector2D(RenderModel->rVertexData[i].rfTextureCoord[0], RenderModel->rVertexData[i].rfTextureCoord[1]));
		}

		triangles.Reserve(RenderModel->unTriangleCount);
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
			ProceduralMeshComponentsToFill[i]->CreateMeshSection(0, vertices, triangles, normals, UV0, vertexColors, tangents, bCreateCollision);
			ProceduralMeshComponentsToFill[i]->SetMeshSectionVisible(0, true);
			ProceduralMeshComponentsToFill[i]->SetWorldScale3D(FVector(scale, scale, scale));
		}
	}

	if (texture != nullptr)
	{
		uint32 Width = texture->unWidth;
		uint32 Height = texture->unHeight;


		//OutTexture = UTexture2DDynamic::Create(Width, Height, PF_R8G8B8A8);
		OutTexture = UTexture2D::CreateTransient(Width, Height, PF_R8G8B8A8);


		//FTexture2DDynamicResource * TexResource = static_cast<FTexture2DDynamicResource*>(OutTexture->Resource);
		//FTexture2DRHIParamRef TextureRHI = TexResource->GetTexture2DRHI();

		//uint32 DestStride = 0;
		//uint8* MipData = reinterpret_cast<uint8*>(RHILockTexture2D(TextureRHI, 0, RLM_WriteOnly, DestStride, false, false));

		uint8* MipData = (uint8*)OutTexture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(MipData, (void*)texture->rubTextureMapData, Height * Width * 4);
		OutTexture->PlatformData->Mips[0].BulkData.Unlock();
		//RHIUnlockTexture2D(TextureRHI, 0, false, false);


		//Setting some Parameters for the Texture and finally returning it
		OutTexture->PlatformData->SetNumSlices(1);
		OutTexture->NeverStream = true;
		OutTexture->UpdateResource();

		/*if (bReturnRawTexture)
		{
			OutRawTexture.AddUninitialized(Height * Width * 4);
			FMemory::Memcpy(OutRawTexture.GetData(), (void*)texture->rubTextureMapData, Height * Width * 4);
		}*/

		Result = EAsyncBlueprintResultSwitch::OnSuccess;
		VRRenderModels->FreeTexture(texture);
	}

	VRRenderModels->FreeRenderModel(RenderModel);
	return OutTexture;
#endif
}


bool UOpenVRExpansionFunctionLibrary::SetSkyboxOverride_LatLongStereoPair(UTexture2D * LatLongSkyboxL, UTexture2D * LatLongSkyboxR)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
	return false;
#else

	if (!LatLongSkyboxL || !LatLongSkyboxR)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Bad texture passed in to SetSkyBoxOverride"));
		return false;
	}

	vr::IVRCompositor* VRCompositor = vr::VRCompositor();

	if (!VRCompositor)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor InterfaceErrored"));
		return false;
	}

	vr::Texture_t TextureArray[2];

	TextureArray[0] = CreateOpenVRTexture_t(LatLongSkyboxL);
	TextureArray[1] = CreateOpenVRTexture_t(LatLongSkyboxR);

	vr::EVRCompositorError CompositorError;
	CompositorError = VRCompositor->SetSkyboxOverride(TextureArray, 2);

	if (CompositorError != vr::VRCompositorError_None)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor Error %i"), (int32)CompositorError);
		return false;
	}

	return true;

#endif
}

bool UOpenVRExpansionFunctionLibrary::SetSkyboxOverride_LatLong(UTexture2D * LatLongSkybox)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
	return false;
#else
	if (!LatLongSkybox)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Bad texture passed in to SetSkyBoxOverride"));
		return false;
	}

	vr::IVRCompositor* VRCompositor = vr::VRCompositor();

	if (!VRCompositor)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor InterfaceErrored"));
		return false;
	}

	vr::Texture_t Texture;

	Texture = CreateOpenVRTexture_t(LatLongSkybox);

	vr::EVRCompositorError CompositorError;
	CompositorError = VRCompositor->SetSkyboxOverride(&Texture, 1);

	if (CompositorError != vr::VRCompositorError_None)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor Error %i"), (int32)CompositorError);
		return false;
	}

	return true;
#endif
}


bool UOpenVRExpansionFunctionLibrary::SetSkyboxOverride(UTexture * tFront, UTexture2D * tBack, UTexture * tLeft, UTexture * tRight, UTexture * tTop, UTexture * tBottom)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
	return false;
#else
	if (!tFront || !tBack || !tLeft || !tRight || !tTop || !tBottom)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Bad texture passed in to SetSkyBoxOverride"));
		return false;
	}

	vr::IVRCompositor* VRCompositor = vr::VRCompositor();

	if (!VRCompositor)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor InterfaceErrored"));
		return false;
	}

	vr::Texture_t TextureArray[6];

	TextureArray[0] = CreateOpenVRTexture_t(tFront);
	TextureArray[1] = CreateOpenVRTexture_t(tBack);
	TextureArray[2] = CreateOpenVRTexture_t(tLeft);
	TextureArray[3] = CreateOpenVRTexture_t(tRight);
	TextureArray[4] = CreateOpenVRTexture_t(tTop);
	TextureArray[5] = CreateOpenVRTexture_t(tBottom);

	vr::EVRCompositorError CompositorError;
	CompositorError = VRCompositor->SetSkyboxOverride(TextureArray, 6);

	if (CompositorError != vr::VRCompositorError_None)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor Error %i"), (int32)CompositorError);
		return false;
	}

	return true;
#endif
}

bool UOpenVRExpansionFunctionLibrary::ClearSkyboxOverride()
{
#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
	return false;
#else
	vr::IVRCompositor* VRCompositor = vr::VRCompositor();

	if (!VRCompositor)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor InterfaceErrored"));
		return false;
	}

	VRCompositor->ClearSkyboxOverride();

	return true;

#endif
}

bool UOpenVRExpansionFunctionLibrary::FadeHMDToColor(float fSeconds, FColor Color, bool bBackground)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
	return false;
#else
	vr::IVRCompositor* VRCompositor = vr::VRCompositor();

	if (!VRCompositor)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor InterfaceErrored"));
		return false;
	}

	VRCompositor->FadeToColor(fSeconds, Color.R, Color.G, Color.B, Color.A, bBackground);

	return true;

#endif
}

bool UOpenVRExpansionFunctionLibrary::GetCurrentHMDFadeColor(FColor & ColorOut, bool bBackground)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
	return false;
#else
	vr::IVRCompositor* VRCompositor = vr::VRCompositor();

	if (!VRCompositor)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor InterfaceErrored"));
		return false;
	}

	vr::HmdColor_t HMDColor = VRCompositor->GetCurrentFadeColor(bBackground);

	ColorOut = FColor(HMDColor.r, HMDColor.g, HMDColor.b, HMDColor.a);
	return true;

#endif
}

bool UOpenVRExpansionFunctionLibrary::FadeVRGrid(float fSeconds, bool bFadeIn)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
	return false;
#else
	vr::IVRCompositor* VRCompositor = vr::VRCompositor();

	if (!VRCompositor)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor InterfaceErrored"));
		return false;
	}

	VRCompositor->FadeGrid(fSeconds, bFadeIn);
	return true;

#endif
}

bool UOpenVRExpansionFunctionLibrary::GetCurrentVRGridAlpha(float & VRGridAlpha)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
	return false;
#else
	vr::IVRCompositor* VRCompositor = vr::VRCompositor();

	if (!VRCompositor)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor InterfaceErrored"));
		return false;
	}

	VRGridAlpha = VRCompositor->GetCurrentGridAlpha();
	return true;

#endif
}

bool UOpenVRExpansionFunctionLibrary::SetSuspendRendering(bool bSuspendRendering)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Not SteamVR Supported Platform!!"));
	return false;
#else
	vr::IVRCompositor* VRCompositor = vr::VRCompositor();

	if (!VRCompositor)
	{
		UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("VR Compositor InterfaceErrored"));
		return false;
	}

	VRCompositor->SuspendRendering(bSuspendRendering);
	return true;

#endif
}