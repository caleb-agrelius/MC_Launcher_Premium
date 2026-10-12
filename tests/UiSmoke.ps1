# Drives the real launcher window and checks the Settings page works the way a person uses it:
#  - the tip's "x" asks "Hide this tip?", Don't show again hides it for good (also after a restart)
#  - Appearance scrolls all the way back to the top after scrolling down
#  - Game options doesn't re-flow while scrolling (every column moves by exactly one wheel notch)
# Needs a visible desktop and no mouse use while it runs; see tests/UiDriver.ps1 for the assumptions
# (Windows display scaling 100%, window at 100,100, 1800x1100). Offline: it never touches the network.
param([string]$Executable = "$PSScriptRoot/../build/Debug/MC_Launcher_Premium.exe")
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/UiDriver.ps1"
$exe = (Resolve-Path -LiteralPath $Executable).Path
$out = Join-Path $PSScriptRoot ('out/ui-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$fixture = Join-Path $out 'data'
New-Fixture $fixture '{}'

function Assert($condition, $message) { if (-not $condition) { throw $message } }
$sidebarSettings = @(83, 442)
$tabMinecraft = @(444, 174); $tabGameOptions = @(554, 174)
$tipClose = @(1731, 224); $dialogConfirm = @(858, 593)

try {
    # The tip's "x", then Don't show again.
    Start-App $fixture $exe
    Click-At $sidebarSettings[0] $sidebarSettings[1]; Click-At $tabMinecraft[0] $tabMinecraft[1]
    Click-At $tipClose[0] $tipClose[1]
    Shot "$out/tip-dialog.png"
    Click-At $dialogConfirm[0] $dialogConfirm[1]
    Close-App
    $saved = Get-Content -LiteralPath (Join-Path $fixture 'settings.json') -Raw | ConvertFrom-Json
    Assert ($saved.dismissedNotices -contains 'minecraft_tip') "Don't show again did not save the tip as dismissed"

    # After a restart the tip is gone, so the first card sits where the tip was.
    Start-App $fixture $exe
    Click-At $sidebarSettings[0] $sidebarSettings[1]; Click-At $tabMinecraft[0] $tabMinecraft[1]
    Shot "$out/tip-gone.png"
    Stop-App

    # Appearance: down, then back up past the top.
    Start-App $fixture $exe
    Click-At $sidebarSettings[0] $sidebarSettings[1]
    Shot "$out/appearance-top.png"
    1..4 | ForEach-Object { Wheel-At 1000 600 -120 }
    1..7 | ForEach-Object { Wheel-At 1000 600 120 }
    Start-Sleep -Seconds 1
    Shot "$out/appearance-back.png"
    $drift = Get-Shift "$out/appearance-top.png" "$out/appearance-back.png" 295 560 215 1050
    # The strip is the first card column, away from the mouse's hover highlight.
    Assert ($drift.Shift -eq 0 -and $drift.Matched -ge 0.9 * $drift.Rows) "Appearance did not return exactly to the top (off by $($drift.Shift) px)"

    # Game options: every column moves one notch (120 px) per step until the end.
    Click-At $tabGameOptions[0] $tabGameOptions[1]; Start-Sleep -Seconds 2
    Shot "$out/go-00.png"
    $mid = [int]($script:WindowWidth / 2)
    for ($i = 1; $i -le 7; $i++) { # the page ends a notch or two later, where the last step is shorter
        Wheel-At 1000 600 -120; Start-Sleep -Milliseconds 400
        Shot ("$out/go-{0:00}.png" -f $i)
        $before = "$out/go-{0:00}.png" -f ($i - 1); $after = "$out/go-{0:00}.png" -f $i
        $left = Get-Shift $before $after 300 ($mid - 40) 215 1050
        $right = Get-Shift $before $after ($mid + 40) ($script:WindowWidth - 100) 215 1050
        Assert ($left.Shift -eq 120 -and $right.Shift -eq 120) "Game options moved unevenly at step ${i}: left $($left.Shift) px, right $($right.Shift) px"
    }
    Stop-App
    Write-Output "Settings UI checks passed (screenshots in $out)."
} finally {
    Stop-App
}
