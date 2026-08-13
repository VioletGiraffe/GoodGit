'use strict';

/* Generates mockup.html beside this file. Run: node doc/UI/mockup.gen.js
   The markup is repetitive enough that hand-editing it invites inconsistency between rows,
   so the mockup is generated and mockup.html is not edited directly. */

const fs = require('fs');
const path = require('path');

/* ============================ theme ============================ */

const LIGHT_VARS = `
	--page-bg:#e8eaed; --page-fg:#16181c; --page-dim:#5d636e;
	--win-bg:#f3f3f3; --pane:#ffffff; --pane-alt:#fafafa;
	--border:#d0d3d8; --border-soft:#e3e5e9;
	--text:#16181c; --dim:#6b7280;
	--accent:#0d6bc4; --accent-fg:#ffffff;
	--sel:#d6e8fb; --btn:#fdfdfd; --btn-border:#c2c6cc;
	--warn-bg:#fff4d6;
	--st-mod:#1668c4; --st-add:#12783c; --st-unt:#8a6a08; --st-del:#b8302a;
	--st-ren:#7345c0; --st-sub:#a15c00;
	--diff-add-bg:#e3f7e8; --diff-add-fg:#0f5f2e;
	--diff-del-bg:#fdeaea; --diff-del-fg:#8f2318;
	--diff-hunk:#6a5fb0; --diff-ctx:#2b2f36;
	--shadow:rgba(0,0,0,.14);
`;

const DARK_VARS = `
	--page-bg:#14161a; --page-fg:#e6e8ec; --page-dim:#9aa1ad;
	--win-bg:#1f2227; --pane:#191c21; --pane-alt:#1c1f24;
	--border:#33383f; --border-soft:#282c32;
	--text:#e6e8ec; --dim:#98a0ab;
	--accent:#3b8fe0; --accent-fg:#06121f;
	--sel:#1e3c58; --btn:#262a30; --btn-border:#3c424a;
	--warn-bg:#3a3013;
	--st-mod:#6cb0f0; --st-add:#62c98a; --st-unt:#d4b352; --st-del:#ef8c82;
	--st-ren:#b394ef; --st-sub:#dda45c;
	--diff-add-bg:#16341f; --diff-add-fg:#8fdca8;
	--diff-del-bg:#3a1d1c; --diff-del-fg:#f0a79c;
	--diff-hunk:#9b8fe0; --diff-ctx:#c9ced6;
	--shadow:rgba(0,0,0,.5);
`;

