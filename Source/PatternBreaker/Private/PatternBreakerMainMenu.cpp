#include "PatternBreakerMainMenu.h"

#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "PatternBreakerMainMenu"

namespace
{
	FText DescribeFailure(const FPatternBreakerAuthResponse& Response)
	{
		const FString& Code = Response.ServerStatus;
		if (Code == TEXT("invalid_credentials")) return LOCTEXT("InvalidCredentials", "Incorrect username or password.");
		if (Code == TEXT("username_taken")) return LOCTEXT("UsernameTaken", "That username is already registered.");
		if (Code == TEXT("invalid_request")) return LOCTEXT("InvalidInput", "Check your username and password. New passwords need at least 12 characters.");
		if (Code == TEXT("too_many_attempts")) return LOCTEXT("RateLimited", "Too many attempts. Wait a minute and try again.");
		if (Code == TEXT("invalid_token") || Response.HttpStatusCode == 401)
		{
			return LOCTEXT("InvalidSession", "Your session is no longer valid. Sign in again.");
		}
		switch (Response.Result)
		{
		case EPatternBreakerAuthResult::TokenExpired:
		case EPatternBreakerAuthResult::NotLoggedIn: return LOCTEXT("Expired", "Sign in again to continue.");
		case EPatternBreakerAuthResult::ConnectionFailed: return LOCTEXT("Offline", "Could not connect to the server. Check that it is running.");
		case EPatternBreakerAuthResult::TimedOut: return LOCTEXT("Timeout", "The server took too long to respond. Try again.");
		case EPatternBreakerAuthResult::Busy: return LOCTEXT("Busy", "A request is already in progress. Try again in a moment.");
		case EPatternBreakerAuthResult::InvalidConfiguration: return LOCTEXT("MissingConfig", "The server address is missing.");
		default: return LOCTEXT("ServerError", "The server could not complete the request. Try again.");
		}
	}
}

void UPatternBreakerMainMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	StartButton = WidgetTree->FindWidget<UButton>(TEXT("BTN_StartGame"));
	RankingButton = WidgetTree->FindWidget<UButton>(TEXT("BTN_Ranking"));
	bMenuBindingsReady = AuthScreens && LoginPage && MenuPage &&
		LoginPage->GetParent() == AuthScreens && MenuPage->GetParent() == AuthScreens &&
		TXT_Username && TXT_Password && BTN_Login && BTN_Register && BTN_Logout &&
		LBL_LoginMessage && LBL_MenuMessage && LBL_SignedInName && StartButton && RankingButton;
	if (!ensure(bMenuBindingsReady))
	{
		// Fail closed if a required Designer control is missing.
		if (UWidget* Root = GetRootWidget()) Root->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	AuthScreens->SetActiveWidget(LoginPage);
	OriginalStart = StartButton->OnClicked;
	OriginalRanking = RankingButton->OnClicked;
	StartButton->OnClicked.Clear();
	RankingButton->OnClicked.Clear();
	StartButton->OnClicked.AddUniqueDynamic(this, &ThisClass::StartGame);
	RankingButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OpenRanking);
	BTN_Login->OnClicked.AddUniqueDynamic(this, &ThisClass::SubmitLogin);
	BTN_Register->OnClicked.AddUniqueDynamic(this, &ThisClass::SubmitRegistration);
	BTN_Logout->OnClicked.AddUniqueDynamic(this, &ThisClass::SignOut);
}

void UPatternBreakerMainMenu::NativeConstruct()
{
	Super::NativeConstruct();
	if (!bMenuBindingsReady) return;

	UGameInstance* GameInstance = GetGameInstance();
	Api = GameInstance ? GameInstance->GetSubsystem<UPatternBreakerApiSubsystem>() : nullptr;
	if (!Api)
	{
		ShowLogin(LOCTEXT("Unavailable", "Sign-in is unavailable."));
		return;
	}
	Api->OnRegistrationCompleted.AddUniqueDynamic(this, &ThisClass::HandleRegistration);
	Api->OnLoginCompleted.AddUniqueDynamic(this, &ThisClass::HandleLogin);
	Api->OnCurrentUserCompleted.AddUniqueDynamic(this, &ThisClass::HandleCurrentUser);
	ShowLogin();

	FPatternBreakerAuthUser CachedUser;
	float Remaining = 0.0f;
	if (Api->GetCachedUser(CachedUser, Remaining)) ConfirmSession(EAction::OpenMenu);
}

void UPatternBreakerMainMenu::NativeDestruct()
{
	if (Api)
	{
		Api->OnRegistrationCompleted.RemoveDynamic(this, &ThisClass::HandleRegistration);
		Api->OnLoginCompleted.RemoveDynamic(this, &ThisClass::HandleLogin);
		Api->OnCurrentUserCompleted.RemoveDynamic(this, &ThisClass::HandleCurrentUser);
		Api = nullptr;
	}
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(ExpiryTimer);
	if (TXT_Password) TXT_Password->SetText(FText());
	if (bMenuBindingsReady) AuthScreens->SetActiveWidget(LoginPage);
	PendingAction = EAction::Idle;
	Super::NativeDestruct();
}

void UPatternBreakerMainMenu::SubmitLogin() { SubmitCredentials(EAction::Login); }
void UPatternBreakerMainMenu::SubmitRegistration() { SubmitCredentials(EAction::Register); }
void UPatternBreakerMainMenu::StartGame() { if (PendingAction == EAction::Idle) ConfirmSession(EAction::StartGame); }
void UPatternBreakerMainMenu::OpenRanking() { if (PendingAction == EAction::Idle) ConfirmSession(EAction::Ranking); }

