'use strict';

/* Generates mockup.html beside this file. Run: node doc/UI/mockup.gen.js
   The markup is repetitive enough that hand-editing it invites inconsistency between rows,
   so the mockup is generated and mockup.html is not edited directly. */

const fs = require('fs');
const path = require('path');

/* ============================ themes ============================ */
/* Mirrors allThemes() in app/src/theme.cpp: same names, same values, plus the page shadow.
   Every window is rendered in both polarities; the page's selectors switch which theme each
   polarity's windows wear. */

const THEMES = {
	light: {
		'Honey': `
	--win-bg:#f7f0dd; --pane:#fffdf4; --pane-alt:#fbf6e7;
	--border:#dfd3ae; --border-soft:#efe7cd;
	--text:#221c0c; --dim:#857a58;
	--accent:#e8a013; --accent-fg:#241a00; --accent-text:#8a6600;
	--sel:#f5e6bb; --btn:#fdf9ec; --btn-border:#d4c79b;
	--warn-bg:#ffe6c2;
	--st-mod:#1668c4; --st-add:#0d9c3c; --st-unt:#0c7d84; --st-del:#dd2418;
	--st-ren:#7345c0; --st-sub:#a15c00;
	--diff-add-bg:#d1f2cf; --diff-add-fg:#07561f;
	--diff-del-bg:#fcd9d2; --diff-del-fg:#96140c;
	--diff-hunk:#6a5fb0; --diff-ctx:#322b18;
	--shadow:rgba(0,0,0,.14);
`,
		'Classic': `
	--win-bg:#f3f3f3; --pane:#ffffff; --pane-alt:#fafafa;
	--border:#d0d3d8; --border-soft:#e3e5e9;
	--text:#16181c; --dim:#6b7280;
	--accent:#0d6bc4; --accent-fg:#ffffff; --accent-text:#0d6bc4;
	--sel:#d6e8fb; --btn:#fdfdfd; --btn-border:#c2c6cc;
	--warn-bg:#fff4d6;
	--st-mod:#1668c4; --st-add:#12783c; --st-unt:#8a6a08; --st-del:#b8302a;
	--st-ren:#7345c0; --st-sub:#a15c00;
	--diff-add-bg:#e3f7e8; --diff-add-fg:#0f5f2e;
	--diff-del-bg:#fdeaea; --diff-del-fg:#8f2318;
	--diff-hunk:#6a5fb0; --diff-ctx:#2b2f36;
	--shadow:rgba(0,0,0,.14);
`,
		'Taxicab': `
	--win-bg:#f4f2ec; --pane:#fffef9; --pane-alt:#fbf9f2;
	--border:#d9d4c4; --border-soft:#ece8da;
	--text:#1c1a12; --dim:#7c7866;
	--accent:#f5c518; --accent-fg:#221b00; --accent-text:#8a6d00;
	--sel:#f7ecc0; --btn:#fbf9f2; --btn-border:#cfc9b6;
	--warn-bg:#ffe4c0;
	--st-mod:#1668c4; --st-add:#0d9c3c; --st-unt:#0c7d84; --st-del:#dd2418;
	--st-ren:#7345c0; --st-sub:#a15c00;
	--diff-add-bg:#d1f2cf; --diff-add-fg:#07561f;
	--diff-del-bg:#fcd9d2; --diff-del-fg:#96140c;
	--diff-hunk:#6a5fb0; --diff-ctx:#2a2820;
	--shadow:rgba(0,0,0,.14);
`,
		'Verdigris': `
	--win-bg:#f3ead9; --pane:#fffcf5; --pane-alt:#faf4e6;
	--border:#ddd0b8; --border-soft:#eee5d2;
	--text:#231e14; --dim:#8a7d63;
	--accent:#0d8577; --accent-fg:#ffffff; --accent-text:#0d8577;
	--sel:#d6ebde; --btn:#faf4e6; --btn-border:#d5c8a8;
	--warn-bg:#ffe3bd;
	--st-mod:#1668c4; --st-add:#0d9c3c; --st-unt:#8a6a08; --st-del:#dd2418;
	--st-ren:#7345c0; --st-sub:#a15c00;
	--diff-add-bg:#dff0d5; --diff-add-fg:#07561f;
	--diff-del-bg:#f8e2da; --diff-del-fg:#96140c;
	--diff-hunk:#6a5fb0; --diff-ctx:#2f2a1e;
	--shadow:rgba(0,0,0,.14);
`,
		'Firefly': `
	--win-bg:#eff3e8; --pane:#fdfff7; --pane-alt:#f7faee;
	--border:#d3dcc2; --border-soft:#e5ecd6;
	--text:#181d10; --dim:#717d64;
	--accent:#c8dc28; --accent-fg:#1c2000; --accent-text:#5f7000;
	--sel:#eef2bd; --btn:#f7faee; --btn-border:#c8d3b2;
	--warn-bg:#ffe4c0;
	--st-mod:#1668c4; --st-add:#0d9c3c; --st-unt:#0c7d84; --st-del:#dd2418;
	--st-ren:#7345c0; --st-sub:#a15c00;
	--diff-add-bg:#e3f0cc; --diff-add-fg:#07561f;
	--diff-del-bg:#f8e3da; --diff-del-fg:#96140c;
	--diff-hunk:#6a5fb0; --diff-ctx:#2a2f1e;
	--shadow:rgba(0,0,0,.14);
`,
	},
	dark: {
		'Blackout Violet': `
	--win-bg:#100a17; --pane:#09060c; --pane-alt:#0d0814;
	--border:#241a36; --border-soft:#170f24;
	--text:#e9e3f2; --dim:#9488a8;
	--accent:#ffc226; --accent-fg:#1f1800; --accent-text:#ffc226;
	--sel:#372d0d; --btn:#1a1128; --btn-border:#322447;
	--warn-bg:#46280e;
	--st-mod:#6cb0f0; --st-add:#38e07c; --st-unt:#45c9c0; --st-del:#ff6a5c;
	--st-ren:#b394ef; --st-sub:#dda45c;
	--diff-add-bg:#07150b; --diff-add-fg:#52ec92;
	--diff-del-bg:#1b0a09; --diff-del-fg:#ff8d80;
	--diff-hunk:#a795e8; --diff-ctx:#cec7dc;
	--shadow:rgba(0,0,0,.55);
`,
		'Classic': `
	--win-bg:#1f2227; --pane:#191c21; --pane-alt:#1c1f24;
	--border:#33383f; --border-soft:#282c32;
	--text:#e6e8ec; --dim:#98a0ab;
	--accent:#3b8fe0; --accent-fg:#06121f; --accent-text:#3b8fe0;
	--sel:#1e3c58; --btn:#262a30; --btn-border:#3c424a;
	--warn-bg:#3a3013;
	--st-mod:#6cb0f0; --st-add:#62c98a; --st-unt:#d4b352; --st-del:#ef8c82;
	--st-ren:#b394ef; --st-sub:#dda45c;
	--diff-add-bg:#16341f; --diff-add-fg:#8fdca8;
	--diff-del-bg:#3a1d1c; --diff-del-fg:#f0a79c;
	--diff-hunk:#9b8fe0; --diff-ctx:#c9ced6;
	--shadow:rgba(0,0,0,.5);
`,
		'Taxicab': `
	--win-bg:#1c1c1a; --pane:#151514; --pane-alt:#1a1a18;
	--border:#3a392f; --border-soft:#262620;
	--text:#e9e7e0; --dim:#9c9a8f;
	--accent:#f8ce1c; --accent-fg:#1f1800; --accent-text:#f8ce1c;
	--sel:#3f3a10; --btn:#26251f; --btn-border:#3f3e33;
	--warn-bg:#44290f;
	--st-mod:#6cb0f0; --st-add:#38e07c; --st-unt:#45c9c0; --st-del:#ff6a5c;
	--st-ren:#b394ef; --st-sub:#dda45c;
	--diff-add-bg:#122718; --diff-add-fg:#52ec92;
	--diff-del-bg:#2f1412; --diff-del-fg:#ff8d80;
	--diff-hunk:#9b8fe0; --diff-ctx:#ccc9c0;
	--shadow:rgba(0,0,0,.5);
`,
		'Forge': `
	--win-bg:#1a2233; --pane:#141b29; --pane-alt:#172030;
	--border:#303d56; --border-soft:#222c40;
	--text:#e4e8f0; --dim:#8e9ab2;
	--accent:#f2683f; --accent-fg:#ffffff; --accent-text:#f2683f;
	--sel:#3d2a20; --btn:#232d42; --btn-border:#3a4763;
	--warn-bg:#3a3116;
	--st-mod:#6cb0f0; --st-add:#38e07c; --st-unt:#45c9c0; --st-del:#ff6a5c;
	--st-ren:#b394ef; --st-sub:#dda45c;
	--diff-add-bg:#1b3324; --diff-add-fg:#52ec92;
	--diff-del-bg:#3f2023; --diff-del-fg:#ff8d80;
	--diff-hunk:#9f93e8; --diff-ctx:#cbd2e0;
	--shadow:rgba(0,0,0,.5);
`,
		'Verdigris': `
	--win-bg:#262019; --pane:#1d1813; --pane-alt:#221c15;
	--border:#453a2c; --border-soft:#2e261d;
	--text:#ece6da; --dim:#a89c88;
	--accent:#2fbfa4; --accent-fg:#03231c; --accent-text:#2fbfa4;
	--sel:#14453a; --btn:#322a20; --btn-border:#4a3e2e;
	--warn-bg:#46300f;
	--st-mod:#6cb0f0; --st-add:#38e07c; --st-unt:#d4b352; --st-del:#ff6a5c;
	--st-ren:#b394ef; --st-sub:#dda45c;
	--diff-add-bg:#14301d; --diff-add-fg:#52ec92;
	--diff-del-bg:#3a1c17; --diff-del-fg:#ff8d80;
	--diff-hunk:#9b8fe0; --diff-ctx:#d6cfc2;
	--shadow:rgba(0,0,0,.5);
`,
		'Firefly': `
	--win-bg:#1c2418; --pane:#161d13; --pane-alt:#192015;
	--border:#33402c; --border-soft:#232c1d;
	--text:#e6ebe1; --dim:#93a189;
	--accent:#c8e224; --accent-fg:#1c2000; --accent-text:#c8e224;
	--sel:#39420f; --btn:#242e1e; --btn-border:#44543a;
	--warn-bg:#44290f;
	--st-mod:#6cb0f0; --st-add:#38e07c; --st-unt:#45c9c0; --st-del:#ff6a5c;
	--st-ren:#b394ef; --st-sub:#dda45c;
	--diff-add-bg:#1e3322; --diff-add-fg:#52ec92;
	--diff-del-bg:#3e2320; --diff-del-fg:#ff8d80;
	--diff-hunk:#9b8fe0; --diff-ctx:#ccd3c6;
	--shadow:rgba(0,0,0,.5);
`,
		'Afterglow': `
	--win-bg:#261b28; --pane:#1e1420; --pane-alt:#221824;
	--border:#443048; --border-soft:#2e2031;
	--text:#ece4ee; --dim:#a794ab;
	--accent:#f2683f; --accent-fg:#ffffff; --accent-text:#f2683f;
	--sel:#472823; --btn:#332338; --btn-border:#4a3650;
	--warn-bg:#3e2f12;
	--st-mod:#6cb0f0; --st-add:#38e07c; --st-unt:#45c9c0; --st-del:#ff6a5c;
	--st-ren:#b394ef; --st-sub:#dda45c;
	--diff-add-bg:#243325; --diff-add-fg:#52ec92;
	--diff-del-bg:#43211f; --diff-del-fg:#ff8d80;
	--diff-hunk:#a795e8; --diff-ctx:#d5cdd8;
	--shadow:rgba(0,0,0,.5);
`,
	},
};

