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

**The Use column never mentions the GPU.** Both platforms.

Tasks tab, Use column, shows only the CPU share — "1.00 CPU", or "1C"
condensed — even for a task running on a GPU. Classic shows the GPU there too.

The cell is built in `bt_taskmodel.cpp`, `COL_USE`, and only ever reads
`useCpus`:

    v = r->useCpus <= 0 ? "" : "%.2f CPU"

Everything needed is already to hand:

- `BtTaskRow::isGpu` exists and is set by the poller from the app version's
  `ncudas`/`natis`, with a fallback that sniffs the plan class for
  cuda/nvidia/ati/amd/intel
- `APP_VERSION` carries `ncudas` and `natis` as doubles, so the *count* is
  available, not just the fact of a GPU

So this is a display change, not new polling.

Classic writes it as **`1NV`** for a task using one NVIDIA GPU. Presumably an
AMD equivalent for ATI/AMD cards, unconfirmed - nobody here has one to look at,
so that wants checking against Classic or a user with AMD hardware before
guessing at the string.

Note the sort comparator keys on `useCpus` (`bt_taskmodel.cpp` ~line 321) and
will need to keep making sense once the cell can describe two devices.

**Collapsed rows show nothing in the Use column.** Wanted: the total.

A group of 32 single-threaded WCG tasks should read `32C`. Four PrimeGrid tasks
at four threads each should read `16C` - the sum of what the group occupies,
not a per-task figure.

The group row currently returns an empty string for `COL_USE`
(`bt_taskmodel.cpp` ~line 230). A group *does* already carry a `useCpus`, but
it is an **average**, not a total:

    use += r->useCpus;        // summed ...
    g->useCpus = use / n;     // ... then divided by n

It is averaged because it is computed alongside progress, time left and
deadline, which genuinely should average. So this needs a separate summed
field rather than a change to that one, or the sort order and any other reader
of `g.useCpus` changes meaning underneath.

Open question for when we build it: what a group holding GPU tasks should
read. `32C` is unambiguous for CPU work; a group of four NVIDIA tasks might
want `4NV`, and a mixed group something like `4C + 4NV`. Worth settling before
writing it rather than after.

**The Computers sidebar does not show which hosts are down.** Both platforms,
confirmed on 0.9.4.

Classic puts a small red marker beside every disconnected host and leaves
connected ones plain, so a farm with half its machines switched off reads at a
glance. Here every host looks identical whether it is answering or not.

This is worse than a missing decoration: selecting a dead host shows empty
task, project and transfer lists, which is indistinguishable from a host that
is up with nothing to do. The user is left to guess which.

The state is already there and already used elsewhere - `snap->connected`,
which drives the status bar count and the Log tab's "lost connection" entries.
The sidebar simply ignores it: items are added as

    m_tree->AppendItem(parent, c.name, -1, -1, new BtTreeData(...))

where the two -1s are image indices, and no image list is attached to the tree
(`bt_main.cpp` ~line 834).

Implementation notes for whoever picks it up:

- **Do not rebuild the tree to refresh this.** `BuildTree` recreates every item
  and calls `ExpandAll` and `SelectItem`, so driving it from the poll loop
  would fight the user's selection and expansion state twice a second. Keep a
  name -> `wxTreeItemId` map and call `SetItemImage` on change.
- There is already a transition hook: `bt_main.cpp` ~line 961 compares the
  previous `connected` against the new one to write the Log entry. That is the
  natural place to update the icon, since it fires exactly on change.
- **Decided: an icon, not a text colour.** A recognisable disconnect symbol -
  a broken link or a plug with a slash - reads at a glance and matches what
  somebody arriving from Classic already knows. Colour alone is also a poor
  carrier of state: red against black is exactly the distinction some
  colour-blind users cannot make.
- The icon has to be **compiled in**, not loaded from disk. Everything ships as
  a single file - AppImage, static .exe - so there is nowhere to put a loose
  asset. An XPM included as a header is the usual wxWidgets way and needs no
  build-system work.
- Provide it at 16px **and** 32px. The development host runs 3840x2160 and the
  application is per-monitor DPI aware on Windows, so a lone 16px icon will
  look blocky exactly where it is most visible.
- Only the disconnected state needs an image. Connected hosts stay plain, as in
  Classic, so there is no second asset and no "everything is decorated" noise.

**Add project opens far too small.** Windows only - Linux opens correctly.

The tree and description panes come up about twenty pixels tall with a
scrollbar, so the project list looks broken. It is not - dragging the dialog
larger reveals everything, since it already has a resize border. But nothing
signals that, and the first impression is a broken control rather than a small
window. Most people will not think to resize a dialog.

