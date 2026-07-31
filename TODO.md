# TODO

Batched so that a build carries several changes rather than one. Four version
bumps in one evening happened because a feature shipped three times without
being testable on the platform it was broken on; the answer is fewer, fuller
builds, not faster ones.

## Versioning

Test builds append a single lowercase letter; releases never carry one.

    0.9.4  ->  0.9.4a  ->  0.9.4b  ->  ...  ->  0.9.5

Set it in one place, `BT_VERSION` in `port/CMakeLists.txt`, and pass the same
value as `VERSION=` to the build scripts.

Rules the build enforces rather than trusts:

- **One letter only, a-z.** `0.9.4aa` sorts *below* `0.9.4z` in dpkg's
  comparison, so a double-letter suffix would silently break upgrade ordering.
  Past z, bump the number. CMake rejects anything else at configure time.
- **Windows gets the letter as a build number.** Version resources and NSIS
  accept four integers and nothing else, so `0.9.4b` becomes `0,9,4,2` -
  the fourth field, previously a wasted constant zero. Releases keep 0, which
  keeps Windows' own ordering monotonic.
- **Nothing in the application parses a version**, so a letter cannot confuse
  it. The first place that could is the update checker, which is not written
  yet - see below.

## Confirmed working

Both platforms, as of 0.9.4:

- Free-DC **Host ID** column on Projects — opens the right page on Windows and
  Linux, verified against a live farm
- Free-DC **Host CPID** on the Projects right-click menu
- Windows: link drawn blue and underlined, hand cursor, no colour bleed when
  the column is dragged between others

## Bugs

**Hand cursor never appears on Linux.**
`ProjectsView::OnMotion` calls `ev.Skip()` before `SetCursor`, so the generic
list control's own motion handling probably resets the cursor straight after.
Try setting the cursor without skipping, or handling `wxEVT_SET_CURSOR`
instead. Windows is unaffected.

## Known limitations

**No link styling on Linux.**
GTK's generic `wxListCtrl` never calls `OnGetItemColumnAttr` — measured, not
assumed: a virtual list with both overrides records five calls to
`OnGetItemAttr` and zero to the per-column one. Per-cell colour is therefore
impossible with this control. Fixing it properly means moving `ProjectsView` to
`wxDataViewCtrl`, the way `TasksView` already is. That is a real refactor of a
core view and should not be done in a hurry.

**Not code signed.**
SmartScreen warns on first run of the Windows installer. Signing costs real
money; the decision is open.

## Needs a tester

- **Windows installer, full cycle** — install, confirm settings land in
  `%APPDATA%\BoincTasksPP\` and not an administrator profile, uninstall
  declining settings removal, reinstall, confirm the computer list survived
- **Checkpoint and efficiency warnings** — present but unused by this author,
  so unverified in practice

## Queued

**Update checker.**
`latest.json` is already published in the right shape (version, released, and
url/sha256/size per platform). The client side is not written.

Compare versions **component-wise as integers**. A string compare puts
`"0.9.10"` below `"0.9.9"`, and a float conversion of `0.9.10` either throws or
truncates to `0.9` — this is a real bug that has bitten this author on another
project. Unit-test `0.9.9` vs `0.9.10` specifically. Nothing in the code parses
a version today, so the checker is the first place it can go wrong.

**Publish.**
The site still carries 0.9.1, Linux only; the Windows installer was pulled
pending testing. Once a build is signed off: regenerate `download.html` and
`latest.json` *from the actual files*, upload, and remember nginx caches
aggressively — verify with a cache-buster, and check each file's checksum
independently rather than concatenating them, which once masked a stale page.

**GitHub.**
Nothing pushed yet: the Windows commits plus everything since. Then tag and cut
a release with all three artifacts.

**Toolbars.**
Not ported. Every command is on a menu instead.

## Housekeeping, not the application

- Rotate the credentials shared during development: `skillz` SSH, `freedc`
  cPanel, `donations_rw`, and the `btpp` FTP account
- The development host has a stale XFCE session on `:0` alongside the xrdp one
  on `:10` — two full desktops, and the source of a lot of confusion about
  which screen a browser opened on
- `dbus-x11` is not installed there, which makes GTK applications complain
  about the accessibility bus
