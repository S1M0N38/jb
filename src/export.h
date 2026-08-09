/* export.h — jb export ID [PATH] (plan phase 8): the pi viewer.
   .jsonl PATH → byte-parity copy of session.jsonl (pi exportToJsonl
   shape); any other PATH (or none) → self-contained HTML viewer built
   from the embedded vendored pi-export template (src/assets.inc). */
#ifndef JB_EXPORT_H
#define JB_EXPORT_H

/* jb export ID [PATH] — resolve the repo + session, write the export.
   0 success · 1 not found/ambiguous/JB_SESSION not set/write failure
   · 2 usage */
int cmd_export(int argc, char **argv);

#endif
