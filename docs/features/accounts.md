# Accounts (Sign-in)

Who Minecraft is launched as. Shown in the **ACCOUNT** section of the sidebar.

## Account model (`src/Storage/Account.{h,cpp}`)
- `Account` has a type (`None`, `Microsoft`, `Offline`), name, UUID (32 hex digits), Minecraft access token, its expiry (unix seconds), Microsoft refresh token, and Xbox user id.
- Saved in `account.dat` in the data folder (gitignored). On Windows the JSON is encrypted with DPAPI (`CryptProtectData`), so only the same Windows user can read it. A missing, unreadable or undecryptable file means no account. **Sign out** deletes the file.
- One account at a time.

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
