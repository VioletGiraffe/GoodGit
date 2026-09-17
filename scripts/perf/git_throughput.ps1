#Requires -Version 7
<#
.SYNOPSIS
Wall time of a batch of read-only git queries at a ladder of concurrency caps. Windows only.

.DESCRIPTION
What MaxConcurrentProcesses in vcsprocess.cpp is chosen with: the cap past which more processes in flight
buy nothing. Queries are launched as the app launches them - CreateProcess with no window, stdout and
stderr drained, the app's read-only invariants on the command line - so process startup counts as it does
in the app. Comparing two git builds also measures per-command cost (see doc/perf.md).

The workloads are the ones a refresh is made of: one `status` per submodule, and `ls-files --stage`, which
is what the folder scan runs.

.PARAMETER Repos
Repository roots to query. Jobs are each root's initialized submodules, or the root itself where it has
none, cycled to -JobCount. A superproject is not queried itself: its `status` recurses into every submodule
and would dwarf the other jobs.

.PARAMETER Binaries
Name to git executable. Several compare builds, or the Git for Windows `cmd\git.exe` wrapper against the
real `mingw64\bin\git.exe` behind it.

.PARAMETER AffinityMask
Processor affinity for the harness, which every git process inherits. Set it to compare core types on a
hybrid CPU (cpu_topology.ps1 prints which logical processors are which).
Sticks for the rest of the PowerShell process: pass it explicitly in every run of a comparison, since a
later run without it is not unpinned.

.EXAMPLE
scripts/perf/git_throughput.ps1 -Repos C:\path\to\superproject -Caps 1,4,8,16,24,32 -Repeats 9

.EXAMPLE
scripts/perf/git_throughput.ps1 -Repos C:\path\to\repo -Binaries @{
	wrapper = 'C:\Program Files\Git\cmd\git.exe'; real = 'C:\Program Files\Git\mingw64\bin\git.exe' }
