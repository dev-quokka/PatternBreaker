#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "PatternBreakerApiSubsystem.h"
#include "TimerManager.h"
#include "PatternBreakerMainMenu.generated.h"

class UEditableTextBox;
class UTextBlock;
class UWidgetSwitcher;

/** Drives the sign-in flow for the Designer widgets in WBP_MainMenu. */
UCLASS(Abstract, Blueprintable)
class PATTERNBREAKER_API UPatternBreakerMainMenu : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	enum class EAction : uint8 { Idle, Register, Login, OpenMenu, StartGame, Ranking };

	void SubmitCredentials(EAction Action);
	void ConfirmSession(EAction Action);
	void SetAction(EAction Action, const FText& Message = FText());
	void ShowLogin(const FText& Message = FText());
	void ArmExpiryTimer(float SecondsRemaining);
	void CheckSessionExpiry();

	UFUNCTION() void SubmitLogin();
	UFUNCTION() void SubmitRegistration();
	UFUNCTION() void SignOut();
	UFUNCTION() void StartGame();
	UFUNCTION() void OpenRanking();
	UFUNCTION() void HandleRegistration(const FPatternBreakerAuthResponse& Response);
	UFUNCTION() void HandleLogin(const FPatternBreakerAuthResponse& Response, const FPatternBreakerAuthUser& User);
	UFUNCTION() void HandleCurrentUser(const FPatternBreakerAuthResponse& Response, const FPatternBreakerAuthUser& User);

	UPROPERTY(Transient) TObjectPtr<UPatternBreakerApiSubsystem> Api;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidgetSwitcher> AuthScreens;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> LoginPage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UWidget> MenuPage;
	UPROPERTY(BlueprintReadOnly, Category = "Login",
		meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UEditableTextBox> TXT_Username;
	UPROPERTY(BlueprintReadOnly, Category = "Password",
		meta = (BindWidget, AllowPrivateAccess = "true"))
	TObjectPtr<UEditableTextBox> TXT_Password;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BTN_Login;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BTN_Register;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UButton> BTN_Logout;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> LBL_LoginMessage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> LBL_MenuMessage;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> LBL_SignedInName;
	UPROPERTY(Transient) TObjectPtr<UButton> StartButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RankingButton;

	FOnButtonClickedEvent OriginalStart;
	FOnButtonClickedEvent OriginalRanking;
	FTimerHandle ExpiryTimer;
	EAction PendingAction = EAction::Idle;
	bool bMenuBindingsReady = false;
};
