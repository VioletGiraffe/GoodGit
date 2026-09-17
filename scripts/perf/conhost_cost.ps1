#Requires -Version 7
<#
.SYNOPSIS
What a child process's console costs: CREATE_NO_WINDOW against DETACHED_PROCESS. Windows only.

.DESCRIPTION
Why `vcsprocess` starts every child detached. Windows attaches a console to a child started the way Qt starts
one, and every console is a conhost.exe process; detaching does without both. The run prints what a single
child spawns under each flag, checks that git's output is identical either way, then times batches at a
ladder of concurrency caps.

CreateProcessW is called directly: neither QProcess nor .NET exposes DETACHED_PROCESS. The flag is the only
difference between the two arms - same command, same job list, output to NUL in both.

.EXAMPLE
scripts/perf/conhost_cost.ps1 -Repos C:\path\to\superproject -Caps 1,8,16 -Repeats 5
#>
param(
	[Parameter(Mandatory)][string[]]$Repos,
	[string]$Exe = (Get-Command git).Source,
	[int[]]$Caps = @(1, 16),
	[int]$Repeats = 7,
	[ValidateSet('both', 'nowindow', 'detached')][string]$Flags = 'both'
)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

public static class Launcher
{
	const uint CREATE_NO_WINDOW = 0x08000000, DETACHED_PROCESS = 0x00000008, STARTF_USESTDHANDLES = 0x00000100;
	const uint GENERIC_WRITE = 0x40000000, FILE_SHARE_WRITE = 2, OPEN_EXISTING = 3, INFINITE = 0xFFFFFFFF;