That it affects only one platform fits the cause: the requested size is the
same on both, but MSW's row heights and border metrics push the total minimum
past 640 where GTK's stay just under it. Same code, one platform over the line.
Linux is therefore the reference for what it should look like, and any fix has
to be checked on Windows because that is the only place the symptom appears.

`bt_addproject.cpp` fixes the dialog at `wxSize(760, 640)` and then calls
`SetSizer(top)` rather than `SetSizerAndFit`. The children's minimum heights -
110 for the computer list, 220 each for the tree and description, four text
rows, three lines of help, the button row - add up to more than the client area,
so the sizer squeezes the one proportional item, which is exactly the tree.

Use `SetSizerAndFit`, and set a minimum size so the panes cannot be dragged
away to nothing either. Worth checking on both platforms: the same arithmetic
will land differently under GTK and MSW because the row heights differ.

**Add project: every computer is pre-ticked.**

`bt_addproject.cpp` ticks all of them:

    for (unsigned i = 0; i < m_computers->GetCount(); i++)
        m_computers->Check(i, true);

Classic instead ticks whichever host is selected in the Computers sidebar and
leaves the rest clear, so attaching to one machine is the default and attaching
to forty is deliberate. Pre-ticking everything makes the destructive case the
easy one.

The caller already knows the selection - `TreeComputer()` in `bt_main.cpp`,
used elsewhere for view filtering - it simply is not passed to the dialog
(`bt_main.cpp` ~line 2017). Pass it in and tick only that entry.

Decide what to do when the sidebar is on "All computers" or a group rather than
a single host: ticking nothing is probably right, since the alternative is
guessing.

**Hand cursor never appears on Linux.**
`ProjectsView::OnMotion` calls `ev.Skip()` before `SetCursor`, so the generic
list control's own motion handling probably resets the cursor straight after.
Try setting the cursor without skipping, or handling `wxEVT_SET_CURSOR`
instead. Windows is unaffected.

## Features

**Import computers from Classic's `computers.xml`.**

Somebody arriving from Classic with a real farm has to retype every host, port
and password by hand. A sample with 157 entries makes the point.

The format is fully derivable from the vendored upstream source
(`BoincTasks/BoincTasks.cpp`, the reader around line 1601), and was checked
against a real file:

    <computers>
      <computer>
        <id_name>   name shown in the tree
        <id_group>  group, may be empty
        <ip>        host or address
        <mac>       Wake-on-LAN, no equivalent here
        <checked>   0 or 1
        <port>      port, or -1 meaning "use the default"
        <password>  see below
        <encryption>yes or no
      </computer>
      ...

Mapping onto `BtComputer` is direct: `id_name` -> `name`, `id_group` -> `group`,
`ip` -> `host`, `checked` -> `enabled`, `port` -> `port` with **-1 meaning
31416**. Classic also folds anything resembling `127.0.0.1` to a localhost
name, and blanks the MAC when it does; worth matching so an imported localhost
entry behaves like a hand-made one.

**Passwords depend on `<encryption>`, and the code is misleading here.** The
reader runs `TranslateFromXml` over the password unconditionally, but only
assigns the result when encryption is on - so with `encryption=no` the
plaintext is used as-is, and the translation is discarded. The sample file is
157 entries of `encryption=no`, all plaintext or empty, so it imports directly.

When `encryption=yes` the password **cannot be carried across**: `Decrypt` uses
the Windows CryptoAPI with a key held locally, so it is only decryptable on the
machine that wrote it. Import those hosts with a blank password and tell the
user plainly which ones need re-entering, rather than importing something that
silently fails to connect.

Other decisions:

- **In the application, not a separate tool.** File -> Import, so there is no
  second binary to build, ship and explain.
- **Merge, never replace.** Importing must not discard hosts already
  configured. Names were unique in the sample, but that cannot be assumed -
  decide whether a clash updates the existing entry or is skipped, and say
  which in the summary afterwards.
- **Preserve the checked state.** 96 of the 157 were ticked and 61 were not;
  turning the unticked ones on would start polling 61 machines the user had
  deliberately parked.
- Report what happened: how many added, skipped, and how many need a password.
- `mac` has no equivalent - there is no Wake-on-LAN here. Dropping it is fine,
  but note it, since importing then losing the ability to wake a host is a
  surprise worth documenting rather than hiding.

**Time Left should estimate wall-clock, not sum or average.**

Classic sums time left across a collapsed group, which is useless: 32 tasks of
one hour each reads as 32 hours when the host finishes them all in one. This
port averages instead, which is right while every task is running but wrong the
moment they cannot all run at once. On a 32-core host, 1333 ready-to-start
tasks of about an hour each currently reads 1h08m; the honest answer is closer
to 41 hours.

