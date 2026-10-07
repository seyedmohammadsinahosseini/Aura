## What & why

<!-- One paragraph: the problem, and the approach. Link an issue if there is one. -->

## Type of change

- [ ] Bug fix (behaviour was wrong)
- [ ] Feature (behaviour was missing)
- [ ] Refactor (behaviour unchanged)
- [ ] Docs / tooling only

## How was it verified?

CI runs the same three commands, but say what you ran **locally and on what hardware**:

```bash
cmake -S tests -B build/tests && cmake --build build/tests && ctest --test-dir build/tests --output-on-failure
python3 scripts/check_repo_hygiene.py
./scripts/format.sh --check
```

- [ ] Unit tests pass
- [ ] Hygiene check passes
- [ ] Formatting clean (`./scripts/format.sh` produced no diff)
- [ ] Full build + run on GPU hardware (`docs/BUILDING.md`), if the change touches inference/capture
- [ ] No new dependency introduced (or justified below)

## Behaviour / API / docs impact

- [ ] `docs/API.md` updated (endpoint added, changed or removed)
- [ ] `docs/CONFIGURATION.md` updated (config key added, changed or removed)
- [ ] `docs/SECURITY_AUDIT.md` updated (finding fixed or newly discovered)
- [ ] `CHANGELOG.md` has an entry
- [ ] Breaking change, and it is called out in the description

## Privacy check

- [ ] No face crops, databases, credentials or model weights in the diff
- [ ] Nothing in this PR enables surveillance beyond informed, opted-in subjects

## Notes for the reviewer

<!-- Anything surprising: a test you could not write, a trade-off you took, a file you deliberately did not touch. -->
