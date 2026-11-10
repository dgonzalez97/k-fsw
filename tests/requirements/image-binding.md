# Report and image binding

The optional image check joins one executed Robot report to an image, its resolved
configuration, and the source/profile context expected by the runner. It checks
file hashes and matching metadata. It does not prove that an image came from the
named sources, that the runner used it, or that physical acceptance occurred.
Those claims need a controlled build and execution record kept with the files.

Use the hosted software selection first. Its `evidence_kind` is `hosted`; the L496
selection declares `physical`. Binding mode requires the selection to declare its
kind. A hosted context cannot satisfy a physical selection or `--board`.

The controlled runner records its expected context independently of the sidecar.
Version 1 has these fields. Unknown and duplicate fields fail validation.

| Field | Value |
| --- | --- |
| `version` | Integer `1` |
| `sources` | Repository names mapped to full 40-character lowercase commit SHAs, including `k-fsw`; same representation as release inputs |
| `snapshot_sha256` | Optional SHA-256 of an archived dirty source snapshot; omit only when appropriate for the recorded build |
| `profile` | Exact nonempty profile name, such as `linux-robot` |
| `config_sha256` | SHA-256 of the image's resolved `.config` bytes |
| `evidence_kind` | `hosted` or `physical`, matching the required selection |

Every SHA-256 is 64 lowercase hexadecimal characters. The source map must record
the repositories used by the composition. The counter validates its form and
equality, not whether it is a complete west manifest. A snapshot digest refers to
an archive retained by the runner; the counter does not inspect that archive.
Absence of the optional snapshot field is not proof of a clean tree.

The sidecar is another strict version 1 JSON object:

```json
{
  "version": 1,
  "report_sha256": "<SHA-256 of output.xml>",
  "image_sha256": "<SHA-256 of the executed image>",
  "context": {
    "version": 1,
    "sources": {"k-fsw": "<full resolved commit SHA>"},
    "profile": "linux-robot",
    "config_sha256": "<SHA-256 of resolved .config>",
    "evidence_kind": "hosted"
  }
}
```

Replace the placeholders with real values. The embedded context must equal the
independently recorded expected context exactly, including any snapshot field.
Do not generate the expected context by copying an untrusted sidecar.

From the workspace root, validate the files retained by the runner:

```bash
python3 k-fsw/tools/ci/robot-counts.py build/proof/robot/output.xml \
  --required k-fsw/tests/requirements/software.json \
  --binding build/proof/binding.json \
  --image build/proof/linux/zephyr/zephyr.exe \
  --config build/proof/linux/zephyr/.config \
  --context build/proof/expected-context.json
```

All four binding options are required together, with `--required`. Without them,
ordinary counting and required-case checking retain their existing behavior.
`IMAGE BINDING: PASS` means the actual file hashes and expected metadata match.
Required tests still have to appear once with PASS. Neither a timestamp nor a
matching hash establishes freshness, authenticity or independent provenance.

For a new two-case hosted capture, first build the `linux` target into an isolated
output root with `tests/config/param-fixtures.conf` and
`config/profiles/linux-temperature.conf`, as `tools/ci/robot.sh` does. Set both
`KFSW_OUTPUT_ROOT` and `KFSW_BUILD_DIR` for execution: the boot fixture chooses
`$KFSW_OUTPUT_ROOT/linux/zephyr/zephyr.exe`; the persistence fixture chooses
`$KFSW_BUILD_DIR/zephyr/zephyr.exe`. Keep them pointed at the same image.

```bash
KFSW_OUTPUT_ROOT="$PWD/build/proof" \
KFSW_BUILD_DIR="$PWD/build/proof/linux" \
KFSW_ROBOT_OUT_DIR="$PWD/build/proof/robot" \
  k-fsw/tests/hil/run.sh \
  --test 'Hosted Image Reports Boot And Ready' \
  --test 'KFSW Linux Persists Parameters Across Restart'
```

Keep the build log, source revisions and any dirty snapshot, resolved `.config`,
executed image, execution command and Robot output together. Check that source
inputs and image/configuration bytes remain unchanged across the run. The test
fixtures use private emulated flash files. This is hosted restart persistence,
not silicon reset or physical flash endurance evidence. Do not attach an image
to an old report unless its original execution record establishes that pair.
Binding is opt-in; current CI does not create a sidecar automatically.
