// Copyright Epic Games, Inc. All Rights Reserved.

#include "VRGlobalSettingsDetails.h"
#include "VRGlobalSettings.h"
//#include "PropertyEditing.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
//#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "IDetailsView.h"

//#include "Developer/AssetTools/Public/IAssetTools.h"
//#include "Developer/AssetTools/Public/AssetToolsModule.h"
//#include "Editor/ContentBrowser/Public/IContentBrowserSingleton.h"
//#include "Editor/ContentBrowser/Public/ContentBrowserModule.h"
//#include "AnimationUtils.h"

#include "UObject/SavePackage.h"
#include "Misc/MessageDialog.h"
//#include "Widgets/Layout/SBorder.h"
//#include "Widgets/Layout/SSeparator.h"
//#include "Widgets/Layout/SUniformGridPanel.h"
//#include "Widgets/Input/SEditableTextBox.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"

#include "Animation/AnimData/AnimDataModel.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"

#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"

#define LOCTEXT_NAMESPACE "VRGlobalSettingsDetails"


TSharedRef< IDetailCustomization > FVRGlobalSettingsDetails::MakeInstance()
{
    return MakeShareable(new FVRGlobalSettingsDetails);
}

void FVRGlobalSettingsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	//DetailBuilder.HideCategory(FName("ComponentTick"));
	//DetailBuilder.HideCategory(FName("GameplayTags"));


	//TSharedPtr<IPropertyHandle> LockedLocationProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bDecoupleMeshPlacement));

	//FSimpleDelegate OnLockedStateChangedDelegate = FSimpleDelegate::CreateSP(this, &FVRGlobalSettingsDetails::OnLockedStateUpdated, &DetailBuilder);
	//LockedLocationProperty->SetOnPropertyValueChanged(OnLockedStateChangedDelegate);

	DetailBuilder.EditCategory("Utilities")
		.AddCustomRow(LOCTEXT("FixInvalidAnimationAssets", "Fix Invalid 5.2 Animation Assets"))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("FixInvalidAnimationAssets", "Fix Invalid 5.2 Animation Assets"))
		]
	.ValueContent()
		.MaxDesiredWidth(125.f)
		.MinDesiredWidth(125.f)
		[
			SNew(SButton)
			.ContentPadding(2)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.OnClicked(this, &FVRGlobalSettingsDetails::OnCorrectInvalidAnimationAssets)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("FixInvalidAnimationAssetsButton", "Fix Animation Assets"))
		]
		];
}

FReply FVRGlobalSettingsDetails::OnCorrectInvalidAnimationAssets()
{

	// Load the asset registry module
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked< FAssetRegistryModule >(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	TArray< FString > ContentPaths;
	ContentPaths.Add(TEXT("/Game"));
	AssetRegistry.ScanPathsSynchronous(ContentPaths);

	FARFilter Filter;
	Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	//Filter.ClassNames.Add(UAnimSequence::StaticClass()->GetFName());
	Filter.bRecursiveClasses = true;
	//if (!Path.IsEmpty())
	//{
	//	Filter.PackagePaths.Add(*Path);
	//}
	Filter.bRecursivePaths = true;

	TArray< FAssetData > AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	// Iterate over retrieved blueprint assets
	for (auto& Asset : AssetList)
	{
		// Check the anim sequence for invalid data
		if (UAnimSequence* AnimSeq = Cast<UAnimSequence>(Asset.GetAsset()))
		{
			IAnimationDataController& AnimController = AnimSeq->GetController();
			{
				IAnimationDataController::FScopedBracket ScopedBracket(AnimController, LOCTEXT("FixAnimationAsset_VRE", "Fixing invalid anim sequences"));
				const IAnimationDataModel* AnimModel = AnimController.GetModel();

				FFrameRate FrameRate = AnimModel->GetFrameRate();
				//int32 NumFrames = AnimModel->GetNumberOfFrames();
				double FrameRateD = FrameRate.AsDecimal();

				// I was saving with a below 1.0 frame rate and 1 frame
				if (FrameRateD < 1.0f)
				{
					// We have an invalid frame rate for 5.2
					AnimController.SetFrameRate(FFrameRate(1, 1));
					AnimSeq->MarkPackageDirty();

					UPackage* const Package = AnimSeq->GetOutermost();
					FString const PackageName = Package->GetName();
					FString const PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

					double StartTime = FPlatformTime::Seconds();

					FSavePackageArgs PackageArguments;
					PackageArguments.SaveFlags = RF_Standalone;
					PackageArguments.SaveFlags = SAVE_NoError;
					UPackage::SavePackage(Package, NULL, *PackageFileName, PackageArguments);
					//UPackage::SavePackage(Package, NULL, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_NoError);

					double ElapsedTime = FPlatformTime::Seconds() - StartTime;
					UE_LOG(LogAnimation, Log, TEXT("Animation re-saved %s in %0.2f seconds"), *PackageName, ElapsedTime);
				}
	
			}
		}
	}


	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
