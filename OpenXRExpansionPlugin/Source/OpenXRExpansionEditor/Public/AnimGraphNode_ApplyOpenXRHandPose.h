#pragma once

#include "AnimGraphDefinitions.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Editor/AnimGraph/Public/AnimGraphNode_SkeletalControlBase.h"

#include "AnimNode_ApplyOpenXRHandPose.h"
// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_ApplyOpenXRHandPose.generated.h"

UCLASS(MinimalAPI)
class UAnimGraphNode_ApplyOpenXRHandPose : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_ApplyOpenXRHandPose Node;

public:
	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FString GetNodeCategory() const override;
	// End of UEdGraphNode interface

protected:

	// UAnimGraphNode_SkeletalControlBase protected interface
	virtual FText GetControllerDescription() const;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
	// End of UAnimGraphNode_SkeletalControlBase protected interface

};