// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Grippables/HandSocketComponent.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"

class IDetailLayoutBuilder;

class FHandSocketComponentDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//void SortCategories(const TMap<FName, IDetailCategoryBuilder*>& AllCategoryMap);

	// The selected hand component
	TWeakObjectPtr<UHandSocketComponent> HandSocketComponent;
	FReply OnUpdateSavePose();
	TWeakObjectPtr<UAnimSequence> SaveAnimationAsset(const FString& InAssetPath, const FString& InAssetName);

	void OnLockedStateUpdated(IDetailLayoutBuilder* LayoutBuilder);
	void OnLeftDominantUpdated(IDetailLayoutBuilder* LayoutBuilder);
	void OnHandRelativeUpdated(IDetailLayoutBuilder* LayoutBuilder);
	void OnUpdateShowMesh(IDetailLayoutBuilder* LayoutBuilder);

	FHandSocketComponentDetails()
	{
	}
};

class SCreateHandAnimationDlg : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SCreateHandAnimationDlg)
	{
	}

	SLATE_ARGUMENT(FText, DefaultAssetPath)
		SLATE_END_ARGS()

		SCreateHandAnimationDlg()
		: UserResponse(EAppReturnType::Cancel)
	{
	}

	void Construct(const FArguments& InArgs);

public:
	/** Displays the dialog in a blocking fashion */
	EAppReturnType::Type ShowModal();

	/** Gets the resulting asset path */
	FString GetAssetPath();

	/** Gets the resulting asset name */
	FString GetAssetName();

	/** Gets the resulting full asset path (path+'/'+name) */
	FString GetFullAssetPath();

protected:
	void OnPathChange(const FString& NewPath);
	void OnNameChange(const FText& NewName, ETextCommit::Type CommitInfo);
	FReply OnButtonClick(EAppReturnType::Type ButtonID);

	bool ValidatePackage();

	EAppReturnType::Type UserResponse;
	FText AssetPath;
	FText AssetName;

	static FText LastUsedAssetPath;

};

