// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AnimGraphNode_ApplyOpenXRHandPose.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimGraphNode_ApplyOpenXRHandPose)

/////////////////////////////////////////////////////
// UAnimGraphNode_ModifyBSHand

UAnimGraphNode_ApplyOpenXRHandPose::UAnimGraphNode_ApplyOpenXRHandPose(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
}

//Title Color!
FLinearColor UAnimGraphNode_ApplyOpenXRHandPose::GetNodeTitleColor() const
{
	return FLinearColor(12, 12, 0, 1);
}

//Node Category
FString UAnimGraphNode_ApplyOpenXRHandPose::GetNodeCategory() const
{
	return FString("OpenXR");
}
FText UAnimGraphNode_ApplyOpenXRHandPose::GetControllerDescription() const
{
	return FText::FromString("Apply OpenXR Hand Pose");
}

FText UAnimGraphNode_ApplyOpenXRHandPose::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	FText Result = GetControllerDescription();
	return Result;
}