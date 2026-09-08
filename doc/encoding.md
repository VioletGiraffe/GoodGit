# Text encoding

Two encodings meet in this app, and mixing them up is silent rather than loud - the wrong one yields
mojibake, or a path that no longer matches the one it was read from.

- **Tool output is the tool's encoding.** Git writes UTF-8 on every platform. Mercurial writes the local
  8-bit encoding - the ANSI codepage on Windows - and reads the same back. `Vcs::Tool::textEncoding` states
  which, and travels into every `ProcessResult`, so `errorText()` decodes a failure without knowing whose it
  is. Non-JSON hg output is decoded by `Hg::textFromOutput`, the inverse of `Hg::localBytes`; a streamed log
  builds its decoder from the job's encoding, since a chunk may split a character.
- **hg's `-T json` is exempt**: hg escapes it to ASCII. A path byte the local encoding cannot decode arrives
  as a lone surrogate U+DC80..DCFF (hg's utf8b scheme), which `Hg::localBytes` turns back into that byte, so
  such a path round-trips through a pathspec unharmed.
- **A path is filesystem bytes, not text in a known encoding.** Git stores them verbatim, so on Unix a path
  is whatever the filesystem holds; `Git::pathFromOutput` and `Git::pathBytes` decode and encode it with the
  local 8-bit codec there, and as UTF-8 on Windows, where Git for Windows stores UTF-8 whatever the codepage.
  That is the codec Qt itself uses for file names, so a path from git and one from `QDir` mean the same file,
  and a path survives the trip back to git through argv, a `--pathspec-file-nul` list, an `update-index`
  record or an ignore pattern. Only paths: a commit message, an author and a ref stay UTF-8, git's own
  convention for them. Mercurial needs no platform fork - its paths ride the same local encoding as the rest
  of its output, `.hgsub` and `.hgsubstate` included: hg reads those files in that encoding too, and their
  paths are matched against the ones a status answers with.
- **File content is not tool output.** `decodedAsText` (qtutils `string/stringutils.h`) reads a byte order mark, falls back to UTF-8, and
  calls bytes with an early NUL binary. This holds whichever VCS produced them: the encoding is the file's
  business.
- A diff is the one stream carrying both - hg's own header lines and the file's content bytes - and is
  decoded as UTF-8 throughout, content being the bulk of it. A non-ASCII path in an hg diff header can
  therefore still display wrong.
- What none of this covers: a file whose name is not valid in the locale's encoding at all (a Latin-1 name
  under a UTF-8 locale). Decoding is lossy there, and no scheme fixes it end to end - Qt encodes argv and
  the working directory with the same lossy codec, so a path that reached git only through stdin could be
  made to round-trip, while one passed to `log`, `diff` or `cat-file` could not.
