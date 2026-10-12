#pragma once

#include "Core/Runtime.h"
#include "Core/TaskScope.h"
#include "Services/Settings/SettingsStore.h"
#include "Storage/Account.h"

#include <atomic>
#include <memory>
#include <string>

// The accounts Minecraft can be launched as: Microsoft sign-in (a browser round trip run on a
// worker) and offline names. Saved to account.dat. UI thread only.
class AccountService {
public:
	enum class SignInState { Idle, Starting };

	AccountService(const Runtime& runtime, SettingsStore& settings, std::string accountFile);

	// A copy of the active account (type None when signed out), kept in step with the list.
	const Account& Active() const { return m_active; }
	const AccountList& List() const { return m_accounts; }

	// Makes the account at index the active one.
	void Select(std::size_t index);
	// Removes the active account (Sign out).
	void RemoveActive();
	// A new sign-in (or offline name) becomes the active account.
	void Add(const Account& account);
	// Fresh tokens for a saved account; it keeps its place in the list.
	void Update(const Account& refreshed);

	// Opens the browser for Microsoft sign-in on a worker; the account is added when it succeeds.
	void StartMicrosoftSignIn();
	// Cancels a sign-in that is waiting for the browser.
	void CancelSignIn();
	SignInState SignIn() const { return m_signIn; }
	bool SignInCancelling() const { return m_signInCancel->load(); }
	const std::string& SignInError() const { return m_signInError; }

private:
	void Save();

	Runtime m_rt;
	SettingsStore& m_settings;
	std::string m_file;
	AccountList m_accounts;
	Account m_active;
	SignInState m_signIn = SignInState::Idle;
	std::string m_signInError;
	std::shared_ptr<std::atomic<bool>> m_signInCancel = std::make_shared<std::atomic<bool>>(false);
	TaskScope m_scope; // last, so its tasks finish before the members above go away
};
