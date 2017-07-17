// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "OpenVRExpansionPlugin.h"
#include "OpenVRExpansionFunctionLibrary.h"

#define LOCTEXT_NAMESPACE "FVRExpansionPluginModule"

void FOpenVRExpansionPluginModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	//LoadOpenVRModule();
}

void FOpenVRExpansionPluginModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
//	UnloadOpenVRModule();
}

/*bool FOpenVRExpansionPluginModule::LoadOpenVRModule()
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else
#if PLATFORM_WINDOWS
#if PLATFORM_64BITS

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

#elif PLATFORM_LINUX
	FString RootOpenVRPath = FPaths::EngineDir() / FString::Printf(TEXT("Binaries/ThirdParty/OpenVR/%s/linux64/"), OPENVR_SDK_VER);
	OpenVRDLLHandle = FPlatformProcess::GetDllHandle(*(RootOpenVRPath + "libopenvr_api.so"));
#else
	#error "SteamVRHMD is not supported for this platform."
#endif	//PLATFORM_WINDOWS

	if (!OpenVRDLLHandle)
	{
		//UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Failed to load OpenVR library."));
		return false;
	}

	//@todo steamvr: Remove GetProcAddress() workaround once we update to Steamworks 1.33 or higher
	//VRInitFn = (pVRInit)FPlatformProcess::GetDllExport(OpenVRDLLHandle, TEXT("VR_Init"));
	//VRShutdownFn = (pVRShutdown)FPlatformProcess::GetDllExport(OpenVRDLLHandle, TEXT("VR_Shutdown"));
	//VRIsHmdPresentFn = (pVRIsHmdPresent)FPlatformProcess::GetDllExport(OpenVRDLLHandle, TEXT("VR_IsHmdPresent"));
	//VRGetStringForHmdErrorFn = (pVRGetStringForHmdError)FPlatformProcess::GetDllExport(OpenVRDLLHandle, TEXT("VR_GetStringForHmdError"));
	UOpenVRExpansionFunctionLibrary::VRGetGenericInterfaceFn = (pVRGetGenericInterface)FPlatformProcess::GetDllExport(OpenVRDLLHandle, TEXT("VR_GetGenericInterface"));

	if (!UOpenVRExpansionFunctionLibrary::VRGetGenericInterfaceFn)
	{
		//UE_LOG(OpenVRExpansionFunctionLibraryLog, Warning, TEXT("Failed to GetProcAddress() on openvr_api.dll"));
		UnloadOpenVRModule();
		return false;
	}
	
	return true;
#endif
}*/

/*void FOpenVRExpansionPluginModule::UnloadOpenVRModule()
{
#if STEAMVR_SUPPORTED_PLATFORM
	if (OpenVRDLLHandle != nullptr)
	{
		FPlatformProcess::FreeDllHandle(OpenVRDLLHandle);
		OpenVRDLLHandle = nullptr;
		//(*VRShutdownFn)();
	}

	UOpenVRExpansionFunctionLibrary::VRGetGenericInterfaceFn = nullptr;
#endif
}*/

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FOpenVRExpansionPluginModule, OpenVRExpansionPlugin)