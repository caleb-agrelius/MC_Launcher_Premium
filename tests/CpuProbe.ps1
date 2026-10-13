<#
.SYNOPSIS
  Measures how much CPU the launcher uses while it sits idle.

.DESCRIPTION
  Starts the exe offline, lets it settle for -WarmupSeconds, then samples its processor time over
  -SampleSeconds and prints the result. Repeats -Runs times and prints the median, so one noisy
  sample does not decide the answer. Run it against a Release build for numbers that match what
  users see.

.EXAMPLE
  .\tests\CpuProbe.ps1 -Exe .\build-release\Release\MC_Launcher_Premium.exe -Label before
#>
param(
	[Parameter(Mandatory)][string]$Exe,
	[string]$Label = '',
	[int]$WarmupSeconds = 8,
	[int]$SampleSeconds = 20,
	[int]$Runs = 3,
	[switch]$Minimized,
	[hashtable]$Environment = @{}
)

$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path $Exe).Path
$root = Split-Path $PSScriptRoot -Parent
$cores = [Environment]::ProcessorCount
$results = @()

for ($run = 1; $run -le $Runs; $run++) {
	$env:MCLP_OFFLINE = '1'
	$env:MCLP_DEBUG = 'perf'
	$perfLog = Join-Path ([IO.Path]::GetTempPath()) "cpuprobe-$PID-$run.txt"
	foreach ($key in $Environment.Keys) { Set-Item "env:$key" $Environment[$key] }
	$style = if ($Minimized) { 'Minimized' } else { 'Normal' }
	$process = Start-Process $exePath -WorkingDirectory $root -WindowStyle $style -PassThru -RedirectStandardError $perfLog
	try {
		Start-Sleep -Seconds $WarmupSeconds
		$process.Refresh()
		$before = $process.TotalProcessorTime.TotalSeconds
		$threadsBefore = @{}
		foreach ($thread in $process.Threads) { $threadsBefore[$thread.Id] = $thread.TotalProcessorTime.TotalSeconds }
		Start-Sleep -Seconds $SampleSeconds
		$process.Refresh()
		$after = $process.TotalProcessorTime.TotalSeconds
		$busiest = 0.0
		foreach ($thread in $process.Threads) {
			$start = if ($threadsBefore.ContainsKey($thread.Id)) { $threadsBefore[$thread.Id] } else { 0.0 }
			$busiest = [Math]::Max($busiest, $thread.TotalProcessorTime.TotalSeconds - $start)
		}
		$onecore = ($after - $before) / $SampleSeconds * 100
		# Frames per second and mascot time from the app's own `perf` lines (one per 5 s).
		$fps = @(); $mascot = @()
		foreach ($line in (Get-Content $perfLog -ErrorAction SilentlyContinue)) {
			if ($line -match '([\d.]+) fps') { $fps += [double]$Matches[1] }
			if ($line -match 'mascot ([\d.]+)') { $mascot += [double]$Matches[1] }
		}
		$fps = @($fps | Select-Object -Skip 1)
		$mascot = @($mascot | Select-Object -Skip 1)
		$results += [pscustomobject]@{
			Run = $run
			CpuPercentOfOneCore = [Math]::Round($onecore, 1)
			BusiestThreadPercent = [Math]::Round($busiest / $SampleSeconds * 100, 1)
			Fps = if ($fps.Count) { [Math]::Round(($fps | Measure-Object -Average).Average, 1) } else { $null }
			MascotMsPerFrame = if ($mascot.Count) { [Math]::Round(($mascot | Measure-Object -Average).Average, 3) } else { $null }
			MemoryMB = [int]($process.WorkingSet64 / 1MB)
		}
	}
	finally {
		if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
		foreach ($key in $Environment.Keys) { Remove-Item "env:$key" -ErrorAction SilentlyContinue }
		Remove-Item env:MCLP_DEBUG -ErrorAction SilentlyContinue
		Remove-Item $perfLog -ErrorAction SilentlyContinue
	}
}

$sorted = $results | Sort-Object CpuPercentOfOneCore
$median = $sorted[[int][Math]::Floor($sorted.Count / 2)]
$results | Format-Table -AutoSize | Out-String | Write-Host
Write-Host ("[{0}] median: {1}% of one core = {2}% of the whole CPU ({3} logical cores); busiest thread {4}%; {5} fps" -f `
	$Label, $median.CpuPercentOfOneCore, [Math]::Round($median.CpuPercentOfOneCore / $cores, 1), $cores, $median.BusiestThreadPercent, $median.Fps)
