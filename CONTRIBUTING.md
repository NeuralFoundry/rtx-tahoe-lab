# Contributing

The original effort is paused. Source improvements and independent reproductions
are welcome. Keep CPU models, compiled artifacts and actual GPU results separate.

Start with `python tools/test_cpu.py`. Include focused regression checks for
changes to protocol parsing, memory layout, ownership and command construction.
Update the public file inventory with `python tools/verify_snapshot.py --write`
when changing source files; verify it again before committing.

Hardware reports should name source/OS/SDK versions and the tested GPU model,
describe exactly what ran, and report cleanup and failure behavior. Remove
credentials, personal machine identifiers, unrelated logs and proprietary
firmware. Do not claim full Metal acceleration from object construction or a
small successful draw/compute workload. Use an authorized, recoverable test
system. CI must remain CPU-only.
