// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Console.h"
#include "Containers/UnrealString.h"
#include "Misc/OutputDeviceHelper.h"
#include "VRLogComponent.generated.h"

/**
*
*/
UENUM(BlueprintType)
enum class EBPVRConsoleDrawType : uint8
{
	VRConsole_Draw_ConsoleOnly,
	VRConsole_Draw_OutputLogOnly
//	VRConsole_Draw_ConsoleAndOutputLog
};


/**
* A single log message for the output log, holding a message and
* a style, for color and bolding of the message.
*/
struct FVRLogMessage
{
	TSharedRef<FString> Message;
	ELogVerbosity::Type Verbosity;
	FName Category;
	FName Style;

	FVRLogMessage(const TSharedRef<FString>& NewMessage, FName NewCategory, FName NewStyle = NAME_None)
		: Message(NewMessage)
		, Verbosity(ELogVerbosity::Log)
		, Category(NewCategory)
		, Style(NewStyle)
	{
	}

	FVRLogMessage(const TSharedRef<FString>& NewMessage, ELogVerbosity::Type NewVerbosity, FName NewCategory, FName NewStyle = NAME_None)
		: Message(NewMessage)
		, Verbosity(NewVerbosity)
		, Category(NewCategory)
		, Style(NewStyle)
	{
	}
};

// Custom Log output history class to hold the VR logs.
/** This class is to capture all log output even if the log window is closed */
class FVROutputLogHistory : public FOutputDevice
{
public:

	int32 MaxStoredMessages;
	bool bIsDirty;
	int32 MaxLineLength;

	FVROutputLogHistory()
	{
		MaxLineLength = 130;
		bIsDirty = false;
		MaxStoredMessages = 1000;
		GLog->AddOutputDevice(this);
		GLog->SerializeBacklog(this);
	}

	~FVROutputLogHistory()
	{
		// At shutdown, GLog may already be null
		if (GLog != NULL)
		{
			GLog->RemoveOutputDevice(this);
		}
	}

	/** Gets all captured messages */
	const TArray< TSharedPtr<FVRLogMessage> >& GetMessages() const
	{
		return Messages;
	}

protected:

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		// Capture all incoming messages and store them in history
		CreateLogMessages(V, Verbosity, Category, Messages);
	}

	bool CreateLogMessages(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category, TArray< TSharedPtr<FVRLogMessage> >& OutMessages)
	{
		if (Verbosity == ELogVerbosity::SetColor)
		{
			// Skip Color Events
			return false;
		}
		else
		{
			FName Style;
			if (Category == NAME_Cmd)
			{
				Style = FName(TEXT("Log.Command"));
			}
			else if (Verbosity == ELogVerbosity::Error)
			{
				Style = FName(TEXT("Log.Error"));
			}
			else if (Verbosity == ELogVerbosity::Warning)
			{
				Style = FName(TEXT("Log.Warning"));
			}
			else
			{
				Style = FName(TEXT("Log.Normal"));
			}

			// Forget timestamps, I don't care about them and we have limited texture space to draw too
			// Determine how to format timestamps
			static ELogTimes::Type LogTimestampMode = ELogTimes::None;
			/*if (UObjectInitialized() && !GExitPurge)
			{
			// Logging can happen very late during shutdown, even after the UObject system has been torn down, hence the init check above
			LogTimestampMode = GetDefault<UEditorStyleSettings>()->LogTimestampMode;
			}*/

			const int32 OldNumMessages = OutMessages.Num();

			// handle multiline strings by breaking them apart by line
			TArray<FTextRange> LineRanges;
			FString CurrentLogDump = V;
			FTextRange::CalculateLineRangesFromString(CurrentLogDump, LineRanges);

			bool bIsFirstLineInMessage = true;
			for (const FTextRange& LineRange : LineRanges)
			{
				if (!LineRange.IsEmpty())
				{
					FString Line = CurrentLogDump.Mid(LineRange.BeginIndex, LineRange.Len());
					Line = Line.ConvertTabsToSpaces(4);

					// Hard-wrap lines to avoid them being too long
					/*static const */int32 HardWrapLen = MaxLineLength;
					for (int32 CurrentStartIndex = 0; CurrentStartIndex < Line.Len();)
					{
						int32 HardWrapLineLen = 0;
						if (bIsFirstLineInMessage)
						{
							FString MessagePrefix = FOutputDeviceHelper::FormatLogLine(Verbosity, Category, nullptr, LogTimestampMode);

							HardWrapLineLen = FMath::Min(HardWrapLen - MessagePrefix.Len(), Line.Len() - CurrentStartIndex);
							FString HardWrapLine = Line.Mid(CurrentStartIndex, HardWrapLineLen);

							OutMessages.Add(MakeShareable(new FVRLogMessage(MakeShareable(new FString(MessagePrefix + HardWrapLine)), Verbosity, Category, Style)));
						}
						else
						{
							HardWrapLineLen = FMath::Min(HardWrapLen, Line.Len() - CurrentStartIndex);
							FString HardWrapLine = Line.Mid(CurrentStartIndex, HardWrapLineLen);

							OutMessages.Add(MakeShareable(new FVRLogMessage(MakeShareable(new FString(MoveTemp(HardWrapLine))), Verbosity, Category, Style)));
						}

						bIsFirstLineInMessage = false;
						CurrentStartIndex += HardWrapLineLen;
					}
				}
			}

			int numMessages = OutMessages.Num();
			if (numMessages > MaxStoredMessages)
			{
				OutMessages.RemoveAt(0, numMessages - MaxStoredMessages, EAllowShrinking::Yes);
			}
			if (OldNumMessages != numMessages)
				bIsDirty = true;

			return OldNumMessages != numMessages;//OutMessages.Num();
		}
	}

