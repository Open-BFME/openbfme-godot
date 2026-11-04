---
title: Golden Pins
model: gpt-6-1-terra
reasoning: low
input: full_diff
tools: [browse_code, modify_pr]
include: ["engine/tests/**", "engine/src/GameLogic/**", "engine/src/GameNetwork/**"]
maxBudgetPerRun: 1
maxBudgetPerPR: 3
conclusion: neutral
---

If this pull request changes no file matching `engine/tests/data/**/*.hashes`, stop now and make your entire final response exactly `All clear`.

Review only the hash pins that this pull request changes. Rule 3 in `REVIEW.md` at the repository root says what a pin change needs.

For each changed pin file, find the code change in the pull request that explains the new hashes, and find the `// pin: <reason>` comment that the pull request adds to a C++ test under `engine/tests/`.

If the comment is missing, or the reason does not fit the code change, post one inline comment on the pin file that says what is missing or what does not fit. Post at most one comment.

Do not comment on anything else in the pull request.

When nothing is wrong, make your entire final response exactly `All clear`.
