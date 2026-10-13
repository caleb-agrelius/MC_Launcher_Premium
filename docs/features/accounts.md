# Accounts (Sign-in)

Who Minecraft is launched as. Shown in the **ACCOUNT** section of the sidebar.

There is no offline play. Minecraft only launches as a Microsoft account that owns Minecraft: Java Edition, and the account is checked with Microsoft and Minecraft services before every launch (see [Account check before Play](#account-check-before-play)). Until Mojang approves the launcher's client ID for the Minecraft API, nobody can sign in, so nobody can play.

## Sidebar
- With no account: **Sign in with Microsoft**.
- With accounts: the active account's name (a dropdown to switch when more than one is saved, each entry marked Microsoft) and its kind, then **Add account** (starts another Microsoft sign-in) and **Sign out**, which removes only the active account; the next one becomes active.
- **Sign in with Microsoft** and **Add account** are disabled while no client ID is set.
- While a Microsoft sign-in is running, a spinner and **Cancel** replace these. Sign-in errors appear in red below.
- Settings > Account shows the active account and how many are saved.

## Account model (`src/Storage/Account.{h,cpp}`)
- `Account` has a type (`None`, `Microsoft`), name, UUID (32 hex digits), Minecraft access token, its expiry (unix seconds), Microsoft refresh token, and Xbox user id.
- Any number of accounts are saved (`AccountList`: the accounts plus the index of the active one, which Play uses). `account.dat` in the data folder (gitignored) holds `{"active": n, "accounts": [...]}`. On Windows the JSON is encrypted with DPAPI, so only the same Windows user can read it. On Linux there is no per-user encryption: the JSON is stored as is, and the file is limited to its owner (mode 0600) before anything is written to it. Both go through `Platform::ProtectSecret`, `Platform::UnprotectSecret` and `Platform::RestrictToOwner` (see [platforms.md](platforms.md)). A missing, unreadable or undecryptable file means no accounts. Files from before multiple accounts (one account object) still load. Offline accounts saved by older versions (`"type": "offline"`) are dropped when the file is read. When the last account is removed, the file is deleted.
- `AddOrReplace` adds a new sign-in and makes it active. Signing in again as a saved player (same UUID) replaces that entry instead of adding a duplicate. `Update` stores refreshed tokens on the matching account wherever it is in the list.
- `CanLaunchAs()` is true only for a Microsoft account with a name, UUID, Minecraft access token and refresh token. It doesn't say whether the tokens still work; that's the check below.
- The startup log says how many accounts were loaded.

## Microsoft sign-in (`src/Network/MicrosoftAuth.{h,cpp}`)
- Uses the launcher’s default Azure app (client) ID, which can be changed in Settings > Account (saved as `"microsoftClientId"` in `settings.json`). The app must be registered for personal Microsoft accounts with public client flows enabled, **and Mojang must approve it for the Minecraft API**. Without approval, the last step fails with a message saying so. If the client ID is cleared, **Sign in with Microsoft** is disabled.
- Flow (authorization code with PKCE; no manual code entry):
  1. `SignInWithMicrosoft()` starts a temporary listener on `127.0.0.1` (`Platform::LoopbackListener`) and opens the Microsoft sign-in page in the system browser (`Platform::OpenUrl`). The state and the PKCE verifier are random bytes from `Platform::RandomBytes`, and the challenge is the verifier's SHA-256 (`Util/Sha256`, `PkceChallenge`).
  2. After the player signs in, Microsoft redirects to the listener. `ParseAuthorizationRedirect` checks the request and the OAuth state, and the app exchanges the authorization code using its PKCE verifier. The browser is shown a short "Sign-in complete" or "could not be completed" page.
  3. Xbox Live user token, then XSTS token, then `login_with_xbox` (Minecraft access token, about 24 h), then `/entitlements/mcstore` (the items must include `game_minecraft` or `product_minecraft`, so the account owns the game; Game Pass counts), then `/minecraft/profile` for the name and UUID.
- Errors are explained for common Xbox `XErr` codes (no Xbox profile, unsupported country, adult verification, child account), a 403 from Minecraft services (client ID not approved), and an account without the game (no ownership entitlement, or a 404 profile).
- HTTP goes through `HttpClient::Request`, which returns the status and body even for 4xx responses.

## Account check before Play
- `PlayService::Start()` refuses to start (an error on the card) unless the active account passes `CanLaunchAs()` and a client ID is set.
- The play worker then runs `RefreshMicrosoftAccount()` every time, whatever the token's expiry: the refresh token, then the same Xbox, Minecraft, ownership and profile steps as sign-in. Nothing is prepared or launched unless it succeeds, and the saved account gets the fresh tokens. A revoked sign-in, lost ownership or an unapproved client ID fails here with its message.
- `PlayModpack()` itself also refuses an account that doesn't pass `CanLaunchAs()`, before touching any files, so no other caller can launch without an account.
- The `--offline` debug switch ([debug-harness.md](debug-harness.md#offline-mode)) only blocks network requests; with it on, the account check fails, so Play never works offline.
