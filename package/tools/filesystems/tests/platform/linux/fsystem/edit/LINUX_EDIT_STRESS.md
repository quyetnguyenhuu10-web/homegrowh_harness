# Linux edit stress harness

The Linux stress harness is opt-in because the standard and extreme profiles
create large files and consume disk space.

Build it with:

```bash
cmake -S package/tools/filesystems -B build-linux \
  -DFILESYSTEMS_BUILD_LINUX_STRESS_TESTS=ON
cmake --build build-linux --config Release --target linux_edit_stress
```

Run a small validation pass:

```bash
build-linux/linux_edit_stress \
  --profile smoke \
  --output ./linux-edit-stress-results
```

Or run it through CTest:

```bash
ctest --test-dir build-linux \
  --output-on-failure \
  -R '^linux_edit_stress_smoke_test$'
```

The harness covers short and long replacements, missing old data, large-file
replacement, and Linux interference cases for in-place modification, deletion,
rename, and unrelated-file writes. It records edit timing, resident/peak
memory, free disk space, error codes, watcher observations, and whether an
interference attempt landed before edit completed.

The streaming matcher is not duplicated here; it is covered by the shared
`chunk_matcher_stress_test`.
