// Copyright Epic Games, Inc. All Rights Reserved.


#include "PatternBreakerPlayerController.h"
#include "Components/InputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "PatternBreaker.h"
#include "PatternBreakerGameMode.h"
#include "Widgets/Input/SVirtualJoystick.h"

void APatternBreakerPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogPatternBreaker, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void APatternBreakerPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		InputComponent->BindKey(EKeys::M, IE_Pressed, this, &APatternBreakerPlayerController::RequestMainMenuReturn);

		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

void APatternBreakerPlayerController::RequestMainMenuReturn()
{
	if (UWorld* World = GetWorld())
	{
		if (APatternBreakerGameMode* RaidMode = World->GetAuthGameMode<APatternBreakerGameMode>())
		{
			RaidMode->ReturnToMainMenu();
		}
	}
}

bool APatternBreakerPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
