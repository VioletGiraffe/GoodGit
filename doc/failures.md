# Failure reporting

A failed command surfaces its own stderr verbatim (qtutils `MessageBox::notice`, scrollable details): hook
output is what makes a rejected commit diagnosable. A command that never launched, died mid-run, or ran out
of time says which. A push step that failed for want of an upstream is offered a retry that sets one; the
backend decides which failure that is and names the upstream. Non-fast-forward is reported plainly, with no
offer to pull or force.

Delete goes to the OS trash and never falls back to permanent deletion. The external diff tool is launched
detached, bypassing the job queue: it blocks the VCS process until the tool closes, and a queue slot held
for minutes would starve refreshes.