void UPatternBreakerMainMenu::SubmitCredentials(EAction Action)
{
	if (!Api || PendingAction != EAction::Idle || AuthScreens->GetActiveWidget() != LoginPage) return;
	const FString Username = TXT_Username->GetText().ToString().TrimStartAndEnd();
	const FString Password = TXT_Password->GetText().ToString();
	TXT_Password->SetText(FText());
	if (Username.IsEmpty() || Password.IsEmpty())
	{
		ShowLogin(LOCTEXT("MissingInput", "Enter your username and password."));
		return;
	}
	if (Action == EAction::Register)
	{
		SetAction(Action, LOCTEXT("Registering", "Creating your account..."));
		Api->RegisterAccount(Username, Password);
	}
	else
	{
		SetAction(Action, LOCTEXT("SigningIn", "Signing in..."));
		Api->Login(Username, Password);
	}
}

void UPatternBreakerMainMenu::ConfirmSession(EAction Action)
{
	FPatternBreakerAuthUser User;
	float Remaining = 0.0f;
	if (!Api || !Api->GetCachedUser(User, Remaining))
	{
		SignOut();
		ShowLogin(LOCTEXT("SessionRequired", "Sign in again to continue."));
		return;
	}
	SetAction(Action, LOCTEXT("Checking", "Verifying your session..."));
	Api->GetCurrentUser();
}

void UPatternBreakerMainMenu::HandleRegistration(const FPatternBreakerAuthResponse& Response)
{
	if (PendingAction != EAction::Register) return;
	ShowLogin(Response.Result == EPatternBreakerAuthResult::Ok
		? LOCTEXT("Registered", "Account created. You can sign in now.") : DescribeFailure(Response));
}

void UPatternBreakerMainMenu::HandleLogin(const FPatternBreakerAuthResponse& Response, const FPatternBreakerAuthUser& User)
{
	if (PendingAction != EAction::Login) return;
	if (Response.Result == EPatternBreakerAuthResult::Ok) ConfirmSession(EAction::OpenMenu);
	else ShowLogin(DescribeFailure(Response));
}

void UPatternBreakerMainMenu::HandleCurrentUser(const FPatternBreakerAuthResponse& Response, const FPatternBreakerAuthUser& User)
{
	const EAction Action = PendingAction;
	if (Action != EAction::OpenMenu && Action != EAction::StartGame && Action != EAction::Ranking) return;

	FPatternBreakerAuthUser CachedUser;
	float Remaining = 0.0f;
	const bool bHasSession = Api && Api->GetCachedUser(CachedUser, Remaining);
	if (Response.Result != EPatternBreakerAuthResult::Ok)
	{
		if (bHasSession && AuthScreens->GetActiveWidget() == MenuPage) SetAction(EAction::Idle, DescribeFailure(Response));
		else ShowLogin(DescribeFailure(Response));
		return;
	}
	if (!bHasSession || CachedUser.UserId != User.UserId)
	{
		SignOut();
		ShowLogin(LOCTEXT("SessionRequired", "Sign in again to continue."));
		return;
	}

	LBL_SignedInName->SetText(FText::Format(LOCTEXT("SignedIn", "Signed in as {0}"), FText::FromString(User.Username)));
	AuthScreens->SetActiveWidget(MenuPage);
	SetAction(EAction::Idle);
	ArmExpiryTimer(Remaining);
	// The original Widget Blueprint events still own level travel and ranking navigation.
	if (Action == EAction::StartGame) OriginalStart.Broadcast();
	else if (Action == EAction::Ranking) OriginalRanking.Broadcast();
}

void UPatternBreakerMainMenu::SetAction(EAction Action, const FText& Message)
{
	PendingAction = Action;
	const bool bIdle = Action == EAction::Idle;
	const bool bMenu = AuthScreens->GetActiveWidget() == MenuPage;
	BTN_Login->SetIsEnabled(bIdle && Api != nullptr);
	BTN_Register->SetIsEnabled(bIdle && Api != nullptr);
	StartButton->SetIsEnabled(bIdle && bMenu);
	RankingButton->SetIsEnabled(bIdle && bMenu);
	LBL_LoginMessage->SetText(bMenu ? FText() : Message);
	LBL_MenuMessage->SetText(bMenu ? Message : FText());
}

void UPatternBreakerMainMenu::ShowLogin(const FText& Message)
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(ExpiryTimer);
	AuthScreens->SetActiveWidget(LoginPage);
	TXT_Password->SetText(FText());
	LBL_SignedInName->SetText(FText());
	SetAction(EAction::Idle, Message);
	TXT_Username->SetKeyboardFocus();
}

void UPatternBreakerMainMenu::SignOut()
{
	if (Api) Api->Logout();
	ShowLogin();
}

void UPatternBreakerMainMenu::ArmExpiryTimer(float SecondsRemaining)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ExpiryTimer, this, &ThisClass::CheckSessionExpiry, FMath::Max(SecondsRemaining, 0.05f), false);
	}
}

void UPatternBreakerMainMenu::CheckSessionExpiry()
{
	FPatternBreakerAuthUser User;
	float Remaining = 0.0f;
	if (Api && Api->GetCachedUser(User, Remaining)) ArmExpiryTimer(Remaining);
	else
	{
		SignOut();
		ShowLogin(LOCTEXT("Expired", "Sign in again to continue."));
	}
}

#undef LOCTEXT_NAMESPACE
