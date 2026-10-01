# tcxNodeInspector tests

Headless console test (no window, no GPU). Hand edits are recorded through
`NodeInspector::recordTouchedForTests()`, the entry point the Inspector panel
uses, and the node tree is updated through a headless `Window`'s `tickTree()`,
which runs `updateTree()` and so sweeps destroyed children. The checks read
`NodeInspector::getTouched()`, the JSON `tcx_imgui_get_touched` reports under
`inspector`.

Touched record (#326):

- a node destroyed while the test still holds a `shared_ptr` to it reports
  `destroyed: true` with the value as of the last edit, right after
  `destroy()` and again after the sweep; a child of a destroyed node does too
  after the sweep; a freed node does; a live node reports its current value;
- a removed mod reports `modRemoved: true` with the value as of the last edit;
- a mod of the same type added again right away continues the same entry:
  one entry, the new mod's value, no `modRemoved`, and a later edit updates
  that entry;
- a mod of another type added in place of a removed one leaves the old entry
  at `modRemoved`, whatever address the new mod gets;
- a mod of a destroyed node reports `destroyed: true`.

CI (`examples/build_all.py --addon-tests-only`) builds and runs this on every
push/PR; a non-zero exit fails the job. Run it locally with:

```bash
trusscli run -p .          # from this directory
# or from the repo root, run every addon test harness:
./examples/build_all.py --addon-tests-only --verbose
```
