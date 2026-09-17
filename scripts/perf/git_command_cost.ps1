#Requires -Version 7
<#
.SYNOPSIS
Serial cost of single git commands, from one that does nothing to one that scans the working tree. Windows only.

.DESCRIPTION
Separates per-process cost from the work a command does: `--version` touches no repository and reads no
config, so its time is process startup alone, and each further command in the ladder adds one step
(config, repository discovery, the index, the working tree). One process at a time, so this is the latency
a blocking query pays, not throughput - use git_throughput.ps1 for that.

Comparing two builds this way is how a per-command startup regression between git versions shows up (see
doc/perf.md). A portable build unpacked anywhere serves as the second binary.

.PARAMETER Binaries
Name to git executable, two or more to compare. Each one past the first gets a delta column against it.

.EXAMPLE
scripts/perf/git_command_cost.ps1 -Repo C:\path\to\repo -Binaries @{
	current = 'C:\Program Files\Git\mingw64\bin\git.exe'; older = 'C:\portable\mingw64\bin\git.exe' }
#>
param(
	[Parameter(Mandatory)][string]$Repo,
	[hashtable]$Binaries = @{ git = (Get-Command git).Source },
	[int]$Runs = 60,
	[long]$AffinityMask = 0 # 0: leave the harness's affinity alone; see git_throughput.ps1
)
$ErrorActionPreference = 'Stop'
if ($AffinityMask -ne 0) { [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity = [IntPtr]$AffinityMask }
# Not $binaries: PowerShell variable names are case-insensitive, so that would overwrite the parameter
$orderedBinaries = [ordered]@{}
foreach ($name in ($Binaries.Keys | Sort-Object)) { $orderedBinaries[$name] = $Binaries[$name] }
$baseline = @($orderedBinaries.Keys)[0]
$commands = [ordered]@{
	version    = @('--version')                                     # no repository, no config
	config     = @('config', '--get', 'core.quotepath')             # reads the config files
	revparse   = @('rev-parse', '--absolute-git-dir')               # discovers the repository
	lsfiles    = @('ls-files', '--stage', '-z')                     # reads the index
	status     = @('status', '--porcelain', '-z')                   # scans the working tree
}

function Measure-Serial([string]$exe, [string[]]$arguments, [int]$runs)
{
	$times = foreach ($i in 1..$runs)
	{
		$psi = [Diagnostics.ProcessStartInfo]::new($exe)
		foreach ($a in $arguments) { $psi.ArgumentList.Add($a) }
		$psi.WorkingDirectory = $Repo; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
		$psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
		$sw = [Diagnostics.Stopwatch]::StartNew()
		$p = [Diagnostics.Process]::Start($psi)
		$null = $p.StandardOutput.ReadToEnd(); $null = $p.StandardError.ReadToEnd()
		$p.WaitForExit()
		$sw.Elapsed.TotalMilliseconds
		$p.Dispose()
	}
	$sorted = @($times | Sort-Object)
	$sorted[[math]::Floor($sorted.Count / 2)]
}

$rows = foreach ($command in $commands.Keys)
{
	$row = [ordered]@{ command = $command }
	foreach ($name in $orderedBinaries.Keys)
	{
		$null = Measure-Serial $orderedBinaries[$name] $commands[$command] 5 # warm-up
		$row[$name] = [math]::Round((Measure-Serial $orderedBinaries[$name] $commands[$command] $Runs), 2)
	}
	# Skipping the baseline leaves a single binary with no delta column at all
	foreach ($name in (@($orderedBinaries.Keys) | Select-Object -Skip 1)) { $row["delta $name"] = [math]::Round($row[$name] - $row[$baseline], 2) }
	[pscustomobject]$row
}
$rows | Format-Table -AutoSize
