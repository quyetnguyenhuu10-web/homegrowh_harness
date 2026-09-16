# Windows edit stress harness

The stress executable is opt-in because the `standard` and `extreme` profiles
create large files and can consume significant disk space.

Build it with:

```powershell
cmake -S package/tools/filesystems -B build-windows `
  -DFILESYSTEMS_BUILD_WINDOWS_STRESS_TESTS=ON
cmake --build build-windows --config Release --target windows_edit_stress
```

Run a small validation pass:

```powershell
build-windows\Release\windows_edit_stress.exe `
  --profile smoke `
  --output .\windows-edit-stress-results
```

Run the large suite with a 4 GiB input, 500 short edits, and 50 long edits:

```powershell
build-windows\Release\windows_edit_stress.exe `
  --profile extreme `
  --output .\windows-edit-stress-results `
  --keep-files
```

Each run writes `results.csv`, `results.jsonl`, and `summary.txt`. Generate the
HTML analysis report with:

```powershell
node package/tools/filesystems/tests/windows_edit_stress_report.mjs `
  --input .\windows-edit-stress-results\results.csv `
  --output .\windows-edit-stress-results\report.html
```

The suite covers short and long replacements, missing old data, a multi-GB
replacement while another thread modifies the file, and an interference matrix
covering in-place modification, exclusive locking, deletion, rename, and an
unrelated file in the same directory. It records duration, working set, peak
working set, disk free space, Win32 error, watcher note, content verification,
and whether interference landed before `fsystem::edit` returned. For the lock
case, the harness additionally records whether the exclusive lock caused the
final `ReplaceFileW` call to fail with `ERROR_SHARING_VIOLATION`.

The lock interferer holds its exclusive handle for five seconds. A lock is
only considered an interference failure when it blocks the final
`ReplaceFileW` call; a lock that is acquired and released before that commit is
reported as inconclusive. Lock, modify, delete, rename, and unrelated-file
cases are reported separately.

The CTest suite also runs `chunk_matcher_stress`. It compares the streaming
matcher with an overlapping `std::string::find` oracle across deterministic
random chunk partitions, one-byte chunks, binary data, and the 64 KiB stream
boundary.
