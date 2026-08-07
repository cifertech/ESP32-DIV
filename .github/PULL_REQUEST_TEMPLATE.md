## Description

Briefly describe **what** this PR changes and **why**.

Fixes # (issue number, if applicable)

---

## Type of Change

<!-- Put an `x` inside the brackets that apply -->

- [ ] `feat` — New feature or module
- [ ] `fix` — Bug fix
- [ ] `docs` — Documentation only
- [ ] `refactor` — Code refactor (no feature or fix)
- [ ] `chore` — Build, CI, dependency update
- [ ] `style` — Formatting / whitespace only

---

## Hardware Tested On

<!-- Check all that you have tested with -->

- [ ] ESP32-DIV v2 (Main board + Shield)
- [ ] ESP32-DIV v1 (Main board + Shield)
- [ ] Main board only (no shield)
- [ ] DIY / custom build
- [ ] Not applicable (docs / CI only)

---

## Checklist

- [ ] My branch is up to date with `main` (`git fetch upstream && git rebase upstream/main`)
- [ ] I have tested the change on actual hardware (or noted why this is not applicable)
- [ ] The sketch compiles without errors in Arduino IDE with the correct board settings
- [ ] I have not introduced any new blocking `delay()` calls > 200 ms in feature loops
- [ ] New display code uses the theme constants from `shared.h` (`UI_BG`, `UI_FG`, `UI_TEXT`, etc.)
- [ ] I have updated the README / Wiki if my change affects user-facing behaviour
- [ ] My commit messages follow the [Conventional Commits](https://www.conventionalcommits.org/) format

---

## Screenshots / Video (if applicable)

<!-- Drag and drop images or paste a link to a short video showing the change on the device -->

---

## Additional Notes

<!-- Anything else reviewers should know? -->
