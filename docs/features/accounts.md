# Accounts (Sign-in)

Who Minecraft is launched as. Shown in the **ACCOUNT** section of the sidebar.

## Account model (`src/Storage/Account.{h,cpp}`)
- `Account` has a type (`None`, `Microsoft`, `Offline`), name, UUID (32 hex digits), Minecraft access token, its expiry (unix seconds), Microsoft refresh token, and Xbox user id.
- Saved in `account.dat` in the data folder (gitignored). On Windows the JSON is encrypted with DPAPI (`CryptProtectData`), so only the same Windows user can read it. A missing, unreadable or undecryptable file means no account. **Sign out** deletes the file.
- One account at a time.

## Microsoft sign-in (`src/Network/MicrosoftAuth.{h,cpp}`)
- Needs an Azure app (client) ID set as `"microsoftClientId"` in `settings.json`. The app must be registered for personal Microsoft accounts with public client flows enabled, **and Mojang must approve it for the Minecraft API**. Without approval, the last step fails with a message saying so. With no client ID, **Sign in with Microsoft** is disabled and has a tooltip explaining why.
- Flow (device code; no browser embedding or redirect server):
  1. `StartMicrosoftDeviceCode()` asks `login.microsoftonline.com/consumers` for a code (scope `XboxLive.signin offline_access`). The sidebar shows the URL and code, with **Copy code & open page** (copies the code and opens the browser) and **Cancel**.
  2. `FinishMicrosoftDeviceCode()` polls the token endpoint at the requested interval (handling `authorization_pending` and `slow_down`) until the player finishes, declines, the code expires, or the attempt is cancelled.
  3. Xbox Live user token, then XSTS token, then `login_with_xbox` (Minecraft access token, about 24 h), then `/minecraft/profile` for the name and UUID.
- Errors are explained for common Xbox `XErr` codes (no Xbox profile, unsupported country, adult verification, child account), a 403 from Minecraft services (client ID not approved), and a 404 profile (doesn't own Java Edition).
- Before each launch, a Microsoft account whose token expires within 5 minutes is refreshed with `RefreshMicrosoftAccount()` (refresh token, then the same Xbox/Minecraft steps), and the saved account is updated.
- HTTP goes through `HttpClient::Request`, which returns the status and body even for 4xx responses.

## Offline mode
- **Play offline...** opens a dialog asking for a username: 3–16 letters, digits or underscores (`IsValidPlayerName`). The dialog explains that offline mode is for singleplayer and servers that don't check accounts, and only for people who own Minecraft: Java Edition.
- `MakeOfflineAccount()` creates a random version-4 UUID, which is saved so worlds keep the player's data, and uses the access token `"0"`.
- The sidebar shows the account as "Offline (singleplayer)".
