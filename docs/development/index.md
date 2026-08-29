# Development {#development}

## Branch-to-merge workflow

Create a topic branch for each change; do not work directly on `main`.

```text
issue
  -> branch
  -> commits
  -> push branch
  -> pull request
  -> CI
  -> review
  -> merge commit
  -> main
```

`main` is the integrated green baseline. Keep each branch tied to one issue or
goal, and update documentation in the same PR as the behavior or API it
describes.

## Branch naming

Use the issue number and a short lowercase slug:

| Change | Pattern | Example |
| --- | --- | --- |
| Feature | `feature/<issue>-<slug>` | `feature/42-command-router` |
| Fix | `fix/<issue>-<slug>` | `fix/43-storage-timeout` |
| Tests | `test/<issue>-<slug>` | `test/44-param-errors` |
| CI | `ci/<issue>-<slug>` | `ci/15-docs-pages` |
| Documentation | `docs/<issue>-<slug>` | `docs/45-port-map` |

Create the issue first, update local `main` with a fast-forward-only pull, then
create the dedicated branch. Do not reuse an unrelated branch.

## Commits

Use focused commits whose subject identifies the subsystem and intent. Keep a
working implementation, its tests, and closely coupled documentation together;
do not create one commit per small documentation page.

Examples:

```text
[CI][PR] Add pull-request integration checks
[DOCS][DOXYGEN] Add generated SDK documentation
[DOCS][PAGES] Publish SDK documentation from main
```

## Pull requests and CI

Push the branch, open a pull request that closes the issue, and wait for every
required software check. Reviewers should be able to reproduce each job using
the command in @ref testing. Pull requests build documentation but never
deploy the public Pages site.

Use a merge commit after approval and green CI. This preserves the reviewed
branch as one integrated change and is important when another repository pins
an exact tested commit. Delete the remote topic branch after merge when it is
no longer needed.

## Multi-repository west changes

Each owned repository has independent Git history. A `k-fsw` PR cannot make an
unpublished commit in `kfsw-services`, `kfsw-platform`, or `kfsw-comms`
available to GitHub-hosted CI.

For a dependency and composition change:

1. Create a branch and commits in the dependency repository.
2. Push that dependency branch and open its PR.
3. In a separate `k-fsw` branch, pin the exact dependency commit in `west.yml`.
4. Push the composition branch and open its PR.
5. Confirm hosted CI reproduces the workspace from the published pin.
6. Merge the dependency PR first using a merge commit when SHA preservation
   matters.
7. Merge the `k-fsw` PR after the pinned commit is reachable from the declared
   remote.

Never rewrite a dependency commit that an open or merged `k-fsw` change pins.
If policy requires a rebase or squash, update the composition pin and rerun CI
before merge.

## Adding a module

Place reusable platform mechanisms, services, communications, or equipment
clients in the repository that owns that abstraction. Keep product enablement,
supported targets, Kconfig selection, and lifecycle order in `k-fsw`. Add a
public header under the owner's `include/kfsw/` hierarchy only for APIs intended
for cross-module use.

Update `west.yml` only after the dependency commit is published. Add unit tests
near the module and end-to-end coverage in the composition repository when the
behavior crosses repository boundaries.

## Adding documentation

- Put ownership-specific detail beside its implementation repository.
- Put aggregate setup, architecture, command, testing, and workflow guidance
  under `k-fsw/docs/`.
- Add useful Doxygen comments to public headers: purpose, lifecycle, arguments,
  return contracts, constraints, and ownership/threading where relevant.
- Do not duplicate upstream documentation or include upstream source trees in
  the API reference.
- Run `./k-fsw/tools/ci/docs.sh` and inspect the HTML before requesting review.

Generated HTML belongs in `build/docs/html/` and is not committed.

## Main branch protection

Configure repository rules in GitHub after the workflow is merged:

- require a pull request before merging;
- require at least one approving review if the project has an independent
  reviewer available;
- require branches to be up to date before merging;
- require all check names listed in @ref testing;
- block force pushes and deletions;
- restrict direct pushes to `main`, while allowing administrators to bypass
  only for recovery.

Repository settings are administrative state and are not changed by this
source tree.
