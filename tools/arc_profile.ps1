# arc_profile.ps1 — parser for arcade PROF lines from the UART log.
#
# Firmware emits one line per 120 frames:
#   PROF,<sys>,<game>,<fps*10>,<emu_us>,<run_us>,<scan_us>,<dcs_us>,<snd_us>,<draw_us>,<render_us>,<cpu68_us>
# All times are average microseconds per frame.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools/arc_profile.ps1 -Log C:\logs\arc.log
#   powershell -ExecutionPolicy Bypass -File tools/arc_profile.ps1 -Log arc.log -Csv docs/arc_profile.csv
#
# The first PROF sample of every game is dropped by default (warm-up).

param(
    [Parameter(Mandatory = $true)][string]$Log,
    [string]$Csv = "",
    [int]$SkipFirst = 1
)

$ErrorActionPreference = "Stop"
$inv = [System.Globalization.CultureInfo]::InvariantCulture
$rows = New-Object System.Collections.Generic.List[object]

foreach ($line in Get-Content -LiteralPath $Log -Encoding UTF8) {
    if ($line -notmatch '^PROF,') { continue }
    $p = $line.Split(',')
    if ($p.Count -lt 12) { continue }
    $rows.Add([pscustomobject]@{
        Sys   = $p[1]
        Game  = $p[2]
        Fps10 = [int]$p[3]
        Emu   = [int]$p[4]
        Run   = [int]$p[5]
        Scan  = [int]$p[6]
        Dcs   = [int]$p[7]
        Snd   = [int]$p[8]
        Draw  = [int]$p[9]
        Ren   = [int]$p[10]
        Cpu68 = [int]$p[11]
    })
}

if ($rows.Count -eq 0) {
    Write-Host "No PROF lines found in $Log"
    exit 1
}

$summary = foreach ($g in ($rows | Group-Object { "$($_.Sys)|$($_.Game)" })) {
    $samples = @($g.Group)
    if ($samples.Count -gt $SkipFirst) {
        $samples = $samples[$SkipFirst..($samples.Count - 1)]
    }
    if ($samples.Count -eq 0) { continue }

    $avg = {
        param($prop)
        ($samples | ForEach-Object { $_.$prop } | Measure-Object -Average).Average
    }
    $fps10 = [math]::Round((& $avg 'Fps10'), 0)
    $emu   = [math]::Round((& $avg 'Emu'), 1)
    $run   = [math]::Round((& $avg 'Run'), 1)
    $scan  = [math]::Round((& $avg 'Scan'), 1)
    $dcs   = [math]::Round((& $avg 'Dcs'), 1)
    $snd   = [math]::Round((& $avg 'Snd'), 1)
    $draw  = [math]::Round((& $avg 'Draw'), 1)
    $ren   = [math]::Round((& $avg 'Ren'), 1)
    $cpu68 = [math]::Round((& $avg 'Cpu68'), 1)

    $period = if ($fps10 -gt 0) { 10000000.0 / $fps10 } else { 0.0 }
    $total = $emu + $ren
    $head = $period - $total
    $load = if ($period -gt 0) { 100.0 * $total / $period } else { 0.0 }

    $parts = @(
        [pscustomobject]@{ Name = 'run';    Us = $run },
        [pscustomobject]@{ Name = 'scan';   Us = $scan },
        [pscustomobject]@{ Name = 'dcs';    Us = $dcs },
        [pscustomobject]@{ Name = 'snd';    Us = $snd },
        [pscustomobject]@{ Name = 'draw';   Us = $draw },
        [pscustomobject]@{ Name = 'render'; Us = $ren },
        [pscustomobject]@{ Name = 'emu';    Us = $emu }
    )
    $bn = ($parts | Sort-Object Us -Descending | Select-Object -First 1).Name

    [pscustomobject]@{
        System     = $samples[0].Sys
        Game       = $samples[0].Game
        Samples    = $samples.Count
        FPS        = $fps10 / 10.0
        Period_us  = [math]::Round($period, 1)
        Emu_us     = $emu
        Run_us     = $run
        Scan_us    = $scan
        Dcs_us     = $dcs
        Snd_us     = $snd
        Draw_us    = $draw
        Render_us  = $ren
        Total_us   = [math]::Round($total, 1)
        Headroom_us = [math]::Round($head, 1)
        Load_pct   = [math]::Round($load, 1)
        Bottleneck = $bn
    }
}

$summary = $summary | Sort-Object System, Game
$summary | Format-Table System, Game, Samples, FPS, Emu_us, Run_us, Scan_us, Dcs_us,
    Snd_us, Draw_us, Render_us, Total_us, Headroom_us, Load_pct, Bottleneck -AutoSize |
    Out-String -Width 260 | Write-Host

if ($Csv -ne "") {
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("system,game,samples,fps,period_us,emu_us,run_us,scan_us,dcs_us,snd_us,draw_us,render_us,total_us,headroom_us,load_pct,bottleneck")
    foreach ($s in $summary) {
        $lines.Add([string]::Format($inv,
            "{0},{1},{2},{3:F1},{4:F1},{5:F1},{6:F1},{7:F1},{8:F1},{9:F1},{10:F1},{11:F1},{12:F1},{13:F1},{14:F1},{15}",
            $s.System, $s.Game, $s.Samples, $s.FPS, $s.Period_us, $s.Emu_us,
            $s.Run_us, $s.Scan_us, $s.Dcs_us, $s.Snd_us, $s.Draw_us,
            $s.Render_us, $s.Total_us, $s.Headroom_us, $s.Load_pct, $s.Bottleneck))
    }
    Set-Content -LiteralPath $Csv -Value $lines -Encoding UTF8
    Write-Host "CSV: $Csv"
}
