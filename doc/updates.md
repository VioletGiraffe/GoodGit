# Update check

The one network access outside git and hg: the GitHub releases of `VioletGiraffe/GoodGit`, read through the
`github-releases-autoupdater` library's dialog, which compares `GG_VERSION` against the tag names and
downloads the platform's asset. Help > Check for Updates reports either answer. The automatic check runs at
startup, at most once a day, when the setting allows: it starts before any window is up and shows nothing
unless there is an update.
