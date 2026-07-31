# Changelog

## 0.9.4 — 2026-07-31

### Free-DC Host ID on Linux

GTK's generic list control never calls `OnGetItemColumnAttr` - measured, not
assumed: a virtual list with the override in place records five calls to
`OnGetItemAttr` and zero to the per-column one. So the cell cannot be drawn
blue and underlined on Linux the way it is on Windows. That is a limitation of
the control, not something this code can fix.

A hand cursor over the cell is the affordance that does work on both platforms,
so that is what indicates the link on Linux.

Click detection was rewritten to stop doing coordinate arithmetic by hand. Two
coordinate spaces are involved and they differ: on GTK the event arrives on the
list's internal child window, whose origin sits below the header, while
GetSubItemRect reports rectangles relative to the control with the header
included. Rather than reconciling those with scroll offsets and column widths -
which went wrong twice - each column's rectangle is now asked whether it
contains the point, in both spaces.

## 0.9.3 — 2026-07-31

### The Free-DC Host ID column is clickable on Linux

It worked on Windows and did nothing on Linux. wxMSW uses the native list
control, which receives mouse events itself; GTK uses the generic one, which
routes them to an internal child window. Binding the handler to the control was
therefore enough on Windows and silently never fired on Linux.

The handler is now bound to the child windows as well - the same thing the
Computers view already had to do, with a comment in the file saying so.

Column detection now uses HitTest's sub-item, which accounts for horizontal
scrolling. Summing column widths from zero picks the wrong column once the list
is scrolled right, which is the normal state for reaching the last of sixteen
columns. The width walk remains as a fallback for backends that do not report a
sub-item.

## 0.9.2 — 2026-07-31

### Free-DC Host ID is now a column

0.9.1 put both Free-DC links on the Projects right-click menu, where nobody
found them — the reasonable conclusion on seeing an unchanged Projects tab was
that the build was wrong, not that the feature was hidden.

The per-project link is now the last column on the Projects tab, showing this
host's ID on that project and opening its Free-DC page when clicked. It is
drawn as a link only where there is somewhere to go: a project Free-DC does not
carry leaves the cell blank rather than offering a link that would 404. The
column sorts as a number, so mapped projects group together.

**Free-DC Host CPID** stays on the right-click menu. It is per computer rather
than per project, and is wanted far less often.

### Opening a link now reports failure

Every one of the six places that opened a web page ignored whether it had
worked. On a machine with no browser installed — or with no `xdg-open` to find
one, which is an ordinary state for a BOINC host — Help → Update and the
Free-DC links did nothing at all, with nothing on screen to say why.

They now go through one helper that, when no browser can be opened, says so and
copies the address to the clipboard so it is still usable.

## 0.9.1 — 2026-07-31

Windows support, plus two **Find computers** fixes that matter just as much on
Linux.

### Windows

The port now builds for Windows from the same source as Linux. The result is a
single static `BoincTasksPP.exe` — no MSVC redistributable, no wxWidgets or
MinGW DLLs to ship beside it — and an NSIS installer that puts it in Program
Files, adds Start menu entries, and registers with Add/Remove Programs.
Settings and history live in `%APPDATA%\BoincTasksPP\` and are left alone when
you uninstall, so reinstalling does not cost you your computer list.

Built for Windows 10 and 11 (x64). The API level is pinned at Windows 7, so
older versions may work but have not been tried.

### Find computers

**Hosts on slow links were skipped.** The scan gave an address 700 ms to answer
its first port and then abandoned the whole address — every port on it. A host
behind a wireless bridge or a congested link exceeds that easily. It now sweeps
quickly as before, then makes a second pass over only the addresses that stayed
silent, with a much longer timeout. Both values are on **Settings → General →
Find computers**; set the retry to 0 to skip the second pass.

**The window locked up during a scan.** The progress dialog's range was sized
for a single pass, so the two-pass scan ran past the end of it. `wxPD_AUTO_HIDE`
then hid the dialog while `wxPD_APP_MODAL` kept the main window disabled — it
looked frozen with nothing on screen to explain why. The range is now sized for
both passes, and a scan can be cancelled while it runs.

Neither fix is Windows-specific; both were affecting Linux the same way.

### Windows-only fixes

- A non-blocking connect is reported through `WSAGetLastError`, not `errno`, so
  the probe never recognised a connection in progress and every address looked
  like a timeout: the scan finished normally and found nothing.
- A failed connect is signalled through `select()`'s exception set on Windows
  rather than the write set, which would have made large port sweeps crawl.

### Packaging

- The version now comes from one place (`BT_VERSION`) instead of being
  hardcoded in the resource script, the installer script and the build scripts,
  where it had already drifted.
- The release build refuses to report success when an artifact is missing. It
  previously fell back to a portable tarball and exited 0, which is how a build
  can ship one package short without anyone noticing.

## 0.9.0 — 2026-07-27

First packaged release of the Linux port. Feature complete against the Windows
application apart from the Windows-only pieces listed in `AUTHORS.md` and on the
website.

### Naming

The project was called **boinctasks-linux** during development and became
**BoincTasks++** before the first commit, to keep it clearly distinct from
eFMer's BoincTasks unless he chooses to adopt it.

Two forms are used deliberately:

| Where | Name |
|---|---|
| Anything a person reads — window title, About, menu entry, website | `BoincTasks++` |
| Anything a machine reads — package, binary, config, download files | `boinctasks-pp` |

`+` is awkward in URLs, shell commands and package names, so it appears only in
human-facing text.

The earlier `boinctasks-linux` builds were overwritten rather than kept; the
rename predates the first commit, so no released artefact ever carried the old
name.

### Migration

Settings and history move themselves on first run:

    ~/.config/boinctasks-linux.conf   ->  ~/.config/boinctasks-pp.conf
    ~/.config/boinctasks-history.db   ->  ~/.config/boinctasks-pp-history.db

The originals are copied, not moved, so an older build still works if you go
back to one.

### Build

Release builds come from an Ubuntu 22.04 container with wxWidgets 3.2 linked
statically (`port/packaging/Containerfile.build22`), which sets the runtime
floor at glibc 2.34 — Ubuntu 22.04, Debian 12, RHEL 9 and newer.
