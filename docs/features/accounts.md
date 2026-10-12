# Accounts (Sign-in)

Who Minecraft is launched as. Shown in the **ACCOUNT** section of the sidebar.

## Sidebar
- With no account: **Sign in with Microsoft** and **Play offline...**.
- With accounts: the active account's name (a dropdown to switch when more than one is saved, each entry marked Microsoft or Offline) and its kind, then **Add account** (a menu with **Sign in with Microsoft** and **Play offline...**) and **Sign out**, which removes only the active account; the next one becomes active.
- While a Microsoft sign-in is running, a spinner and **Cancel** replace these. Sign-in errors appear in red below.
- Settings > Account shows the active account and how many are saved.

## Account model (`src/Storage/Account.{h,cpp}`)
- `Account` has a type (`None`, `Microsoft`, `Offline`), name, UUID (32 hex digits), Minecraft access token, its expiry (unix seconds), Microsoft refresh token, and Xbox user id.
- Any number of accounts are saved (`AccountList`: the accounts plus the index of the active one, which Play uses). `account.dat` in the data folder (gitignored) holds `{"active": n, "accounts": [...]}`. On Windows the JSON is encrypted with DPAPI (`CryptProtectData`), so only the same Windows user can read it. A missing, unreadable or undecryptable file means no accounts. Files from before multiple accounts (one account object) still load. When the last account is removed, the file is deleted.
- `AddOrReplace` adds a new sign-in or offline name and makes it active. Signing in again as a saved player replaces that entry instead of adding a duplicate: the same UUID for Microsoft, the same name for offline. A replaced offline account keeps its UUID, so worlds still know the player. `Update` stores refreshed tokens on the matching account wherever it is in the list.
- The startup log says how many accounts were loaded.

## Microsoft sign-in (`src/Network/MicrosoftAuth.{h,cpp}`)
- Uses the launcher’s default Azure app (client) ID, which can be changed in Settings > Account (saved as `"microsoftClientId"` in `settings.json`). The app must be registered for personal Microsoft accounts with public client flows enabled, **and Mojang must approve it for the Minecraft API**. Without approval, the last step fails with a message saying so. If the client ID is cleared, **Sign in with Microsoft** is disabled.
- Flow (authorization code with PKCE; no manual code entry):
  1. `SignInWithMicrosoft()` opens the Microsoft sign-in page in the system browser and starts a temporary listener on `localhost`.
  2. After the player signs in, Microsoft redirects to the listener. The app validates the OAuth state and exchanges the authorization code using its PKCE verifier.
  3. Xbox Live user token, then XSTS token, then `login_with_xbox` (Minecraft access token, about 24 h), then `/minecraft/profile` for the name and UUID.
- Errors are explained for common Xbox `XErr` codes (no Xbox profile, unsupported country, adult verification, child account), a 403 from Minecraft services (client ID not approved), and a 404 profile (doesn't own Java Edition).
- Before each launch, a Microsoft account whose token expires within 5 minutes is refreshed with `RefreshMicrosoftAccount()` (refresh token, then the same Xbox/Minecraft steps), and the saved account is updated.
- HTTP goes through `HttpClient::Request`, which returns the status and body even for 4xx responses.

## Offline mode
- **Play offline...** opens a dialog asking for a username: 3–16 letters, digits or underscores (`IsValidPlayerName`). The dialog explains that offline mode is for singleplayer and servers that don't check accounts, and only for people who own Minecraft: Java Edition.
- `MakeOfflineAccount()` creates a random version-4 UUID, which is saved so worlds keep the player's data, and uses the access token `"0"`.
- The sidebar shows the account as "Offline (singleplayer)".
