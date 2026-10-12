param([string]$Executable = "$PSScriptRoot/../build/Debug/MC_Launcher_Premium.exe")
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class LauncherWindowCheck {
    public delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr window, int x, int y, int w, int h, bool repaint);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr window);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr window, System.Text.StringBuilder name, int size);
    public struct Rect { public int Left, Top, Right, Bottom; }
    // The launcher's GLFW window, not one of the hidden OpenGL, IME or GLFW helper windows
    // the process also owns.
    public static IntPtr Find(int id) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, data) => {
            uint processId;
            GetWindowThreadProcessId(window, out processId);
            var name = new System.Text.StringBuilder(64);
            GetClassName(window, name, name.Capacity);
            if (processId == id && name.ToString() == "GLFW30") { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
[LauncherWindowCheck]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$exePath = (Resolve-Path -LiteralPath $Executable).Path
$fixture = Join-Path $PSScriptRoot ('out/window-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $fixture 'resources') -Force | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/../resources/index.json" -Destination (Join-Path $fixture 'resources/index.json')
Set-Content -LiteralPath (Join-Path $fixture 'settings.json') -Value '{"discoverSources":["curseforge"],"reduceMotion":true}' -Encoding Ascii

function Start-CheckLauncher {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $exePath
    $startInfo.WorkingDirectory = $fixture
    $startInfo.UseShellExecute = $false
    $startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $startInfo.EnvironmentVariables['MCLP_CURSEFORGE_API_KEY'] = ''
    $script:checkProcess = [System.Diagnostics.Process]::Start($startInfo)
    $handle = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        Start-Sleep -Milliseconds 100
        if ($script:checkProcess.HasExited) { throw 'Launcher exited during startup' }
        $handle = [LauncherWindowCheck]::Find($script:checkProcess.Id)
        if ($handle -ne [IntPtr]::Zero) { break }
    }
    if ($handle -eq [IntPtr]::Zero) { throw 'Launcher window was not created' }
    Start-Sleep -Milliseconds 1000
    return $handle
}

function Close-CheckLauncher($handle) {
    [LauncherWindowCheck]::PostMessage($handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    if (!$script:checkProcess.WaitForExit(10000)) { throw 'Launcher did not close normally' }
    if ($script:checkProcess.ExitCode -ne 0) { throw 'Launcher exited with an error' }
    return (Get-Content -LiteralPath (Join-Path $fixture 'settings.json') -Raw | ConvertFrom-Json).window
}

try {
    $handle = Start-CheckLauncher
    [LauncherWindowCheck]::MoveWindow($handle, 100, 100, 1800, 1100, $true) | Out-Null
    Start-Sleep -Milliseconds 700
    $rect = New-Object LauncherWindowCheck+Rect
    [LauncherWindowCheck]::GetClientRect($handle, [ref]$rect) | Out-Null
    $first = Close-CheckLauncher $handle
    if ($first.width -ne $rect.Right -or $first.height -ne $rect.Bottom -or $first.maximized) {
        throw "Closing the launcher did not save its client size (saved $($first.width)x$($first.height), Windows $($rect.Right)x$($rect.Bottom))"
    }
    $handle = Start-CheckLauncher
    [LauncherWindowCheck]::GetClientRect($handle, [ref]$rect) | Out-Null
    if ($rect.Right -ne $first.width -or $rect.Bottom -ne $first.height) {
        throw 'Launcher did not restore its saved size'
    }
    [LauncherWindowCheck]::ShowWindow($handle, 3) | Out-Null
    Start-Sleep -Milliseconds 700
    $maximized = Close-CheckLauncher $handle
    if (!$maximized.maximized -or $maximized.width -ne $first.width -or $maximized.height -ne $first.height) {
        throw 'Maximized close lost the normal window size'
    }
    $handle = Start-CheckLauncher
    if (![LauncherWindowCheck]::IsZoomed($handle)) { throw 'Launcher did not reopen maximized' }
    [LauncherWindowCheck]::ShowWindow($handle, 9) | Out-Null
    Start-Sleep -Milliseconds 700
    [LauncherWindowCheck]::ShowWindow($handle, 6) | Out-Null
    Start-Sleep -Milliseconds 300
    $minimized = Close-CheckLauncher $handle
    if ($minimized.width -ne $first.width -or $minimized.height -ne $first.height) {
        throw 'Minimized close lost the normal window size'
    }
    $handle = Start-CheckLauncher
    [LauncherWindowCheck]::ShowWindow($handle, 3) | Out-Null
    Start-Sleep -Milliseconds 700
    [LauncherWindowCheck]::ShowWindow($handle, 6) | Out-Null
    Start-Sleep -Milliseconds 300
    $minimizedMax = Close-CheckLauncher $handle
    if (!$minimizedMax.maximized -or $minimizedMax.width -ne $first.width -or $minimizedMax.height -ne $first.height) {
        throw 'Minimizing a maximized window lost its placement'
    }
    Write-Output "Launcher resize, reopen, maximize and minimize checks passed ($($first.width)x$($first.height))."
} finally {
    if ($script:checkProcess -and !$script:checkProcess.HasExited) { $script:checkProcess.Kill() }
}