const CSS = `
:root { ${LIGHT_VARS} }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { ${DARK_VARS} } }
:root[data-theme="dark"] { ${DARK_VARS} }

* { box-sizing: border-box; }
html, body { margin: 0; padding: 0; }
body {
	background: var(--page-bg); color: var(--page-fg);
	font: 14px/1.5 "Segoe UI", system-ui, -apple-system, sans-serif;
	padding-bottom: 60px;
}
.wrap { max-width: 1380px; margin: 0 auto; padding: 0 24px; }
.masthead { padding: 30px 0 18px; }
.masthead h1 { margin: 0 0 6px; font-size: 26px; font-weight: 650; letter-spacing: -0.01em; }
.masthead p { margin: 0 0 8px; color: var(--page-dim); max-width: 92ch; }
code { font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: .92em; }
ul.notes { margin: 22px 0 0; padding-left: 20px; color: var(--page-dim); max-width: 100ch; line-height: 1.75; }
ul.notes b { color: var(--page-fg); font-weight: 600; }

/* ---------- window shell ---------- */
.win {
	width: 1180px; max-width: 100%; height: 740px;
	background: var(--win-bg); border: 1px solid var(--border); border-radius: 8px;
	box-shadow: 0 10px 30px var(--shadow), 0 2px 6px rgba(0,0,0,.07);
	overflow: hidden; display: flex; flex-direction: column;
	color: var(--text); font-size: 13px;
}
.titlebar {
	display: flex; align-items: center; gap: 8px; height: 32px; flex: 0 0 32px;
	padding-left: 12px; background: var(--pane-alt); border-bottom: 1px solid var(--border);
	font-size: 12.5px; color: var(--dim);
}
.titlebar .tt { color: var(--text); }
.titlebar .wbtns { margin-left: auto; display: flex; }
.titlebar .wbtns i { width: 44px; height: 32px; display: grid; place-items: center; font-style: normal; font-size: 11px; color: var(--dim); }

.repobar { display: flex; align-items: center; gap: 10px; padding: 8px 12px; background: var(--pane-alt); border-bottom: 1px solid var(--border); flex: 0 0 auto; }
.repobar .repo { font-weight: 650; }
.repobar .branch { font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 12px; background: var(--pane); border: 1px solid var(--border); border-radius: 4px; padding: 1px 7px; }
.repobar .ab { color: var(--accent); font-size: 12px; font-weight: 600; }
.grow { flex: 1; }
.row { display: flex; flex: 1; min-height: 0; }
.col { display: flex; flex-direction: column; min-height: 0; min-width: 0; }

/* ---------- message ---------- */
.msg { display: flex; flex-direction: column; flex: 0 0 auto; background: var(--win-bg); }
.msg .lbl { display: flex; align-items: center; gap: 8px; padding: 7px 12px 5px; font-size: 12px; color: var(--dim); }
.msg .box {
	margin: 0 12px 10px; background: var(--pane); border: 1px solid var(--btn-border); border-radius: 4px;
	padding: 7px 9px; position: relative; overflow: hidden; min-height: 96px;
	font-family: ui-monospace, "Cascadia Mono", Consolas, monospace;
	font-size: 12.5px; line-height: 1.55; color: var(--text);
}
.msg .box .caret { display: inline-block; width: 1px; height: 1.05em; background: var(--text); vertical-align: text-bottom; }
/* Subject-length guide. 50 columns, not 72: the body-wrap convention does not fit this column width. */
.msg .box .ruler { position: absolute; top: 0; bottom: 0; left: calc(9px + 50ch); width: 1px; background: var(--border-soft); }
.msg .box .ruler::after { content: "50"; position: absolute; top: 2px; left: 3px; font-size: 9px; color: var(--dim); opacity: .6; font-family: "Segoe UI", sans-serif; }

/* ---------- file list ---------- */
.files { display: flex; flex-direction: column; min-height: 0; min-width: 0; background: var(--pane); }
.files .bar { display: flex; align-items: center; gap: 8px; padding: 6px 10px; font-size: 12px; color: var(--dim); background: var(--win-bg); border-bottom: 1px solid var(--border); flex: 0 0 auto; }
.files .rows { flex: 1; min-height: 0; overflow: auto; }
.frow { display: flex; align-items: center; gap: 9px; padding: 4px 10px; border-bottom: 1px solid var(--border-soft); white-space: nowrap; }
.frow.sel { background: var(--sel); box-shadow: inset 2px 0 0 var(--accent); }
.frow .nocb { width: 14px; flex: 0 0 14px; text-align: center; color: var(--dim); opacity: .55; font-size: 11px; }
.frow .state { flex: 0 0 auto; font-size: 11.5px; font-weight: 600; min-width: 78px; }
.frow .path { font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 12px; overflow: hidden; text-overflow: ellipsis; }
.frow .ico { flex: 0 0 14px; font-size: 12px; }
.frow.del .path { text-decoration: line-through; text-decoration-color: var(--st-del); text-decoration-thickness: 1px; }
.frow.disabled { opacity: .8; }
.frow.blocked { background: color-mix(in srgb, var(--warn-bg) 62%, var(--pane)); }

/* Checkboxes are drawn rather than <input>, so the page renders identically with scripting off. */
.cb { width: 13px; height: 13px; flex: 0 0 13px; border: 1px solid var(--btn-border); border-radius: 3px; background: var(--pane); display: inline-grid; place-items: center; }
.cb.on { background: var(--accent); border-color: var(--accent); }
.cb.on::after { content: "\\2713"; color: var(--accent-fg); font-size: 10px; line-height: 1; }
.cb.tri { background: var(--accent); border-color: var(--accent); }
.cb.tri::after { content: ""; width: 7px; height: 2px; background: var(--accent-fg); }

.s-mod { color: var(--st-mod); } .s-add { color: var(--st-add); }
.s-unt { color: var(--st-unt); } .s-del { color: var(--st-del); }
.s-ren { color: var(--st-ren); } .s-sub { color: var(--st-sub); }

/* ---------- diff ---------- */
.diff { display: flex; flex-direction: column; min-height: 0; min-width: 0; background: var(--pane); }
.diff .dhead { display: flex; align-items: center; gap: 9px; padding: 6px 10px; background: var(--win-bg); border-bottom: 1px solid var(--border); flex: 0 0 auto; font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 12px; }
.diff .dhead .tag { font-family: "Segoe UI", sans-serif; font-size: 11.5px; color: var(--dim); }
.diff pre { margin: 0; flex: 1; min-height: 0; overflow: auto; font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 12px; line-height: 1.5; color: var(--diff-ctx); tab-size: 4; }
.diff pre .l { display: block; padding: 0 10px; min-height: 1.5em; }
.diff pre .h { color: var(--diff-hunk); background: var(--pane-alt); }
.diff pre .a { background: var(--diff-add-bg); color: var(--diff-add-fg); }
.diff pre .d { background: var(--diff-del-bg); color: var(--diff-del-fg); }

/* ---------- buttons ---------- */
.btn { font: inherit; font-size: 12.5px; padding: 5px 14px; background: var(--btn); color: var(--text); border: 1px solid var(--btn-border); border-radius: 4px; white-space: nowrap; }
.btn.primary { background: var(--accent); color: var(--accent-fg); border-color: var(--accent); font-weight: 600; }
.btn.big { padding: 8px 14px; width: 100%; font-size: 13px; }
.btn.small { padding: 3px 9px; }
`;