What is wanted is how long the host needs to get through the work:

    slots      = floor(p_ncpus * max_ncpus_pct / 100)
    concurrent = slots / avg_ncpus            (per task, so a 4-thread task
                                               occupies four slots)
    wall_clock = max( longest single remaining,
                      total remaining / concurrent )

That one expression covers both cases. Thirty-two one-hour tasks on 32 slots:
total 32h over 32 concurrent is 1h, longest single is 1h, so 1h - which is what
the average happens to give today, and why running groups look right. The same
expression on 1333 tasks gives 1333h / 32 = ~41h rather than 1h08m.

Data needed, all of it already arriving and none of it currently kept:

- `HOST_INFO::p_ncpus` - in `CC_STATE::host_info`, which the poller already
  fetches and reads for `host_cpid`
- `GLOBAL_PREFS::max_ncpus_pct` - the prefs editor already reads these
- `GLOBAL_PREFS::cpu_usage_limit` - throttling. A host limited to 50% takes
  twice as long, so this multiplies the estimate; easy to forget
- `avg_ncpus` per task - already stored as `BtTaskRow::useCpus`
- `HOST_INFO::_coprocs` - GPU counts, for the GPU pool

The Projects tab needs the same treatment. It currently *sums*
(`bt_poller.cpp`, `row.timeLeft += t.timeLeft`), so it has Classic's problem.

**GPU tasks still consume CPU slots.** They are scheduled against the GPU, but
BOINC reserves CPU for them according to the app version's `avg_ncpus`, and
that reservation comes out of the same pool. A 32-thread host running one GPU
task that holds four threads has 28 left for CPU work, not 32, even at 100%
CPU. So the CPU pool has to be reduced by what the running GPU tasks hold
before the CPU estimate is computed:

    cpu_slots_free = slots - sum(avg_ncpus of running GPU tasks)

Get that wrong and every CPU estimate on a host with GPUs reads optimistic.

A collapsed group belongs to one computer, so there is no combining across
hosts to do - the estimate is per host throughout.

Things to settle before writing it:

- **Which tasks count.** Suspended tasks and projects set to no-new-work still
  represent work the host must eventually do, but not at the current rate.
- Tasks that have not started report an estimate rather than a measurement, so
  the figure is only ever as good as BOINC's own runtime estimates - worth not
  presenting it with false precision.

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

**Link BOINC's library directly instead of the vendored copy.**
*Headline item for the next version.* Suggested by Wedge009
([issue #1](https://github.com/lblanchardiii/boinctasks-pp/issues/1)), who
independently ported BoincTasks and used BOINC as a submodule.

`BoincLibrary/` is a snapshot of BOINC's `lib/` carried inside eFMer's tree, not
a submodule. Its `version.h` reads 6.13.0 against upstream's 8.3.0, and
`hostinfo.h` is half the size of upstream's. Missing: `COPROC_INTEL`,
`COPROC_APPLE`/`apple_gpu`, `docker_version`, `p_vm_extensions_disabled`,
`num_opencl_cpu_platforms`. How stale it is shows in `str_util.cpp`, where the
`HAVE_STRDUP` branch read `needle=strdup(s1)` — wrong variable and no semicolon,
so it cannot compile and never has on Windows. We patched it; upstream had
already deleted the whole approach and rewritten the function with `strlcpy`.

Not a drop-in. Three things have to be handled:

- **`str_util.cpp`** (+1/−1) — our patch disappears; upstream rewrote it.
- **`gui_rpc_client.cpp`** (5 hunks) — must be carried across or upstreamed. A
  30-second `SO_RCVTIMEO` and the `errno` handling around it, so an unreachable
  host cannot hang a poller thread indefinitely.
- **`gui_rpc_client.h`** (16 lines) — same.

The actual work is that **`APP_VERSION` changed shape**: upstream replaced
`ncudas` with a generic `gpu_usage`. `bt_poller.cpp:213-216` reads
`ncudas`/`natis` to set `isGpu`, `useGpus` and `gpuKind`, which feeds the Use
column and the GPU half of the Time Left estimate. That has to be rewritten.

The payoff beyond currency: it settles the `ATI` string, which is still an
unverified guess, and makes Intel and Apple GPUs identifiable at all — neither
is representable in the vendored structs.


**Update checker.**
`latest.json` is already published in the right shape (version, released, and
url/sha256/size per platform). The client side is not written.

Compare versions **component-wise as integers**. A string compare puts
`"0.9.10"` below `"0.9.9"`, and a float conversion of `0.9.10` either throws or
truncates to `0.9` — this is a real bug that has bitten this author on another
project. Unit-test `0.9.9` vs `0.9.10` specifically. Nothing in the code parses
a version today, so the checker is the first place it can go wrong.


**Toolbars.**
Not ported. Every command is on a menu instead.
