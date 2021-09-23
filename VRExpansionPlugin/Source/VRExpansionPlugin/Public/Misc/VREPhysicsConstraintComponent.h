#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

// Delete this eventually when the physics interface is fixed
#if PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"
#endif // WITH_PHYSX

#include "VREPhysicsConstraintComponent.generated.h"

/**
*	A custom constraint component subclass that exposes additional missing functionality from the default one
*/
UCLASS(ClassGroup = Physics, meta = (BlueprintSpawnableComponent), HideCategories = (Activation, "Components|Activation", Physics, Mobility), ShowCategories = ("Physics|Components|PhysicsConstraint", "VRE Constraint Settings"))
class VREXPANSIONPLUGIN_API UVREPhysicsConstraintComponent : public UPhysicsConstraintComponent
{
	
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "VRE Physics Constraint Component")
		void SetConstraintToForceBased(bool bUseForceConstraint)
	{
#if PHYSICS_INTERFACE_PHYSX
		// This is a temp workaround until epic fixes the drive creation to allow force constraints
		// I wanted to use the new interface and not directly set the drive so that it is ready to delete this section
		// When its fixed
		//#if PHYSICS_INTERFACE_PHYSX


		if (!ConstraintInstance.ConstraintHandle.ConstraintData)
			return;

		PxD6JointDriveFlags JointFlags;
		if (!bUseForceConstraint)
			JointFlags = PxD6JointDriveFlag::eACCELERATION;


		PxD6JointDrive driveVal = ConstraintInstance.ConstraintHandle.ConstraintData->getDrive(PxD6Drive::Enum::eX);
		driveVal.flags = JointFlags;
		ConstraintInstance.ConstraintHandle.ConstraintData->setDrive(PxD6Drive::Enum::eX, driveVal);

		driveVal = ConstraintInstance.ConstraintHandle.ConstraintData->getDrive(PxD6Drive::Enum::eY);
		driveVal.flags = JointFlags;
		ConstraintInstance.ConstraintHandle.ConstraintData->setDrive(PxD6Drive::Enum::eY, driveVal);

		driveVal = ConstraintInstance.ConstraintHandle.ConstraintData->getDrive(PxD6Drive::Enum::eZ);
		driveVal.flags = JointFlags;
		ConstraintInstance.ConstraintHandle.ConstraintData->setDrive(PxD6Drive::Enum::eZ, driveVal);

		// Check if slerp
		if (ConstraintInstance.ProfileInstance.AngularDrive.AngularDriveMode == EAngularDriveMode::SLERP)
		{
			driveVal = ConstraintInstance.ConstraintHandle.ConstraintData->getDrive(PxD6Drive::Enum::eSLERP);
			driveVal.flags = JointFlags;
			ConstraintInstance.ConstraintHandle.ConstraintData->setDrive(PxD6Drive::Enum::eSLERP, driveVal);
		}
		else
		{
			driveVal = ConstraintInstance.ConstraintHandle.ConstraintData->getDrive(PxD6Drive::Enum::eSWING);
			driveVal.flags = JointFlags;
			ConstraintInstance.ConstraintHandle.ConstraintData->setDrive(PxD6Drive::Enum::eSWING, driveVal);

			driveVal = ConstraintInstance.ConstraintHandle.ConstraintData->getDrive(PxD6Drive::Enum::eTWIST);
			driveVal.flags = JointFlags;
			ConstraintInstance.ConstraintHandle.ConstraintData->setDrive(PxD6Drive::Enum::eTWIST, driveVal);
		}

#elif WITH_CHAOS

		if (!ConstraintInstance.ConstraintHandle.IsValid())
			return;

		if (ConstraintInstance.ConstraintHandle->IsType(Chaos::EConstraintType::JointConstraintType))
		{
			if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(ConstraintInstance.ConstraintHandle.Constraint))
			{
				Constraint->SetLinearDriveForceMode(bUseForceConstraint ? Chaos::EJointForceMode::Force : Chaos::EJointForceMode::Acceleration);
				Constraint->SetAngularDriveForceMode(bUseForceConstraint ? Chaos::EJointForceMode::Force : Chaos::EJointForceMode::Acceleration);
			}
		}