/* ============================ sample content ============================ */

/* One row per state the list can show, so the styling of each is visible at once. */
const FILES = [
	{ chk: 1,    st: 'Modified',  cls: 'mod', path: 'app/src/commitwindow.cpp' },
	{ chk: 1,    st: 'Modified',  cls: 'mod', path: 'app/src/main.cpp' },
	{ chk: 1,    st: 'Added',     cls: 'add', path: 'app/src/repository.h' },
	{ chk: 1,    st: 'Renamed',   cls: 'ren', path: 'app/src/gitprocess.cpp' },
	{ chk: 1,    st: 'Deleted',   cls: 'del', path: 'app/src/old_widget.cpp' },
	{ chk: 0,    st: 'Untracked', cls: 'unt', path: 'app/src/diffview.cpp' },
	{ chk: 0,    st: 'Untracked', cls: 'unt', path: 'build/moc_commitwindow.o' },
	{ chk: 1,    st: 'Submodule', cls: 'sub', path: 'cpputils', ico: 1 },
	{ chk: null, st: 'Submodule &mdash; blocked', cls: 'del', path: 'thin_io', ico: 1, blocked: 1 },
	{ chk: null, st: 'Uncommitted inside', cls: 'unt', path: 'cpp-template-utils', ico: 1, blocked: 1 },
];

const DIFF = [
	['h', '@@ -9,14 +9,21 @@ CommitWindow::CommitWindow(Repository& repo, QWidget* parent)'],
	['c', '\tui->setupUi(this);'],
	['c', ''],
	['c', '\tui->filesView->setModel(&_filesModel);'],
	['d', '-\tui->filesView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);'],
	['a', '+\tauto* const header = ui->filesView->horizontalHeader();'],
	['a', '+\theader->setSectionResizeMode(ColumnCheck, QHeaderView::Fixed);'],
	['a', '+\theader->setSectionResizeMode(ColumnPath,  QHeaderView::Stretch);'],
	['c', ''],
	['d', '-\tconnect(ui->filesView, &QTreeView::activated, this, &CommitWindow::openDiffTool);'],
	['a', '+\tconnect(ui->filesView, &QAbstractItemView::activated, this, &CommitWindow::openDiffTool);'],
	['a', '+\tconnect(ui->filesView->selectionModel(), &QItemSelectionModel::currentChanged,'],
	['a', '+\t        this, &CommitWindow::showDiffForCurrentRow);'],
	['c', ''],
	['c', '\trestoreGeometry(Settings::commitWindowGeometry());'],
	['c', '}'],
	['c', ''],
	['h', '@@ -41,9 +48,18 @@ void CommitWindow::commit()'],
	['c', '{'],
	['c', '\tconst auto paths = _filesModel.checkedPaths();'],
	['c', '\tassert(!paths.empty());'],
	['c', ''],
	['d', '-\t_repo.commit(ui->messageEdit->toPlainText(), paths);'],
	['a', '+\tconst auto untracked = _filesModel.checkedUntrackedPaths();'],
	['a', '+\tif (!untracked.empty() && !confirmStartTracking(untracked))'],
	['a', '+\t\treturn;'],
	['a', '+'],
	['a', '+\t_repo.commit(ui->messageEdit->toPlainText(), paths, [this](const GitResult& result) {'],
	['a', '+\t\tif (!result.ok)'],
	['a', '+\t\t\tshowGitError(tr("Commit failed"), result);'],
	['a', '+'],
	['a', '+\t\trefresh();'],
	['a', '+\t});'],
	['c', '}'],
];

const MSG_SUBJECT = 'Commit checked files without disturbing the index';

