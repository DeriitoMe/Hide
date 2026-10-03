param([int]$Seconds = 30, [int]$IntervalSeconds = 5, [string]$OutputPath)
$ErrorActionPreference = 'Stop'
$cursorRoot = Split-Path $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $cursorRoot 'state\resident-observation.json' }
& (Join-Path $cursorRoot 'Hide.exe') --status
Start-Sleep -Milliseconds 150
$cursorInitial = Get-Content -LiteralPath (Join-Path $cursorRoot 'state\status.json') -Raw | ConvertFrom-Json
$cursorMainId = $cursorInitial.pid
$cursorGuardId = Get-CimInstance Win32_Process -Filter "Name='Hide.exe'" | Where-Object { $_.ParentProcessId -eq $cursorMainId -and $_.CommandLine -match ' --guard ' } | Select-Object -First 1 -ExpandProperty ProcessId
if (-not $cursorGuardId) { throw 'Hide guardian not found.' }
$cursorIds = @($cursorMainId, $cursorGuardId)
$cursorWatch = [System.Diagnostics.Stopwatch]::StartNew()
$cursorSamples = [System.Collections.Generic.List[object]]::new()
$cursorReport = [ordered]@{ date='2026-10-03'; requested_seconds=$Seconds; elapsed_seconds=0; status='running'; passed=$false; samples=$cursorSamples; input_counts_start=$cursorInitial; helper_included_in_resource_totals=$false }
try {
    do {
        $cursorProcesses = @($cursorIds | ForEach-Object { Get-Process -Id $_ })
        if ($cursorProcesses.Count -ne 2) { throw 'Hide or guardian stopped during observation.' }
        $cursorSamples.Add([pscustomobject]@{
            elapsed_seconds=[math]::Round($cursorWatch.Elapsed.TotalSeconds,3)
            cpu_seconds=($cursorProcesses | Measure-Object -Property CPU -Sum).Sum
            private_bytes=($cursorProcesses | Measure-Object -Property PrivateMemorySize64 -Sum).Sum
            working_set_bytes=($cursorProcesses | Measure-Object -Property WorkingSet64 -Sum).Sum
            handles=($cursorProcesses | Measure-Object -Property HandleCount -Sum).Sum
        })
        $cursorReport.elapsed_seconds=[math]::Round($cursorWatch.Elapsed.TotalSeconds,3)
        $cursorReport | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
        if ($cursorWatch.Elapsed.TotalSeconds -ge $Seconds) { break }
        Start-Sleep -Seconds ([math]::Min($IntervalSeconds, [math]::Max(1,$Seconds-[int]$cursorWatch.Elapsed.TotalSeconds)))
    } while ($true)
    & (Join-Path $cursorRoot 'Hide.exe') --status
    Start-Sleep -Milliseconds 150
    $cursorFinal = Get-Content -LiteralPath (Join-Path $cursorRoot 'state\status.json') -Raw | ConvertFrom-Json
    if ($cursorFinal.pid -ne $cursorMainId) { throw 'Hide restarted during observation.' }
    $cursorReport.input_counts_end=$cursorFinal
    $cursorReport.cpu_single_core_percent=[math]::Round(100*($cursorSamples[-1].cpu_seconds-$cursorSamples[0].cpu_seconds)/($cursorSamples[-1].elapsed_seconds-$cursorSamples[0].elapsed_seconds),4)
    $cursorReport.keyboard_events_delta=$cursorFinal.keyboard_events-$cursorInitial.keyboard_events
    $cursorReport.mouse_events_delta=$cursorFinal.mouse_events-$cursorInitial.mouse_events
    $cursorReport.status='complete'
    $cursorReport.passed=$true
} catch {
    $cursorReport.status='interrupted'
    $cursorReport.error=$_.Exception.Message
} finally {
    $cursorReport.elapsed_seconds=[math]::Round($cursorWatch.Elapsed.TotalSeconds,3)
    $cursorReport | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
}
Write-Output ($cursorReport | ConvertTo-Json -Depth 8)
if (-not $cursorReport.passed) { exit 1 }
