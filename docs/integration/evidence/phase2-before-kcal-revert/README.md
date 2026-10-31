# Superseded KCal-enabled build evidence

These files record the earlier build at commit `72ccb7e1d`, before the user
requested removal of KCal. Its local build and GitHub Actions run 41169050563
passed, but the resulting kernel **included KCal**.

Do not use these hashes, configurations, symbol lists, or the corresponding
GitHub Actions artifacts to identify the current KCal-free kernel. They are
retained only to make the integration and removal history auditable.

The KCal feature was reverted in `21eacd34b`. Current build evidence is in
`../phase2-no-kcal/`; build/download instructions are in
`../../04-build-and-download.md`.
