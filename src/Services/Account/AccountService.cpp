#include "Services/Account/AccountService.h"

#include "Network/MicrosoftAuth.h"

#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

AccountService::AccountService(const Runtime& runtime, SettingsStore& settings, std::string accountFile)
	: m_rt(runtime), m_settings(settings), m_file(std::move(accountFile)), m_accounts(LoadAccounts(m_file)),
	  m_active(m_accounts.Active()), m_scope(runtime.tasks)
{
	m_rt.log << "Loaded " << m_accounts.accounts.size() << " account(s)\n";
}

void AccountService::Save()
{
	m_active = m_accounts.Active();
	std::error_code removeError;
	if (m_accounts.accounts.empty())
		fs::remove(m_file, removeError);
	else if (!SaveAccounts(m_file, m_accounts))
		m_rt.log << "Could not save " << m_file << "\n";
}

void AccountService::Select(std::size_t index)
{
	m_accounts.active = index;
	Save();
}

void AccountService::RemoveActive()
{
	m_accounts.Remove(m_accounts.active);
	Save();
}

void AccountService::Add(const Account& account)
{
	m_accounts.AddOrReplace(account);
	Save();
}

void AccountService::Update(const Account& refreshed)
{
	m_accounts.Update(refreshed);
	Save();
}

void AccountService::CancelSignIn()
{
	m_signInCancel->store(true);
}

void AccountService::StartMicrosoftSignIn()
{
	m_signIn = SignInState::Starting;
	m_signInError.clear();
	m_signInCancel = std::make_shared<std::atomic<bool>>(false);
	const std::string clientId = m_settings.Get().microsoftClientId;
	m_scope.Run([this, clientId, cancel = m_signInCancel]() {
		std::string error;
		Account signedIn;
		const bool ok = SignInWithMicrosoft(m_rt.http, clientId, signedIn, &error, cancel.get());
		m_rt.ui.Post([this, ok, signedIn, error, cancel]() {
			m_signIn = SignInState::Idle;
			if (ok) {
				Add(signedIn);
				m_rt.log << "Signed in as " << signedIn.name << "\n";
			} else if (!cancel->load()) {
				m_signInError = error;
				m_rt.log << "Microsoft sign-in failed: " << error << "\n";
			}
		});
	});
}
