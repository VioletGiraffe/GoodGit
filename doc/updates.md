# Update check

The one network access outside git and hg: the GitHub releases of `VioletGiraffe/GoodGit`, read through the
`github-releases-autoupdater` library's dialog, which compares `GG_VERSION` against the tag names. It offers the
most recent newer release, pre-releases included and marked as such. On Windows it downloads and launches the
installer, elsewhere it opens the download link. Help > Check for Updates reports either answer. The automatic check runs at
startup, at most once a day, when the setting allows: it starts before any window is up and shows nothing
unless there is an update.