const themeClass = (polarity, name) => `t-${polarity}-${name.toLowerCase().replace(/ /g, '-')}`;

const themeCss = () => Object.entries(THEMES).flatMap(([polarity, themes]) =>
	Object.entries(themes).map(([name, vars]) => `.${themeClass(polarity, name)} { ${vars} }`)).join('\n');

const CSS = `
${themeCss()}

* { box-sizing: border-box; }
html, body { margin: 0; padding: 0; }
body {
	background: #ffffff;
	font: 14px/1.5 "Segoe UI", system-ui, -apple-system, sans-serif;
	padding-bottom: 60px;
}
.wrap { max-width: 1500px; margin: 0 auto; padding: 0 24px; }

/* ---------- theme pickers and polarity bands ---------- */
.masthead { background: #000000; padding: 14px 0; }
.pickers { display: flex; gap: 20px; font-size: 13px; color: #9aa1ad; }
.pickers select { font: inherit; padding: 1px 4px; }
.band { padding: 12px 0 30px; }
.band.dark { background: #000000; }

/* ---------- window shell ---------- */
.win {
	position: relative; /* the content-search popup centers on it */
	width: 1400px; max-width: 100%; height: 780px;
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

.menubar { display: flex; padding: 0 6px; background: var(--win-bg); border-bottom: 1px solid var(--border); font-size: 12.5px; flex: 0 0 auto; }
.menubar span { padding: 4px 10px; }

.repobar { display: flex; align-items: center; gap: 8px; padding: 8px 12px; background: var(--pane-alt); border-bottom: 1px solid var(--border); flex: 0 0 auto; white-space: nowrap; }
.repobar .repo { font-weight: 650; }
.repobar .branch { font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 12px; background: var(--pane); border: 1px solid var(--border); border-radius: 4px; padding: 1px 7px; }
.repobar .ab { color: var(--accent-text); font-size: 12px; font-weight: 600; }
.grow { flex: 1; }
.row { display: flex; flex: 1; min-height: 0; }

/* ---------- recent repositories dock ---------- */
.dock { flex: 0 0 210px; background: var(--pane); border-right: 1px solid var(--border); }
.dock .dhead { display: flex; align-items: center; gap: 6px; padding: 6px 10px; background: var(--pane-alt); border-bottom: 1px solid var(--border); font-size: 12px; color: var(--dim); }
.rrow { position: relative; padding: 4px 10px; border-bottom: 1px solid var(--border-soft); line-height: 1.35; }
.rrow.cur { background: var(--pane-alt); }
.rrow.cur::before { content: ""; position: absolute; left: 0; top: 0; bottom: 0; width: 2px; background: var(--accent); }
.rrow.hover { background: var(--sel); } /* the mouse or the keyboard, there being no selection */
.rrow.sub { padding-left: 30px; }
.rrow .p { font-size: 11px; color: var(--dim); word-break: break-all; }
.rrow .kind { float: right; font-size: 10px; color: var(--accent-text); border: 1px solid var(--border); border-radius: 3px; padding: 1px 6px; }
.rrow .tw { display: inline-block; width: 12px; margin-left: -14px; color: var(--dim); font-size: 10px; }
.rrow .ico { margin-left: -18px; margin-right: 4px; font-size: 12px; }
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
/* Two columns, not one cell: each is colored by an item data role, which one string could not be. */
.frow .plus, .frow .minus { flex: 0 0 auto; font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 12px; text-align: right; }
.frow .plus { min-width: 32px; color: var(--st-add); }
.frow .minus { min-width: 34px; color: var(--st-del); }
/* min-width:0, or the flex item refuses to shrink below its text and the ellipsis never takes effect */
.frow .path { font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 12px; min-width: 0; overflow: hidden; text-overflow: ellipsis; }
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

/* ---------- commit log ---------- */
.log { display: flex; flex-direction: column; min-height: 0; background: var(--pane); }
/* Inside the scroller, so the vertical scrollbar insets header and rows alike and the columns stay lined up */
.log .lhead { position: sticky; top: 0; z-index: 1; display: flex; gap: 10px; padding: 5px 10px; background: var(--win-bg); border-bottom: 1px solid var(--border); color: var(--dim); font-size: 12px; white-space: nowrap; }
.log .lhead span { font-family: inherit; color: inherit; font-weight: inherit; }
.log .rows { flex: 1; min-height: 0; overflow: auto; }
.lrow { display: flex; gap: 10px; padding: 4px 10px; border-bottom: 1px solid var(--border-soft); font-size: 12.5px; white-space: nowrap; }
.lrow.sel { background: var(--sel); }
/* Sha, author and date are dim; the subject is not, being the one the eye scans for. An unpushed
   commit takes the accent on its sha - the same mark the commit window's ahead count wears. */
.c-sha { flex: 0 0 72px; font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 12px; color: var(--dim); }
.c-sha.unpushed { color: var(--accent-text); font-weight: 600; }
.c-subj { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; }
.c-auth { flex: 0 0 96px; color: var(--dim); overflow: hidden; text-overflow: ellipsis; }
.c-date { flex: 0 0 232px; color: var(--dim); }

/* ---------- search field ---------- */
.field { background: var(--pane); border: 1px solid var(--btn-border); border-radius: 4px; padding: 4px 8px; color: var(--dim); font-size: 12.5px; }

/* ---------- content-search popup ---------- */
/* Centered on the window rather than dropped under its button, which sits too far out in the corner to
   read a caption from. */
.popup { position: absolute; left: 50%; top: 50%; transform: translate(-50%, -50%); z-index: 5; width: 350px;
	display: flex; flex-direction: column; gap: 6px; padding: 8px;
	background: var(--win-bg); border: 1px solid var(--border); border-radius: 6px; box-shadow: 0 8px 24px var(--shadow); }
.popup .cap { font-size: 12.5px; }
.popup .prow { display: flex; align-items: center; gap: 8px; font-size: 12.5px; }

/* ---------- push log ---------- */
/* Open only while a push runs. A ConsoleLogView, so the progress meter's carriage returns rewrite one
   line instead of filling the log. Each entry opens with the command and closes with its verdict,
   which the output itself nowhere states - red on a failure. */
.plog { flex: 0 0 auto; border-top: 1px solid var(--border); background: var(--pane); }
.plog .phead { display: flex; align-items: center; gap: 9px; padding: 6px 10px; background: var(--win-bg); border-bottom: 1px solid var(--border); font-size: 12px; }
.plog pre { margin: 0; max-height: 170px; overflow: auto; padding: 6px 10px; font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; font-size: 12px; line-height: 1.5; color: var(--dim); }
.plog .ok { color: var(--st-add); font-weight: 600; }

/* ---------- buttons ---------- */
.btn { font: inherit; font-size: 12.5px; padding: 5px 14px; background: var(--btn); color: var(--text); border: 1px solid var(--btn-border); border-radius: 4px; white-space: nowrap; }
.btn.primary { background: var(--accent); color: var(--accent-fg); border-color: var(--accent); font-weight: 600; }
.btn.big { padding: 8px 14px; width: 100%; font-size: 13px; }
.btn.small { padding: 3px 9px; }
`;

