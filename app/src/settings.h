#pragma once

#include <QtGlobal> // qlonglong

// The settings vocabulary: every storage key, with its default beside it. The values themselves are
// read and written through qtutils CSettings directly at the consumption sites.
namespace Settings {

// The executables also read empty as the default: that is what a cleared Preferences field stores
inline constexpr const char* GitExecutableKey = "GitExecutable";
inline constexpr const char* GitExecutableDefault = "git"; // from PATH
inline constexpr const char* HgExecutableKey = "HgExecutable";
inline constexpr const char* HgExecutableDefault = "hg";

// An empty family means no override - the system fixed font, at its own size - and a size of 0 leaves
// the family's default size
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

// Stored as text rather than an enum's number, so a reorder could never silently repoint an existing
// user's setting. Tracked is the default, and what unknown text reads as.
inline constexpr const char* NewRowCheckPolicyKey = "NewRowCheckPolicy";
inline constexpr const char* NewRowCheckPolicyTracked = "tracked";
inline constexpr const char* NewRowCheckPolicyAll = "all";
inline constexpr const char* NewRowCheckPolicyNone = "none";

inline constexpr const char* DiffTabWidthKey = "DiffTabWidth";
inline constexpr int DiffTabWidthDefault = 4;

// One key per window kind, shared by every repository, as the window geometry beside them is
inline constexpr const char* CommitWindowSplitterKey = "CommitWindow/splitterState";
inline constexpr const char* HistoryWindowSplitterKey = "HistoryWindow/splitterState";
inline constexpr const char* HistoryWindowDetailSplitterKey = "HistoryWindowDetail/splitterState";

} // namespace Settings
