#pragma once

#include <QtGlobal> // qlonglong

// Every storage key with its default. The values are read and written through CSettings at the use sites.
namespace Settings {

// An empty stored value (a cleared Preferences field) also means the default
inline constexpr const char* GitExecutableKey = "GitExecutable";
inline constexpr const char* GitExecutableDefault = "git"; // from PATH
inline constexpr const char* HgExecutableKey = "HgExecutable";
inline constexpr const char* HgExecutableDefault = "hg";

// A whole command line, %path% standing for the file to edit. The default is empty where no editor ships
// with every install, and the Edit action then reports that nothing is configured.
inline constexpr const char* TextEditorCommandKey = "TextEditorCommand";
#ifdef Q_OS_WIN
inline constexpr const char* TextEditorCommandDefault = "notepad.exe %path%";
#elif defined Q_OS_MACOS
inline constexpr const char* TextEditorCommandDefault = "open -t %path%"; // the default text editor, TextEdit unless changed
#else
inline constexpr const char* TextEditorCommandDefault = "";
#endif

// An empty family means no override (the system fixed font at its own size); a size of 0 keeps the family's default size
inline constexpr const char* MonospaceFontFamilyKey = "MonospaceFontFamily";
inline constexpr const char* MonospaceFontPointSizeKey = "MonospaceFontPointSize";

inline constexpr const char* HistoryMaxCommitsKey = "HistoryMaxCommits";
inline constexpr int HistoryMaxCommitsDefault = 20000;

inline constexpr const char* MaxShownDiffBytesKey = "MaxShownDiffBytes";
inline constexpr qlonglong MaxShownDiffBytesDefault = 2LL * 1024LL * 1024LL;

inline constexpr const char* ShowLineEndingOnlyChangesKey = "ShowLineEndingOnlyChanges";
inline constexpr bool ShowLineEndingOnlyChangesDefault = false;

inline constexpr const char* SubjectGuideColumnKey = "SubjectGuideColumn";
inline constexpr int SubjectGuideColumnDefault = 50;

inline constexpr const char* CompletionAutoPopupKey = "CompletionAutoPopup";
inline constexpr bool CompletionAutoPopupDefault = true;
inline constexpr const char* CompletionMinPrefixLengthKey = "CompletionMinPrefixLength";
inline constexpr int CompletionMinPrefixLengthDefault = 3;

// Stored as text rather than an enum value, so reordering the enum cannot repoint stored settings. Tracked
// is the default, and what unknown text reads as.
inline constexpr const char* NewRowCheckPolicyKey = "NewRowCheckPolicy";
inline constexpr const char* NewRowCheckPolicyTracked = "tracked";
inline constexpr const char* NewRowCheckPolicyAll = "all";
inline constexpr const char* NewRowCheckPolicyNone = "none";

inline constexpr const char* DiffTabWidthKey = "DiffTabWidth";
inline constexpr int DiffTabWidthDefault = 4;

// A QSettings array, most recent first, whose elements carry the keys below. Kinds are stored as text for
// the same reason as the check policy.
inline constexpr const char* RecentRepositoriesKey = "RecentRepositories";
inline constexpr const char* RecentRepositoryRootKey = "root";
inline constexpr const char* RecentRepositoryKindKey = "kind";
inline constexpr const char* RecentRepositoryLastUsedKey = "lastUsed";
// Two parallel lists
inline constexpr const char* RecentRepositorySubrepoPathsKey = "subrepoPaths";
inline constexpr const char* RecentRepositorySubrepoKindsKey = "subrepoKinds";
inline constexpr const char* VcsKindGit = "git";
inline constexpr const char* VcsKindMercurial = "hg";

// One key per window kind, shared by every repository, like the window geometry
inline constexpr const char* CommitWindowSplitterKey = "CommitWindow/splitterState";
inline constexpr const char* HistoryWindowSplitterKey = "HistoryWindow/splitterState";
inline constexpr const char* HistoryWindowDetailSplitterKey = "HistoryWindowDetail/splitterState";

} // namespace Settings