#endif
		//#endif
	}


	UFUNCTION(BlueprintCallable, Category = "VRE Physics Constraint Component")
	void GetConstraintReferenceFrame(EConstraintFrame::Type Frame, FTransform& RefFrame)
	{
		RefFrame = ConstraintInstance.GetRefFrame(Frame);
	}

	UFUNCTION(BlueprintCallable, Category = "VRE Physics Constraint Component")
	FTransform GetLocalPose(EConstraintFrame::Type ConstraintFrame)
	{
		if (ConstraintInstance.IsValidConstraintInstance())
		{
			if (ConstraintFrame == EConstraintFrame::Frame1)
			{
				return FTransform(ConstraintInstance.PriAxis1, ConstraintInstance.SecAxis1, ConstraintInstance.PriAxis1 ^ ConstraintInstance.SecAxis1, ConstraintInstance.Pos1);
			}
			else
			{
				return FTransform(ConstraintInstance.PriAxis2, ConstraintInstance.SecAxis2, ConstraintInstance.PriAxis2 ^ ConstraintInstance.SecAxis2, ConstraintInstance.Pos2);
			}

		}

		return FTransform::Identity;
	}

	UFUNCTION(BlueprintCallable, Category = "VRE Physics Constraint Component")
	void GetGlobalPose(EConstraintFrame::Type ConstraintFrame, FTransform& GlobalPose)
	{
		if (ConstraintInstance.IsValidConstraintInstance())
		{
			GlobalPose = FPhysicsInterface::GetGlobalPose(ConstraintInstance.ConstraintHandle, ConstraintFrame);
		}
		else
			GlobalPose = FTransform::Identity;
	}

	// Gets the current linear distance in world space on the joint in +/- from the initial reference frame
	UFUNCTION(BlueprintPure, Category = "VRE Physics Constraint Component")
		FVector GetCurrentLinearDistance(EConstraintFrame::Type FrameOfReference)
	{
		EConstraintFrame::Type Frame2 = FrameOfReference;
		EConstraintFrame::Type Frame1 = (FrameOfReference == EConstraintFrame::Frame1) ? EConstraintFrame::Frame2 : EConstraintFrame::Frame1;

		FTransform Frame1Trans = this->GetBodyTransform(Frame1);
		FTransform Frame2Trans = this->GetBodyTransform(Frame2);

		FTransform LocalPose = GetLocalPose(Frame1);		
		FTransform LocalPose2 = GetLocalPose(Frame2);

		Frame1Trans.SetScale3D(FVector(1.f));
		Frame1Trans = LocalPose * Frame1Trans;

		FVector OffsetLoc = Frame1Trans.GetRotation().UnrotateVector(Frame1Trans.GetLocation() - Frame2Trans.GetLocation());
		FVector OffsetLoc2 = LocalPose2.GetRotation().UnrotateVector(LocalPose2.GetLocation());
		FVector FinalVec = OffsetLoc2 - OffsetLoc;

		return FinalVec;
	}

	// Gets the angular offset on the constraint
	UFUNCTION(BlueprintPure, Category = "VRE Physics Constraint Component")
		FRotator GetAngularOffset()
	{
		return ConstraintInstance.AngularRotationOffset;
	}

	// Sets the angular offset on the constraint and re-initializes it
	UFUNCTION(BlueprintCallable, Category="VRE Physics Constraint Component")
	void SetAngularOffset(FRotator NewAngularOffset)
	{

		// If the constraint is broken then there is no reason to do everything below
		// Just early out of it.
		if (!ConstraintInstance.IsValidConstraintInstance() || ConstraintInstance.IsBroken())
		{
			ConstraintInstance.AngularRotationOffset = NewAngularOffset;
			return;
		}

		// I could remove a full step if I calc delta in Frame2 local and then apply to the new
		// Values. However I am keeping it like this for now, would require an extra inverse / relative calc, this may not even be slower

		FVector RefPos = ConstraintInstance.Pos2;
		const float RefScale = FMath::Max(GetConstraintScale(), 0.01f);
		if (GetBodyInstance(EConstraintFrame::Frame2))
		{
			RefPos *= RefScale;
		}
		
		FQuat AngRotOffset = ConstraintInstance.AngularRotationOffset.Quaternion();
		FQuat newAngRotOffset = NewAngularOffset.Quaternion();

		FTransform A2Transform = GetBodyTransform(EConstraintFrame::Frame2);
		A2Transform.RemoveScaling();

		FTransform CurrentLocalFrame(ConstraintInstance.PriAxis2, ConstraintInstance.SecAxis2, ConstraintInstance.PriAxis2 ^ ConstraintInstance.SecAxis2, ConstraintInstance.Pos2);
		FTransform WorldLocalFrame = (CurrentLocalFrame * A2Transform);
	
		FVector WPri21 = GetComponentTransform().TransformVectorNoScale(AngRotOffset.GetForwardVector());
		FVector WOrth21 = GetComponentTransform().TransformVectorNoScale(AngRotOffset.GetRightVector());

		FTransform OriginalRotOffset(WPri21, WOrth21, WPri21 ^ WOrth21, FVector::ZeroVector);
		FQuat DeltaRot = WorldLocalFrame.GetRotation() * OriginalRotOffset.GetRotation().Inverse();
		DeltaRot.Normalize();

		FVector WPri2 = GetComponentTransform().TransformVectorNoScale(newAngRotOffset.GetForwardVector());
		FVector WOrth2 = GetComponentTransform().TransformVectorNoScale(newAngRotOffset.GetRightVector());

		WPri2 = DeltaRot.RotateVector(WPri2);
		WOrth2 = DeltaRot.RotateVector(WOrth2);

		ConstraintInstance.PriAxis2 = A2Transform.InverseTransformVectorNoScale(WPri2);
		ConstraintInstance.SecAxis2 = A2Transform.InverseTransformVectorNoScale(WOrth2);
		ConstraintInstance.AngularRotationOffset = NewAngularOffset;

		FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(ConstraintInstance.ConstraintHandle, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
			{
				FTransform URefTransform = FTransform(ConstraintInstance.PriAxis2, ConstraintInstance.SecAxis2, ConstraintInstance.PriAxis2 ^ ConstraintInstance.SecAxis2, RefPos);
				FPhysicsInterface::SetLocalPose(InUnbrokenConstraint, URefTransform, EConstraintFrame::Frame2);
			});

		return;
	}

	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRE Constraint Settings")
	//bool bSetAndMaintainCOMOnFrame2;

	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRE Constraint Settings")
	//	bool bUseForceConstraint;
};