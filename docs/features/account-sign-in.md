# Microsoft / Minecraft sign-in

The launcher signs users in through the Microsoft identity platform using the
OAuth 2.0 authorization code flow with PKCE. It then exchanges the Microsoft
access token with Xbox Live, XSTS, and Minecraft Services and verifies that the
account has a Minecraft: Java Edition profile.

## Microsoft app registration

Sign-in requires an application registered for public desktop clients. Create
one in the Microsoft Entra admin center:

1. Register an app that supports personal Microsoft accounts.
2. Add the `http://localhost` redirect URI under the **Mobile and desktop
   applications** platform. The launcher listens on an available localhost port
   and uses PKCE; no client secret is used.
3. Configure the delegated Xbox Live `XboxLive.signin` permission required for
   the Minecraft authentication exchange. Microsoft may require Xbox API access
   approval for the app before this permission or the Minecraft token exchange
   is available.
4. Copy the app's **Application (client) ID**.

When configuring CMake, provide that ID with
`-DMCLP_MICROSOFT_CLIENT_ID="YOUR-APPLICATION-CLIENT-ID"`. The ID is a public
identifier, not a secret. If it is omitted, the launcher builds normally but
the sign-in button reports that this setting is required.

For example:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DMCLP_MICROSOFT_CLIENT_ID="00000000-0000-0000-0000-000000000000"
```

Reconfigure and rebuild after changing the client ID.

## Token handling

OAuth state and PKCE values are generated for each sign-in attempt, and the
authorization code is received on a loopback listener. Access tokens are used
only for the Xbox and Minecraft exchanges and are not saved to disk. The
launcher retains only the Minecraft profile name and UUID in memory until the
user signs out or exits.

This authenticates the account and verifies its Java profile; game installation,
entitlement management, and launching are still separate planned features.