#>
param(
	[Parameter(Mandatory)][string[]]$Repos,
	[hashtable]$Binaries = @{ git = (Get-Command git).Source },
	[int[]]$Caps = @(1, 2, 4, 6, 8, 12, 16, 24),
	[int]$Repeats = 5,
	[int]$JobCount = 0, # 0: one job per repository directory found, no cycling
	[string[]]$Workloads = @(), # empty: all of them
	[long]$AffinityMask = 0, # 0: leave the harness's affinity alone
	[string]$OutCsv = (Join-Path $env:TEMP 'git_throughput.csv')
)
$ErrorActionPreference = 'Stop'
$summaryPath = [IO.Path]::ChangeExtension($OutCsv, '.summary.txt')
if ($AffinityMask -ne 0) { [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity = [IntPtr]$AffinityMask }
# Redirected pipes are synchronous handles, so draining one blocks a pool thread: two per running job.
# The pool must hold more than twice the highest cap, or thread injection paces the batch instead of the CPU.
$null = [Threading.ThreadPool]::SetMinThreads(128, 128)

$jobDirs = foreach ($root in $Repos) {
	# Leading '-' marks a submodule that was never initialized: an empty directory, nothing to query
	$submodules = @(& git -C $root submodule status --recursive | Where-Object { $_ -notmatch '^-' } |
		ForEach-Object { Join-Path $root (($_.Trim() -split ' ')[1]) })
	if ($submodules.Count) { $submodules } else { $root }
}
if ($JobCount -eq 0) { $JobCount = $jobDirs.Count }
$dirs = @(for ($i = 0; $i -lt $JobCount; $i++) { $jobDirs[$i % $jobDirs.Count] })

# Names differing from the parameters' only in case: PowerShell variables are case-insensitive, and
# assigning to $binaries or $workloads here would overwrite the parameter instead of making a local
$orderedBinaries = [ordered]@{}
foreach ($name in ($Binaries.Keys | Sort-Object)) { $orderedBinaries[$name] = $Binaries[$name] }
# The app's invariants on read-only queries come first
$invariants = @('-c', 'core.quotepath=false', '--no-optional-locks')
$workloadArgs = [ordered]@{
	status  = @('status', '--porcelain', '-z')
	lsfiles = @('ls-files', '--stage', '-z')
}

function Start-GitJob([string]$exe, [string[]]$arguments, [string]$dir)
{
	$psi = [Diagnostics.ProcessStartInfo]::new($exe)
	foreach ($a in $arguments) { $psi.ArgumentList.Add($a) }
	$psi.WorkingDirectory = $dir
	$psi.UseShellExecute = $false
	$psi.CreateNoWindow = $true
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.Environment['GIT_TERMINAL_PROMPT'] = '0'
	$process = [Diagnostics.Process]::Start($psi)
	$drain = [Threading.Tasks.Task]::WhenAll(
		$process.StandardOutput.BaseStream.CopyToAsync([IO.Stream]::Null),
		$process.StandardError.BaseStream.CopyToAsync([IO.Stream]::Null))
	# The process handle itself, as QWinEventNotifier waits on it
	$exitHandle = [Threading.ManualResetEvent]::new($false)
	$exitHandle.SafeWaitHandle = [Microsoft.Win32.SafeHandles.SafeWaitHandle]::new($process.Handle, $false)
	[pscustomobject]@{ Process = $process; ExitHandle = $exitHandle; Drain = $drain }
}

# Returns @(elapsedMs, failedJobs)
function Invoke-Batch([string]$exe, [string[]]$arguments, [int]$cap)
{
	$running = [Collections.Generic.List[object]]::new()
	$next = 0
	$failed = 0
	$stopwatch = [Diagnostics.Stopwatch]::StartNew()
	while ($next -lt $dirs.Count -or $running.Count -gt 0)
	{
		while ($running.Count -lt $cap -and $next -lt $dirs.Count)
		{
			$running.Add((Start-GitJob $exe $arguments $dirs[$next]))
			$next++
		}
		# A plain loop: a PowerShell pipeline here costs about a millisecond per running job, per iteration
		$handles = [Threading.WaitHandle[]]::new($running.Count)
		for ($i = 0; $i -lt $running.Count; $i++) { $handles[$i] = $running[$i].ExitHandle }
		$index = [Threading.WaitHandle]::WaitAny($handles)
		$job = $running[$index]
		$job.Drain.Wait()
		if ($job.Process.ExitCode -ne 0) { $failed++ }
		$job.Process.Dispose()
		$running.RemoveAt($index)
	}
	@($stopwatch.Elapsed.TotalMilliseconds, $failed)
}

"workload,binary,cap,repeat,ms,failed" | Set-Content $OutCsv
foreach ($workload in $workloadArgs.Keys)
{
	if ($Workloads.Count -and $workload -notin $Workloads) { continue }
	foreach ($binary in $orderedBinaries.Keys)
	{
		$arguments = $invariants + $workloadArgs[$workload]
		$null = Invoke-Batch $orderedBinaries[$binary] $arguments 8 # warm-up, discarded
		for ($repeat = 1; $repeat -le $Repeats; $repeat++)
		{
			# Shuffled per repeat, so drift over the run does not line up with the cap
			foreach ($cap in ($Caps | Get-Random -Count $Caps.Count))
			{
				$ms, $failed = Invoke-Batch $orderedBinaries[$binary] $arguments $cap
				"$workload,$binary,$cap,$repeat,$([math]::Round($ms, 1)),$failed" | Add-Content $OutCsv
			}
		}
	}
}

# Medians
$rows = Import-Csv $OutCsv
$rows | Group-Object workload, binary, cap | ForEach-Object {
	$sorted = @($_.Group | ForEach-Object { [double]$_.ms } | Sort-Object)
	[pscustomobject]@{
		workload = $_.Group[0].workload
		binary   = $_.Group[0].binary
		cap      = [int]$_.Group[0].cap
		medianMs = $sorted[[math]::Floor($sorted.Count / 2)]
		minMs    = $sorted[0]
		maxMs    = $sorted[-1]
		failed   = ($_.Group | Measure-Object -Property failed -Sum).Sum
	}
} | Sort-Object workload, binary, cap | Format-Table -AutoSize | Out-String -Width 200 | Set-Content $summaryPath
"{0} job directories, cycled to {1} jobs per batch; affinity mask 0x{2:X}" -f $jobDirs.Count, $dirs.Count, $AffinityMask | Add-Content $summaryPath
"per-run times: $OutCsv" | Add-Content $summaryPath
Get-Content $summaryPath