private:

	/** All log messages since this module has been started */
	TArray< TSharedPtr<FVRLogMessage> > Messages;
};

/**
* This class taps into the output log and console and renders them to textures so they can be viewed in levels.
* Generally used for debugging and testing in VR, also allows sending input to the console.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRLogComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVRLogComponent(const FObjectInitializer& ObjectInitializer);


	~UVRLogComponent();

	FVROutputLogHistory OutputLogHistory;

	virtual void PostInitProperties() override
	{
		Super::PostInitProperties();
		OutputLogHistory.MaxStoredMessages = FMath::Clamp(MaxStoredMessages, 100, 100000);
		OutputLogHistory.MaxLineLength = FMath::Clamp(MaxLineLength, 50, 1000);
	}

	UPROPERTY(BlueprintReadWrite,EditAnywhere, Category = "VRLogComponent|Console")
		int32 MaxLineLength;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRLogComponent|Console")
		int32 MaxStoredMessages;

	// Sets the console input text, can be used to clear the console or enter full or partial commands
	UFUNCTION(BlueprintCallable, Category = "VRLogComponent|Console", meta = (bIgnoreSelf = "true"))
		void SetConsoleText(FString Text);

	// Sends a key to the console - Console considers Released as final, flashes the cursor
	UFUNCTION(BlueprintCallable, Category = "VRLogComponent|Console", meta = (bIgnoreSelf = "true"))
		void SendKeyEventToConsole(FKey Key, EInputEvent KeyEvent);

	// Sends text to the console - Optionally returns at the end to "enter" the text, end flashes the cursor
	UFUNCTION(BlueprintCallable, Category = "VRLogComponent|Console", meta = (bIgnoreSelf = "true"))
		void AppendTextToConsole(FString Text, bool bReturnAtEnd = false);

	// Draw the console to a render target 2D
	UFUNCTION(BlueprintCallable, Category = "VRLogComponent|Console", meta = (bIgnoreSelf = "true", DisplayName = "DrawConsoleToCanvasRenderTarget2D"))
		bool DrawConsoleToRenderTarget2D(EBPVRConsoleDrawType DrawType, UTextureRenderTarget2D * Texture, float ScrollOffset, bool bForceDraw);


	void DrawConsole(bool bLowerHalfOnly, UCanvas* Canvas);
	void DrawOutputLog(bool bUpperHalfOnly, UCanvas* Canvas, float ScrollOffset);

};