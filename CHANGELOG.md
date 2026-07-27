# Changelog

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
