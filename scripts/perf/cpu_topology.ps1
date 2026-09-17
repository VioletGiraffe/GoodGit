#Requires -Version 7
<#
.SYNOPSIS
Efficiency class of each logical processor. Windows only.

.DESCRIPTION
Which logical processors are the slow cores on a hybrid CPU: class 0 is the least performant, so on a
current Intel part those are the E-cores. Needed to read the other perf scripts' results on such a
machine, and to build the affinity masks they take: a batch pinned to the E-cores stands in for a weaker
machine than the one measuring.
#>

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CpuSets
{
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool GetSystemCpuSetInformation(IntPtr information, uint bufferLength, out uint returnedLength, IntPtr process, uint flags);

	// SYSTEM_CPU_SET_INFORMATION: Size (4), Type (4), then CpuSet: Id (4), Group (2), LogicalProcessorIndex (1), CoreIndex (1),
	// LastLevelCacheIndex (1), NumaNodeIndex (1), EfficiencyClass (1)
	public static string[] Describe()
	{
		GetSystemCpuSetInformation(IntPtr.Zero, 0, out uint needed, IntPtr.Zero, 0);
		IntPtr buffer = Marshal.AllocHGlobal((int)needed);
		try
		{
			if (!GetSystemCpuSetInformation(buffer, needed, out needed, IntPtr.Zero, 0))
				throw new System.ComponentModel.Win32Exception();
			var lines = new System.Collections.Generic.List<string>();
			for (int offset = 0; offset < needed; )
			{
				int size = Marshal.ReadInt32(buffer, offset);
				byte logical = Marshal.ReadByte(buffer, offset + 14);
				byte core = Marshal.ReadByte(buffer, offset + 15);
				byte efficiency = Marshal.ReadByte(buffer, offset + 18);
				lines.Add(string.Format("logical {0,2}  core {1,2}  efficiency class {2}", logical, core, efficiency));
				offset += size;
			}
			return lines.ToArray();
		}
		finally { Marshal.FreeHGlobal(buffer); }
	}
}
'@
[CpuSets]::Describe()