/* ============================ sample content ============================ */

/* One row per state the list can show, so the styling of each is visible at once. `add`/`del` absent is
   a row the diff gives no count for: untracked files are not in it, and a submodule pointer's one-line
   change is not a line count. A pure rename is genuinely 0 and 0. */
const FILES = [
	{ chk: 1,    st: 'Modified',  cls: 'mod', path: 'app/src/commitwindow.cpp', add: 42, del: 17 },
	{ chk: 1,    st: 'Modified',  cls: 'mod', path: 'app/src/main.cpp', add: 3, del: 1 },
	{ chk: 1,    st: 'Added',     cls: 'add', path: 'app/src/repository.h', add: 96, del: 0 },
	{ chk: 1,    st: 'Renamed',   cls: 'ren', path: 'app/src/gitprocess.cpp', add: 0, del: 0 },
	{ chk: 1,    st: 'Deleted',   cls: 'del', path: 'app/src/old_widget.cpp', add: 0, del: 128 },
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

const PUSH_LOG = [
	'Enumerating objects: 27, done.',
	'Counting objects: 100% (27/27), done.',
	'Compressing objects: 100% (14/14), done.',
	'Writing objects: 100% (15/15), 2.14 KiB | 2.14 MiB/s, done.',
	'Total 15 (delta 11), reused 0 (delta 0), pack-reused 0',
	'remote: Resolving deltas: 100% (11/11), completed with 11 local objects.',
	'To github.com:VioletGiraffe/GoodGit.git',
	'   c218660..a3f19e2  master -> master',
];

/* History window sample. `up` is a commit the upstream has not seen. Refs ride the subject, as git's
   %D hands them over. */
const COMMITS = [
	{ sha: '98087db9', up: 1, subj: '(HEAD -> master) +/- lines counts', date: '2026-08-14 15:49 (18 minutes ago)' },
	{ sha: 'c218660a', up: 1, subj: 'Refactor', date: '2026-08-14 14:03 (2 hours ago)' },
	{ sha: 'd7f0181b', up: 1, subj: 'Bugfixes', date: '2026-08-14 12:28 (3 hours ago)' },
	{ sha: '27044055', subj: 'Bugfixes', date: '2026-08-14 04:09 (11 hours ago)' },
	{ sha: '61e2f70c', subj: 'Bugfix', date: '2026-08-14 03:55 (12 hours ago)' },
	{ sha: 'dc4b7b53', subj: 'Bugfix', date: '2026-08-14 03:45 (12 hours ago)' },
	{ sha: '57fd149d', subj: 'merge/cherry-pick/revert/rebase commit that fails after its checked untracked files were staged leaves them staged', date: '2026-08-14 03:33 (12 hours ago)' },
	{ sha: '30cc56c5', subj: 'Bugfix', date: '2026-08-14 03:26 (12 hours ago)' },
	{ sha: 'b3dde3ed', subj: 'Bugfix: job completion callback must be async', date: '2026-08-14 03:18 (12 hours ago)' },
	{ sha: 'a97c41e0', subj: 'Discard changes to the selected files', date: '2026-08-13 22:04 (17 hours ago)' },
	{ sha: '4f0b2d18', subj: 'Peek at what the upstream has', date: '2026-08-13 20:41 (19 hours ago)' },
];

/* One commit's files. No checkboxes here - a commit already made has nothing to check. */
const COMMIT_FILES = [
	{ st: 'Modified', cls: 'mod', add: 29, del: 2, path: 'app/src/changedfilesmodel.cpp' },
	{ st: 'Modified', cls: 'mod', add: 9, del: 2, path: 'app/src/changedfilesmodel.h' },
	{ st: 'Modified', cls: 'mod', add: 2, del: 0, path: 'app/src/commitwindow.cpp' },
	{ st: 'Modified', cls: 'mod', add: 33, del: 0, path: 'app/src/gitparsers.cpp' },
	{ st: 'Modified', cls: 'mod', add: 11, del: 0, path: 'app/src/gitparsers.h' },
	{ st: 'Modified', cls: 'mod', add: 37, del: 2, path: 'app/src/historymodels.cpp' },
	{ st: 'Modified', cls: 'mod', add: 11, del: 1, path: 'app/src/historymodels.h' },
	{ st: 'Modified', cls: 'mod', add: 12, del: 4, path: 'app/src/historywindow.cpp' },
	{ st: 'Modified', cls: 'mod', add: 1, del: 0, path: 'app/src/historywindow.h' },
	{ st: 'Modified', cls: 'mod', add: 45, del: 6, path: 'app/src/repository.cpp' },
	{ st: 'Modified', cls: 'mod', add: 10, del: 2, path: 'app/src/repository.h' },
	{ st: 'Modified', cls: 'mod', add: 8, del: 7, path: 'doc/ARCHITECTURE.md' },
];

const COMMIT_DIFF = [
	['h', '@@ -52,6 +52,19 @@ QColor changeTypeColor(ChangeType type)'],
	['c', '\treturn {};'],
	['c', '}'],
	['c', ''],
	['a', '+QString lineCountText(const std::optional<LineCounts>& counts, bool added)'],
	['a', '+{'],
	['a', '+\tif (!counts)'],
	['a', '+\t\treturn {};'],
	['a', '+\treturn added ? QStringLiteral("+%1").arg(counts->added) : QStringLiteral("-%1").arg(counts->removed);'],
	['a', '+}'],
	['a', '+'],
	['a', '+QColor lineCountColor(bool added)'],
	['a', '+{'],
	['a', '+\tconst Theme& t = activeTheme();'],
	['a', '+\treturn added ? t.stAdded : t.stDeleted;'],
	['a', '+}'],
	['a', '+'],
	['c', 'ChangedFilesModel::ChangedFilesModel(QObject* parent) :'],
	['c', '\tQAbstractTableModel(parent)'],
	['c', '{'],
	['c', '}'],
];

const esc = s => String(s).replace(/&(?!\w+;)/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

/* ============================ markup ============================ */

const cb = state => state === null
	? '<span class="nocb">&mdash;</span>'
	: `<span class="cb ${state === 'tri' ? 'tri' : state ? 'on' : ''}"></span>`;

const frow = (f, selected) => {
	const cls = ['frow', selected ? 'sel' : '', f.cls === 'del' ? 'del' : '',
		f.blocked ? 'disabled blocked' : ''].filter(Boolean).join(' ');
	const counts = f.add === undefined
		? '<span class="plus"></span><span class="minus"></span>'
		: `<span class="plus">+${f.add}</span><span class="minus">-${f.del}</span>`;
	const check = f.chk === undefined ? '' : cb(f.chk); // a commit already made has nothing to check
	return `<div class="${cls}">${check}${f.ico ? '<span class="ico">&#128193;</span>' : ''}
				<span class="state s-${f.cls}">${f.st}</span>${counts}
				<span class="path">${esc(f.path)}</span></div>`;
};

const filelist = () => {
	const checked = FILES.filter(f => f.chk === 1).length;
	return `<div class="files col" style="flex:1">
			<div class="bar">${cb('tri')}<span>${checked} of ${FILES.length} checked</span>
				<button class="btn small">Modified only</button><span class="grow"></span></div>
			<div class="rows">
				${FILES.map((f, i) => frow(f, i === 0)).join('\n\t\t\t\t')}
			</div>
		</div>`;
};

const diffpane = (path, tag, lines) => `<div class="diff col" style="flex:1">
			<div class="dhead"><span>${esc(path)}</span><span class="grow"></span>
				<span class="tag">${tag}</span></div>
			<pre>${lines.map(([t, s]) => `<span class="l ${t === 'c' ? '' : t}">${esc(s) || '&nbsp;'}</span>`).join('')}</pre>
		</div>`;

const pushlog = () => `<div class="plog">
			<div class="phead"><span>Push output</span><span class="grow"></span>
				<button class="btn small">Hide</button></div>
			<pre>&gt; git push
${PUSH_LOG.map(l => esc(l)).join('\n')}
<span class="ok">Succeeded</span></pre>
		</div>`;

/* The dock takes its width from the diff pane beside it, never from the commit column: the repo header
   row sets that column's floor and nothing here may push it. Rows carry the path over two lines, since
   210px elides most of one. */
const RECENT = [
	{ name: 'GoodGit', path: 'E:\\Development\\Projects\\Personal\\GoodGit', kind: 'git', cur: 1, open: 1,
		subs: [{ name: 'cpputils' }, { name: 'cpp-template-utils' }, { name: 'qtutils' },
			{ name: 'zlib', path: '3rdparty\\zlib' }] },
	{ name: 'FileCommander', path: 'E:\\Development\\Projects\\FileCommander', kind: 'git', subs: [{ name: 'thin_io' }] },
	{ name: 'text-utils', path: 'E:\\Development\\Projects\\Personal\\text-processing-utils', kind: 'git', hover: 1 },
	{ name: 'design-notes', path: 'D:\\Work\\design-notes', kind: 'hg' },
];

const rrow = r => {
	const twisty = r.subs ? `<span class="tw">${r.open ? '&#9662;' : '&#9656;'}</span>` : '';
	const cls = ['rrow', r.cur ? 'cur' : '', r.hover ? 'hover' : ''].filter(Boolean).join(' ');
	return `<div class="${cls}"><span class="kind">${r.kind}</span>${twisty}${esc(r.name)}<div class="p">${esc(r.path)}</div></div>`;
};

// A subrepo's own path only where it says more than the name does
const subrow = s => `<div class="rrow sub"><span class="ico">&#128193;</span>${esc(s.name)}`
	+ (s.path ? `<div class="p">${esc(s.path)}</div>` : '') + '</div>';

const recentDock = () => `<div class="dock col">
			<div class="dhead"><span>Recent</span><span class="grow"></span>
				<button class="btn small">Open...</button><button class="btn small">Hide</button></div>
			${RECENT.map(r => [rrow(r), ...(r.open ? r.subs.map(subrow) : [])].join('\n\t\t\t')).join('\n\t\t\t')}
		</div>`;

/* Left column width is set by the repo header row, the widest thing in the column: repo name, branch
   chip, the upstream phrase and four buttons, none of which shrink. The 50-column subject guide needs
   only 430px of it. */
const window_ = () => `<div class="win">
	<div class="titlebar"><span class="tt">GoodGit [master] - GoodGit</span>
		<span class="wbtns"><i>&#8210;</i><i>&#9633;</i><i>&#10005;</i></span></div>
	<div class="menubar"><span>File</span><span>Edit</span><span>View</span><span>Help</span></div>
	<div class="row">
		${recentDock()}
		<div class="col" style="flex:0 0 600px;border-right:1px solid var(--border)">
			<div class="repobar">
				<span class="repo">GoodGit</span><span class="branch">master</span>
				<span class="ab">3 to push to origin/master</span>
				<span class="grow"></span>
				<button class="btn small">Push</button><button class="btn small">Peek</button>
				<button class="btn small">Refresh</button><button class="btn small">History</button>
			</div>
			${filelist()}
			<div style="border-top:1px solid var(--border)">
				<div class="msg">
					<div class="lbl"><span>Commit message</span></div>
					<div class="box"><span class="ruler"></span>
						<div>${esc(MSG_SUBJECT)}<span class="caret"></span></div></div>
				</div>
				<div style="padding:0 12px 12px;display:flex;flex-direction:column;gap:6px">
					<button class="btn primary big">Commit 6 file(s)</button>
					<button class="btn big">Commit &amp; Push</button>
				</div>
			</div>
		</div>
		<div class="col" style="flex:1">${diffpane('app/src/commitwindow.cpp', 'HEAD &rarr; working tree', DIFF)}${pushlog()}</div>
	</div>
</div>`;

const AUTHOR = 'Violet Giraffe';

const logrow = (c, selected) => `<div class="lrow${selected ? ' sel' : ''}">
					<span class="c-sha${c.up ? ' unpushed' : ''}">${c.sha}</span>
					<span class="c-subj">${esc(c.subj)}</span>
					<span class="c-auth">${AUTHOR}</span>
					<span class="c-date">${c.date}</span></div>`;

/* Open over the log, its edit holding whatever term is in force - empty here, so the placeholder shows. */
const pickaxePopup = () => `<div class="popup">
		<div class="cap">List the commits whose diff touches this text:</div>
		<div class="field">Text to find, taken literally</div>
		<div class="prow"><span class="cb on"></span><span>Ignore case</span><span class="grow"></span>
			<button class="btn small">Clear</button><button class="btn small">Find</button></div>
	</div>`;

/* Load more shows only once the log has hit its commit cap (20 000 by default), which is also what puts
   "more to load" in the count. */
const historyWindow_ = () => `<div class="win">
	<div class="titlebar"><span class="tt">History - GoodGit - GoodGit</span>
		<span class="wbtns"><i>&#8210;</i><i>&#9633;</i><i>&#10005;</i></span></div>
	<div class="repobar">
		<span>20000 commits, more to load</span>
		<span class="grow"></span>
		<span class="field" style="flex:0 0 220px">Search commits</span>
		<button class="btn small">Find in contents...</button>
		<button class="btn small">Load more</button>
		<button class="btn small">Refresh</button>
	</div>
	<div class="log" style="flex:0 0 307px">
		<div class="rows">
			<div class="lhead"><span class="c-sha">Commit</span><span class="c-subj">Subject</span>
				<span class="c-auth">Author</span><span class="c-date">Date</span></div>
				${COMMITS.map((c, i) => logrow(c, i === 0)).join('\n\t\t\t\t')}
		</div>
	</div>
	<div class="row" style="border-top:1px solid var(--border)">
		<div class="col" style="flex:0 0 320px;border-right:1px solid var(--border)">
			<div class="files col" style="flex:1">
				<div class="bar"><span>${COMMIT_FILES.length} files</span><span class="grow"></span></div>
				<div class="rows">
					${COMMIT_FILES.map((f, i) => frow(f, i === 0)).join('\n\t\t\t\t\t')}
				</div>
			</div>
		</div>
		<div class="col" style="flex:1">${diffpane('app/src/changedfilesmodel.cpp', '98087db9', COMMIT_DIFF)}</div>
	</div>
	${pickaxePopup()}
</div>`;

/* Each window is rendered once per polarity, wrapped in a full-bleed band that carries the theme's
   variables; the dark band paints the page black around its window. */
const band = (polarity, content) => `<div class="band ${polarity} ${themeClass(polarity, Object.keys(THEMES[polarity])[0])}">
	<div class="wrap">${content}</div>
</div>`;

const options = polarity => Object.keys(THEMES[polarity])
	.map(name => `<option value="${themeClass(polarity, name)}">${name}</option>`).join('');

const html = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GoodGit &mdash; window layouts</title>
<!-- Generated by mockup.gen.js. Edit that file, not this one. -->
<style>${CSS}</style>
</head>
<body>
<div class="masthead">
	<div class="wrap"><div class="pickers">
		<label>Dark theme: <select id="darkSel">${options('dark')}</select></label>
		<label>Light theme: <select id="lightSel">${options('light')}</select></label>
	</div></div>
</div>
${band('dark', window_())}
${band('light', window_())}
${band('dark', historyWindow_())}
${band('light', historyWindow_())}
<script>
/* Retheming only; with scripting off the page stays on the Default themes. */
for (const polarity of ['light', 'dark']) {
	document.getElementById(polarity + 'Sel').addEventListener('change', e => {
		for (const band of document.querySelectorAll('.band.' + polarity))
			band.className = 'band ' + polarity + ' ' + e.target.value;
	});
}
</script>
</body>
</html>`;

const dest = path.join(__dirname, 'mockup.html');
fs.writeFileSync(dest, html, 'utf8');
console.log('wrote', dest, (html.length / 1024).toFixed(1) + ' KB');