const esc = s => String(s).replace(/&(?!\w+;)/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

/* ============================ markup ============================ */

const cb = state => state === null
	? '<span class="nocb">&mdash;</span>'
	: `<span class="cb ${state === 'tri' ? 'tri' : state ? 'on' : ''}"></span>`;

const frow = (f, selected) => {
	const cls = ['frow', selected ? 'sel' : '', f.cls === 'del' ? 'del' : '',
		f.blocked ? 'disabled blocked' : ''].filter(Boolean).join(' ');
	return `<div class="${cls}">${cb(f.chk)}${f.ico ? '<span class="ico">&#128193;</span>' : ''}
				<span class="state s-${f.cls}">${f.st}</span>
				<span class="path">${esc(f.path)}</span></div>`;
};

const filelist = () => {
	const checked = FILES.filter(f => f.chk === 1).length;
	return `<div class="files col" style="flex:1">
			<div class="bar">${cb('tri')}<span>${checked} of ${FILES.length} checked</span></div>
			<div class="rows">
				${FILES.map((f, i) => frow(f, i === 0)).join('\n\t\t\t\t')}
			</div>
		</div>`;
};

const diffpane = () => `<div class="diff col" style="flex:1">
			<div class="dhead"><span>app/src/commitwindow.cpp</span><span class="grow"></span>
				<span class="tag">HEAD &rarr; working tree</span></div>
			<pre>${DIFF.map(([t, s]) => `<span class="l ${t === 'c' ? '' : t}">${esc(s) || '&nbsp;'}</span>`).join('')}</pre>
		</div>`;

/* Left column is 430px because that is what the 50-column subject guide needs: at this width the
   editor holds just over 50 monospace characters. Narrower and the guide falls outside the box. */
const window_ = () => `<div class="win">
	<div class="titlebar"><span class="tt">GoodGit [master] - GoodGit</span>
		<span class="wbtns"><i>&#8210;</i><i>&#9633;</i><i>&#10005;</i></span></div>
	<div class="row">
		<div class="col" style="flex:0 0 430px;border-right:1px solid var(--border)">
			<div class="repobar">
				<span class="repo">GoodGit</span><span class="branch">master</span><span class="ab">&uarr;3</span>
				<span class="grow"></span>
				<button class="btn small">Push (3)</button><button class="btn small">Refresh</button>
			</div>
			${filelist()}
			<div style="border-top:1px solid var(--border)">
				<div class="msg">
					<div class="lbl"><span>Commit message</span></div>
					<div class="box"><span class="ruler"></span>
						<div>${esc(MSG_SUBJECT)}<span class="caret"></span></div></div>
				</div>
				<div style="padding:0 12px 12px;display:flex;flex-direction:column;gap:6px">
					<button class="btn primary big">Commit 6 files to master</button>
					<button class="btn big">Commit &amp; Push</button>
				</div>
			</div>
		</div>
		${diffpane()}
	</div>
</div>`;

const NOTES = [
	'Left column 430&#8239;px, set by the subject guide: the editor holds just over 50 monospace characters at this width.',
	'<b>50-column subject marker, not 72.</b> The body-wrap convention does not fit in this column and is dropped; the subject one does, and is the one that matters.',
	'<b>Push and Refresh live in the repo header row</b>, not stacked under the primary button, which puts Push next to the ahead-count that justifies it.',
	'Single path column; renames render as <code>new/path (was old/path)</code> where width allows. Check-all is the tri-state box in the counter line above the list.',
	'Submodule rows carry the folder icon. The two conditions that cannot be committed &mdash; pointer moved but dirty inside, and dirty with the pointer unmoved &mdash; take a warning tint as well as their state wording.',
	'Not drawn: the merge-mode and detached-HEAD strips, which appear directly beneath the repo header row and change how the whole window behaves.',
];

const html = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GoodGit &mdash; commit window mockup</title>
<!-- Generated by mockup.gen.js. Edit that file, not this one. -->
<style>${CSS}</style>
</head>
<body>
<div class="wrap">
	<div class="masthead">
		<h1>GoodGit &mdash; commit window</h1>
		<p>The file list occupies the top of a fixed left column, the message and the primary action sit beneath it,
		   and the diff takes the full height of the remaining width. Sample content only; every row state is
		   populated at once so the styling of each is visible.</p>
		<p>Behaviour and the reasoning behind the design live in <code>doc/ARCHITECTURE.md</code>.</p>
	</div>
	${window_()}
	<ul class="notes">
		${NOTES.map(n => `<li>${n}</li>`).join('\n\t\t')}
	</ul>
</div>
</body>
</html>`;

const dest = path.join(__dirname, 'mockup.html');
fs.writeFileSync(dest, html, 'utf8');
console.log('wrote', dest, (html.length / 1024).toFixed(1) + ' KB');
