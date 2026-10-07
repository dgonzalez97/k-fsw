# Contributing

K-FSW is built from five repositories in one west workspace. How to set it up,
how branches and commits are named, and what to check before a pull request are
in the [contributing guide](https://dgonzalez97.github.io/k-fsw/development.html).

In short:

- branch from `develop` and open the pull request against it;
- write commit subjects as `[AREA][TOPIC] Imperative summary`;
- run the checks the change needs, `./k-fsw/tools/ci/all.sh` for all of them,
  from the workspace root.

Report problems here, including how the five repositories fit together.
