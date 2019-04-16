// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Misc/BucketUpdateSubsystem.h"

	bool UBucketUpdateSubsystem::AddObjectToBucket(int32 UpdateHTZ, UObject* InObject, FName FunctionName)
	{
		if (!InObject || UpdateHTZ < 1)
			return false;

		return BucketContainer.AddBucketObject(UpdateHTZ, InObject, FunctionName);
	}

	bool UBucketUpdateSubsystem::K2_AddObjectToBucket(int32 UpdateHTZ, UObject* InObject, FName FunctionName)
	{
		if (!InObject || UpdateHTZ < 1)
			return false;

		return BucketContainer.AddBucketObject(UpdateHTZ, InObject, FunctionName);
	}


	bool UBucketUpdateSubsystem::K2_AddObjectEventToBucket(FDynamicBucketUpdateTickSignature Delegate, int32 UpdateHTZ)
	{
		if (!Delegate.IsBound())
			return false;

		return BucketContainer.AddBucketObject(UpdateHTZ, Delegate);
	}

	bool UBucketUpdateSubsystem::RemoveObjectFromBucketByFunctionName(UObject* InObject, FName FunctionName)
	{
		if (!InObject)
			return false;

		return BucketContainer.RemoveBucketObject(InObject, FunctionName);
	}

	bool UBucketUpdateSubsystem::RemoveObjectFromBucketByEvent(FDynamicBucketUpdateTickSignature Delegate)
	{
		if (!Delegate.IsBound())
			return false;

		return BucketContainer.RemoveBucketObject(Delegate);
	}

	bool UBucketUpdateSubsystem::RemoveObjectFromAllBuckets(UObject* InObject)
	{
		if (!InObject)
			return false;

		return BucketContainer.RemoveObjectFromAllBuckets(InObject);
	}

	bool UBucketUpdateSubsystem::IsObjectFunctionInBucket(UObject* InObject, FName FunctionName)
	{
		if (!InObject)
			return false;

		return BucketContainer.IsObjectFunctionInBucket(InObject, FunctionName);
	}

	bool UBucketUpdateSubsystem::IsActive()
	{
		return BucketContainer.bNeedsUpdate;
	}

	void UBucketUpdateSubsystem::Tick(float DeltaTime)
	{
		BucketContainer.UpdateBuckets(DeltaTime);
	}

	bool UBucketUpdateSubsystem::IsTickable() const
	{
		return BucketContainer.bNeedsUpdate;
	}

	UWorld* UBucketUpdateSubsystem::GetTickableGameObjectWorld() const
	{
		return GetWorld();
	}

	bool UBucketUpdateSubsystem::IsTickableInEditor() const
	{
		return false;
	}

	bool UBucketUpdateSubsystem::IsTickableWhenPaused() const
	{
		return false;
	}

	ETickableTickType UBucketUpdateSubsystem::GetTickableTickType() const
	{
		if (IsTemplate(RF_ClassDefaultObject))
			return ETickableTickType::Never;

		return ETickableTickType::Conditional;
	}

	TStatId UBucketUpdateSubsystem::GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UVRGripScriptBase, STATGROUP_Tickables);
	}
	
	bool FUpdateBucketDrop::ExecuteBoundCallback()
	{
		if (NativeCallback.IsBound())
		{
			return NativeCallback.Execute();
		}
		else if (DynamicCallback.IsBound())
		{
			DynamicCallback.Execute();
			return true;
		}

		return false;
	}

	bool FUpdateBucketDrop::IsBoundToObjectFunction(UObject * Obj, FName & FuncName)
	{
		return (NativeCallback.IsBoundToObject(Obj) && FunctionName == FuncName);
	}

	bool FUpdateBucketDrop::IsBoundToObjectDelegate(FDynamicBucketUpdateTickSignature & DynEvent)
	{
		return DynamicCallback == DynEvent;
	}

	bool FUpdateBucketDrop::IsBoundToObject(UObject * Obj)
	{
		return (NativeCallback.IsBoundToObject(Obj) || DynamicCallback.IsBoundToObject(Obj));
	}

	FUpdateBucketDrop::FUpdateBucketDrop()
	{
		FunctionName = NAME_None;
	}

	FUpdateBucketDrop::FUpdateBucketDrop(FDynamicBucketUpdateTickSignature & DynCallback)
	{
		DynamicCallback = DynCallback;
	}

	FUpdateBucketDrop::FUpdateBucketDrop(UObject * Obj, FName FuncName)
	{
		if (Obj && Obj->FindFunction(FuncName))
		{
			FunctionName = FuncName;
			NativeCallback.BindUFunction(Obj, FunctionName);
		}
		else
		{
			FunctionName = NAME_None;
		}
	}
	
	bool FUpdateBucket::Update(float DeltaTime)
	{
		//#TODO: Need to consider batching / spreading out load if there are a lot of updating objects in the bucket
		if (Callbacks.Num() < 1)
			return false;

		// Check for if this bucket is ready to fire events
		nUpdateCount += DeltaTime;
		if (nUpdateCount >= nUpdateRate)
		{
			nUpdateCount = 0.0f;
			for (int i = Callbacks.Num() - 1; i >= 0; --i)
			{
				if (Callbacks[i].ExecuteBoundCallback())
				{
					// If this returns true then we keep it in the queue
					continue;
				}

				// Remove the callback, it is complete or invalid
				Callbacks.RemoveAt(i);
			}
		}

		return Callbacks.Num() > 0;
	}
	
	void FUpdateBucketContainer::UpdateBuckets(float DeltaTime)
	{
		TArray<uint32> BucketsToRemove;
		for(auto& Bucket : ReplicationBuckets)
		{		
			if (!Bucket.Value.Update(DeltaTime))
			{
				// Add Bucket to list to remove at end of update
				BucketsToRemove.Add(Bucket.Key);
			}
		}

		// Remove unused buckets so that they don't get ticked
		for (const uint32 Key : BucketsToRemove)
		{
			ReplicationBuckets.Remove(Key);
		}

		if (ReplicationBuckets.Num() < 1)
			bNeedsUpdate = false;
	}

	bool FUpdateBucketContainer::AddBucketObject(uint32 UpdateHTZ, UObject* InObject, FName FunctionName)
	{
		if (!InObject || InObject->FindFunction(FunctionName) == nullptr || UpdateHTZ < 1)
			return false;

		// First verify that this object isn't already contained in a bucket, if it is then erase it so that we can replace it below
		RemoveBucketObject(InObject, FunctionName);

		if (ReplicationBuckets.Contains(UpdateHTZ))
		{
			ReplicationBuckets[UpdateHTZ].Callbacks.Add(FUpdateBucketDrop(InObject, FunctionName));
		}
		else
		{
			FUpdateBucket & newBucket = ReplicationBuckets.Add(UpdateHTZ, FUpdateBucket(UpdateHTZ));
			ReplicationBuckets[UpdateHTZ].Callbacks.Add(FUpdateBucketDrop(InObject, FunctionName));
		}

		if (ReplicationBuckets.Num() > 0)
			bNeedsUpdate = true;

		return true;
	}


	bool FUpdateBucketContainer::AddBucketObject(uint32 UpdateHTZ, FDynamicBucketUpdateTickSignature &Delegate)
	{
		if (!Delegate.IsBound() || UpdateHTZ < 1)
			return false;

		// First verify that this object isn't already contained in a bucket, if it is then erase it so that we can replace it below
		RemoveBucketObject(Delegate);

		if (ReplicationBuckets.Contains(UpdateHTZ))
		{
			ReplicationBuckets[UpdateHTZ].Callbacks.Add(FUpdateBucketDrop(Delegate));
		}
		else
		{
			FUpdateBucket & newBucket = ReplicationBuckets.Add(UpdateHTZ, FUpdateBucket(UpdateHTZ));
			ReplicationBuckets[UpdateHTZ].Callbacks.Add(FUpdateBucketDrop(Delegate));
		}

		if (ReplicationBuckets.Num() > 0)
			bNeedsUpdate = true;

		return true;
	}

	bool FUpdateBucketContainer::RemoveBucketObject(UObject * ObjectToRemove, FName FunctionName)
	{
		if (!ObjectToRemove || ObjectToRemove->FindFunction(FunctionName) == nullptr)
			return false;

		// Store if we ended up removing it
		bool bRemovedObject = false;

		TArray<uint32> BucketsToRemove;
		for (auto& Bucket : ReplicationBuckets)
		{
			for (int i = Bucket.Value.Callbacks.Num() - 1; i >= 0; --i)
			{
				if (Bucket.Value.Callbacks[i].IsBoundToObjectFunction(ObjectToRemove, FunctionName))
				{
					Bucket.Value.Callbacks.RemoveAt(i);
					bRemovedObject = true;

					// Leave the loop, this is called in add as well so we should never get duplicate entries
					break;
				}
			}

			if (bRemovedObject)
			{
				break;
			}
		}

		return bRemovedObject;
	}

	bool FUpdateBucketContainer::RemoveBucketObject(FDynamicBucketUpdateTickSignature &DynEvent)
	{
		if (!DynEvent.IsBound())
			return false;

		// Store if we ended up removing it
		bool bRemovedObject = false;

		TArray<uint32> BucketsToRemove;
		for (auto& Bucket : ReplicationBuckets)
		{
			for (int i = Bucket.Value.Callbacks.Num() - 1; i >= 0; --i)
			{
				if (Bucket.Value.Callbacks[i].IsBoundToObjectDelegate(DynEvent))
				{
					Bucket.Value.Callbacks.RemoveAt(i);
					bRemovedObject = true;

					// Leave the loop, this is called in add as well so we should never get duplicate entries
					break;
				}
			}

			if (bRemovedObject)
			{
				break;
			}
		}

		return bRemovedObject;
	}

	bool FUpdateBucketContainer::RemoveObjectFromAllBuckets(UObject * ObjectToRemove)
	{
		if (!ObjectToRemove)
			return false;

		// Store if we ended up removing it
		bool bRemovedObject = false;

		TArray<uint32> BucketsToRemove;
		for (auto& Bucket : ReplicationBuckets)
		{
			for (int i = Bucket.Value.Callbacks.Num() - 1; i >= 0; --i)
			{
				if (Bucket.Value.Callbacks[i].IsBoundToObject(ObjectToRemove))
				{
					Bucket.Value.Callbacks.RemoveAt(i);
					bRemovedObject = true;
				}
			}
		}

		return bRemovedObject;
	}

	bool FUpdateBucketContainer::IsObjectInBucket(UObject * ObjectToRemove)
	{
		if (!ObjectToRemove)
			return false;
		for (auto& Bucket : ReplicationBuckets)
		{
			for (int i = Bucket.Value.Callbacks.Num() - 1; i >= 0; --i)
			{
				if (Bucket.Value.Callbacks[i].IsBoundToObject(ObjectToRemove))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool FUpdateBucketContainer::IsObjectFunctionInBucket(UObject * ObjectToRemove, FName FunctionName)
	{
		if (!ObjectToRemove)
			return false;
		for (auto& Bucket : ReplicationBuckets)
		{
			for (int i = Bucket.Value.Callbacks.Num() - 1; i >= 0; --i)
			{
				if (Bucket.Value.Callbacks[i].IsBoundToObjectFunction(ObjectToRemove, FunctionName))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool FUpdateBucketContainer::IsObjectDelegateInBucket(FDynamicBucketUpdateTickSignature &DynEvent)
	{
		if (!DynEvent.IsBound())
			return false;

		for (auto& Bucket : ReplicationBuckets)
		{
			for (int i = Bucket.Value.Callbacks.Num() - 1; i >= 0; --i)
			{
				if (Bucket.Value.Callbacks[i].IsBoundToObjectDelegate(DynEvent))
				{
					return true;
				}
			}
		}

		return false;
	}