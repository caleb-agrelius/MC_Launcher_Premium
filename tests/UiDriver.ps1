# Helpers that drive the real launcher window: start it in a throwaway data folder, click, scroll,
# screenshot, and measure how much of the window moved between two screenshots. Dot-source it:
#   . tests/UiDriver.ps1
#   Start-App $fixtureDir $exe
#   Click-At 83 442; Shot "$out/settings.png"; Stop-App
# Coordinates are pixels inside the client area of a window placed at $script:WindowX/Y with the size in
# $script:WindowWidth/Height (default 1800x1100). They assume Windows display scaling of 100%; at other
# scales the sidebar and tab positions move, so adjust them.
#
# The window is made topmost and screenshots copy that screen rectangle, so nothing else should sit in
# front of it while a scenario runs. Mouse input is real (SetCursorPos and mouse_event): don't touch the
# mouse during a run.
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
public static class UiDriverNative {
    public delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr window, int x, int y, int w, int h, bool repaint);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, int data, IntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr window, System.Text.StringBuilder name, int size);
    public static IntPtr Find(int processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, data) => {
            uint pid; GetWindowThreadProcessId(window, out pid);
            var name = new System.Text.StringBuilder(64); GetClassName(window, name, 64);
            if (pid == processId && name.ToString() == "GLFW30") { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    // A hash per pixel row of a screenshot strip, for finding how far the content moved.
    public static ulong[] RowHashes(string path, int x0, int x1, int y0, int y1) {
        using (var bmp = new Bitmap(path)) {
            var rows = new ulong[y1 - y0];
            for (int y = y0; y < y1; y++) {
                ulong h = 1469598103934665603UL;
                for (int x = x0; x < x1; x += 2) { var c = bmp.GetPixel(x, y); h = (h ^ (ulong)(c.R | (c.G << 8) | (c.B << 16))) * 1099511628211UL; }
                rows[y - y0] = h;
            }
            return rows;
        }
    }
    // The vertical shift (positive = content moved up) that makes the most rows of b equal a's.
    public static int BestShift(ulong[] a, ulong[] b, out int matched) {
        int best = 0; matched = -1;
        for (int s = -400; s <= 400; s++) {
            int m = 0;
            for (int i = 0; i < a.Length; i++) { int j = i - s; if (j >= 0 && j < b.Length && a[i] == b[j]) m++; }
            if (m > matched) { matched = m; best = s; }
        }
        return best;
    }
}
'@
[UiDriverNative]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
# Set $script:AppOnline = $true before Start-App to let the app use the network, and $script:AppTrace to a
# debug channel list (e.g. 'http') to trace into the console of the PowerShell host.
$script:AppOnline = $false
$script:AppTrace = ''
$script:WindowX = 100; $script:WindowY = 100; $script:WindowWidth = 1800; $script:WindowHeight = 1100

# Starts the launcher with $fixture as its working (data) folder and puts it at the window rectangle.
function Start-App($fixture, $exe) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $exe; $info.WorkingDirectory = $fixture; $info.UseShellExecute = $false
    $info.EnvironmentVariables['MCLP_CURSEFORGE_API_KEY'] = ''
    # Offline unless a test needs the network ($script:AppOnline = $true): no requests to the pack APIs.
    if (-not $script:AppOnline) { $info.EnvironmentVariables['MCLP_OFFLINE'] = '1' }
    if ($script:AppTrace) { $info.EnvironmentVariables['MCLP_DEBUG'] = $script:AppTrace }
    $script:app = [System.Diagnostics.Process]::Start($info)
    $script:appWindow = [IntPtr]::Zero
    for ($i = 0; $i -lt 100 -and $script:appWindow -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 100
        $script:appWindow = [UiDriverNative]::Find($script:app.Id)
    }
    if ($script:appWindow -eq [IntPtr]::Zero) { throw 'Launcher window was not created' }
    Start-Sleep -Milliseconds 1500
    [UiDriverNative]::MoveWindow($script:appWindow, $script:WindowX, $script:WindowY, $script:WindowWidth, $script:WindowHeight, $true) | Out-Null
    [UiDriverNative]::SetWindowPos($script:appWindow, [IntPtr](-1), $script:WindowX, $script:WindowY, $script:WindowWidth, $script:WindowHeight, 0x0040) | Out-Null
    [UiDriverNative]::SetForegroundWindow($script:appWindow) | Out-Null
    Start-Sleep -Milliseconds 1000
}

# Closes the window normally so settings.json and library.json are written.
function Close-App() {
    if ($script:app -and !$script:app.HasExited) {
        $script:app.CloseMainWindow() | Out-Null
        if (!$script:app.WaitForExit(10000)) { $script:app.Kill() }
    }
}

function Stop-App() { if ($script:app -and !$script:app.HasExited) { $script:app.Kill() } }

function Shot($path) {
    $bitmap = New-Object System.Drawing.Bitmap($script:WindowWidth, $script:WindowHeight)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($script:WindowX, $script:WindowY, 0, 0, $bitmap.Size)
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
}

function Move-Mouse($x, $y) {
    [UiDriverNative]::SetCursorPos($script:WindowX + $x, $script:WindowY + $y) | Out-Null
    Start-Sleep -Milliseconds 150
}

function Click-At($x, $y) {
    Move-Mouse $x $y; Move-Mouse ($x + 1) $y
    [UiDriverNative]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80
    [UiDriverNative]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 500
}

# One wheel notch is 120; negative scrolls down.
function Wheel-At($x, $y, $delta) {
    Move-Mouse $x $y
    [UiDriverNative]::mouse_event(0x800, 0, 0, $delta, [IntPtr]::Zero); Start-Sleep -Milliseconds 700
}

# How far the strip x0..x1, y0..y1 moved between two screenshots (positive = up), and how many rows matched.
function Get-Shift($before, $after, $x0, $x1, $y0, $y1) {
    $matched = 0
    $shift = [UiDriverNative]::BestShift([UiDriverNative]::RowHashes($before, $x0, $x1, $y0, $y1), [UiDriverNative]::RowHashes($after, $x0, $x1, $y0, $y1), [ref]$matched)
    [pscustomobject]@{ Shift = $shift; Matched = $matched; Rows = $y1 - $y0 }
}

# A fresh data folder with the offline index, and the given settings.json text.
function New-Fixture($dir, $settingsJson = '{}') {
    New-Item -ItemType Directory -Path (Join-Path $dir 'resources') -Force | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot/../resources/index.json" -Destination (Join-Path $dir 'resources/index.json') -Force
    Set-Content -LiteralPath (Join-Path $dir 'settings.json') -Value $settingsJson -Encoding Ascii
}
