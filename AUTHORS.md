# Authors and credit

## BoincTasks (the original)

**eFMer (Fred)** — https://efmer.com/boinctasks/

The Windows application this is built from: the design, the views, the column
sets, the rules engine, the menu structure and the behaviour that BOINC
crunchers have relied on for years. Released under the GPLv3.

Where this port had to decide how something should work, the answer was taken
from eFMer's source and its English language file rather than guessed at, so
the two behave the same way.

## BoincTasks++ (this port)

**Skillz** — https://free-dc.org/

Native Linux port, written in wxWidgets against BOINC's own RPC library.
Not a wrapper, not an emulation layer, not a reimplementation in another
language — the same application rebuilt so it runs as a normal Linux desktop
program, keeping the layout and behaviour of the original.

Work beyond a straight port:

- Data from each client is merged as it arrives rather than after every host
  has answered, so one unreachable machine cannot stall the display
- Each client is polled on its own thread; the GUI never blocks on RPC
- Completed-task history is kept locally, with a long-term archive, since the
  client discards a task once it has been reported
- Views are rebuilt only while visible, which is most of the difference
  between ~20% and ~5% of a CPU core on a 41-client farm

## BOINC

**University of California, Berkeley** — https://boinc.berkeley.edu/

The client, and the GUI RPC interface everything here talks to. The RPC client
library is vendored from the BOINC source, with the small portability fixes
noted in the commit history.

## Licence

GPLv3, the same as the original. Whoever ends up maintaining this, that licence
and the attribution above travel with the code.
