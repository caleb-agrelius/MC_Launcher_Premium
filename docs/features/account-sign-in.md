# Microsoft / Minecraft sign-in setup

The launcher opens Microsoft's sign-in page in the system browser using OAuth
authorization code flow with PKCE. After sign-in, Microsoft redirects to a
temporary localhost listener; the launcher then exchanges the Microsoft access
token with Xbox Live, XSTS, and Minecraft Services. No client secret is used.

## Entra app registration

1. In Microsoft Entra, open **App registrations** and register an application
   that supports **personal Microsoft accounts**.
2. In **Authentication**, add the `http://localhost` redirect URI under
   **Mobile and desktop applications**. The launcher uses an available local
   port for each sign-in attempt; no client secret is needed.
3. Add the delegated Xbox Live `XboxLive.signin` permission if it is available.
   Xbox API access or Minecraft Services may require approval for the app.
4. Copy the **Application (client) ID**. Do not use the Directory (tenant) ID.

## Configure the launcher

Set `microsoftClientId` in the launcher's `settings.json` to that application
ID. For example:

```json
{
  "uiScale": 1.0,
  "microsoftClientId": "YOUR-APPLICATION-CLIENT-ID",
  "maxMemoryMb": 4096
}
```

Use the client ID, not a client secret; the ID is not secret. The app must also
be approved to use the Minecraft API. See [accounts.md](accounts.md) for the
sign-in flow, token storage, and troubleshooting details.
