## What does this change?

## How was it tested?
- Game version (from `meta/0.paver` or the main menu):
- Relevant lines of `bin64/CampFundsWithdraw.log`:
- Offline check (`build/cfw_check.exe` under Wine) output, if signatures changed:

## Checklist
- [ ] Runtime patches only, no change to game files on disk, no anti-cheat/DRM bypass
- [ ] Signatures instead of fixed offsets; fails closed when something is missing
- [ ] I agree that this contribution is licensed under section 3 of [LICENSE](../LICENSE)