	[StructLayout(LayoutKind.Sequential)] struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public uint dwProcessId, dwThreadId; }
	[StructLayout(LayoutKind.Sequential)] struct SECURITY_ATTRIBUTES { public int nLength; public IntPtr lpSecurityDescriptor; public bool bInheritHandle; }
	[StructLayout(LayoutKind.Sequential)] struct STARTUPINFO
	{
		public int cb; public IntPtr lpReserved, lpDesktop, lpTitle;
		public uint dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
		public short wShowWindow; public short cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
	}

	[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	static extern bool CreateProcessW(string app, string commandLine, IntPtr procAttrs, IntPtr threadAttrs, bool inherit,
		uint flags, IntPtr env, string currentDirectory, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
	[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	static extern IntPtr CreateFileW(string name, uint access, uint share, ref SECURITY_ATTRIBUTES sa, uint disposition, uint flags, IntPtr template);
	[DllImport("kernel32.dll", SetLastError = true)] static extern bool CloseHandle(IntPtr h);
	[DllImport("kernel32.dll", SetLastError = true)] static extern uint WaitForMultipleObjects(uint count, IntPtr[] handles, bool waitAll, uint ms);
	[DllImport("kernel32.dll", SetLastError = true)] static extern bool GetExitCodeProcess(IntPtr h, out uint code);

	const uint CREATE_ALWAYS = 2;

	// The child's stdout and stderr: NUL, or a file to capture what it wrote
	static IntPtr OutputHandle(string path)
	{
		var sa = new SECURITY_ATTRIBUTES { nLength = Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES)), bInheritHandle = true };
		IntPtr h = string.IsNullOrEmpty(path)
			? CreateFileW("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, ref sa, OPEN_EXISTING, 0, IntPtr.Zero)
			: CreateFileW(path, GENERIC_WRITE, FILE_SHARE_WRITE, ref sa, CREATE_ALWAYS, 0, IntPtr.Zero);
		if (h == new IntPtr(-1)) throw new System.ComponentModel.Win32Exception();
		return h;
	}

	// Launches one child and returns its pid and handle, for inspecting what it spawns while it runs
	public static uint StartOne(string exe, string commandLine, string dir, bool detached, out IntPtr handle)
	{
		uint flags = detached ? DETACHED_PROCESS : CREATE_NO_WINDOW;
		IntPtr nul = OutputHandle("");
		var si = new STARTUPINFO { cb = Marshal.SizeOf(typeof(STARTUPINFO)), dwFlags = STARTF_USESTDHANDLES,
			hStdInput = IntPtr.Zero, hStdOutput = nul, hStdError = nul };
		PROCESS_INFORMATION pi;
		if (!CreateProcessW(exe, commandLine, IntPtr.Zero, IntPtr.Zero, true, flags, IntPtr.Zero, dir, ref si, out pi))
			throw new System.ComponentModel.Win32Exception();
		CloseHandle(pi.hThread);
		CloseHandle(nul);
		handle = pi.hProcess;
		return pi.dwProcessId;
	}

	public static void WaitOne(IntPtr handle)
	{
		WaitForMultipleObjects(1, new IntPtr[] { handle }, false, INFINITE);
		CloseHandle(handle);
	}

	// Wall milliseconds for `jobs` launches of one command over `dirs`, at most `cap` at a time
	public static double Batch(string exe, string commandLine, string[] dirs, int cap, bool detached, string outPath, out int failures)
	{
		uint flags = detached ? DETACHED_PROCESS : CREATE_NO_WINDOW;
		IntPtr nul = OutputHandle(outPath);
		var running = new List<IntPtr>();
		int next = 0, failed = 0;
		var sw = Stopwatch.StartNew();
		try
		{
			while (next < dirs.Length || running.Count > 0)
			{
				while (running.Count < cap && next < dirs.Length)
				{
					var si = new STARTUPINFO { cb = Marshal.SizeOf(typeof(STARTUPINFO)), dwFlags = STARTF_USESTDHANDLES,
						hStdInput = IntPtr.Zero, hStdOutput = nul, hStdError = nul };
					PROCESS_INFORMATION pi;
					if (!CreateProcessW(exe, commandLine, IntPtr.Zero, IntPtr.Zero, true, flags, IntPtr.Zero, dirs[next], ref si, out pi))
						throw new System.ComponentModel.Win32Exception();
					CloseHandle(pi.hThread);
					running.Add(pi.hProcess);
					next++;
				}
				IntPtr[] handles = running.ToArray();
				// WaitForMultipleObjects takes 1 to 64 handles and rejects duplicates: fail with the reason, not with its error code
				if (handles.Length == 0 || handles.Length > 64) throw new Exception("cap out of range: wait array length " + handles.Length);
				uint index = WaitForMultipleObjects((uint)handles.Length, handles, false, INFINITE);
				if (index >= (uint)handles.Length) // WAIT_FAILED, or an abandoned wait: report why rather than indexing past the end
					throw new System.ComponentModel.Win32Exception();
				IntPtr done = handles[(int)index];
				uint code; GetExitCodeProcess(done, out code);
				if (code != 0) failed++;
				CloseHandle(done);
				running.RemoveAt((int)index);
			}
		}
		finally { CloseHandle(nul); }
		failures = failed;
		return sw.Elapsed.TotalMilliseconds;
	}
}
'@

$dirs = foreach ($root in $Repos) {
	$subs = @(& git -C $root submodule status --recursive | Where-Object { $_ -notmatch '^-' } | ForEach-Object { Join-Path $root (($_.Trim() -split ' ')[1]) })
	if ($subs.Count) { $subs } else { $root }
}
$dirs = [string[]]$dirs
$commandLine = '"' + $Exe + '" -c core.quotepath=false --no-optional-locks status --porcelain -z'

# Same command both ways, output captured: the flag must not change what git writes
$failures = 0
$one = [string[]]@($dirs[0])
$noWindowOut = Join-Path $env:TEMP 'conhost_nowindow.out'
$detachedOut = Join-Path $env:TEMP 'conhost_detached.out'
$listingCommandLine = '"' + $Exe + '" -c core.quotepath=false --no-optional-locks ls-files --stage -z'
$null = [Launcher]::Batch($Exe, $listingCommandLine, $one, 1, $false, $noWindowOut, [ref]$failures)
$null = [Launcher]::Batch($Exe, $listingCommandLine, $one, 1, $true, $detachedOut, [ref]$failures)
$sameOutput = (Get-FileHash $noWindowOut).Hash -eq (Get-FileHash $detachedOut).Hash
"output identical both ways: $sameOutput ($((Get-Item $noWindowOut).Length) bytes)"

# What one child spawns, per flag: a superproject `status` recurses into every submodule, so it lives long enough to look at
foreach ($detached in $false, $true) {
	$handle = [IntPtr]::Zero
	$childPid = [Launcher]::StartOne($Exe, $commandLine, $Repos[0], $detached, [ref]$handle)
	Start-Sleep -Milliseconds 400
	# Grandchildren too: a wrapper's own child is a console program, and one started by a process without a
	# console gets a console of its own unless its launcher says otherwise
	$children = foreach ($child in (Get-CimInstance Win32_Process -Filter "ParentProcessId = $childPid")) {
		$grandchildren = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $($child.ProcessId)" | Select-Object -ExpandProperty Name)
		if ($grandchildren.Count) { "$($child.Name) -> [$($grandchildren -join ', ')]" } else { $child.Name }
	}
	"{0,-18} pid {1}: children [{2}]" -f $(if ($detached) { 'DETACHED_PROCESS' } else { 'CREATE_NO_WINDOW' }), $childPid, ($children -join ', ')
	[Launcher]::WaitOne($handle)
}

$rows = foreach ($cap in $Caps) {
	foreach ($detached in $false, $true) {
		$failures = 0
		$null = [Launcher]::Batch($Exe, $commandLine, $dirs, $cap, $detached, '', [ref]$failures) # warm-up
		$times = foreach ($i in 1..$Repeats) { [Launcher]::Batch($Exe, $commandLine, $dirs, $cap, $detached, '', [ref]$failures) }
		$sorted = @($times | Sort-Object)
		[pscustomobject]@{
			cap = $cap
			flag = $(if ($detached) { 'DETACHED_PROCESS' } else { 'CREATE_NO_WINDOW' })
			medianMs = [math]::Round($sorted[[math]::Floor($sorted.Count / 2)], 1)
			perJobMs = [math]::Round($sorted[[math]::Floor($sorted.Count / 2)] / $dirs.Count, 2)
			failures = $failures
		}
	}
}
"$($dirs.Count) jobs per batch"
$rows | Format-Table -AutoSize
