---
title: Edition Impact
model: gpt-6-1-sol
reasoning: low
input: incremental
tools: [browse_code, modify_pr]
include: ["engine/src/**"]
exclude: ["engine/thirdparty/**"]
maxBudgetPerRun: 2
maxBudgetPerPR: 6
conclusion: neutral
---

Act only when the pull request changes an INI parser, a field or data table, a module registry, or a startup or load path. Rule 1 in `REVIEW.md` at the repository root says what such a change must do.

For each such change, check the effect on BFME1, BFME2 and RotWK data. Flag:

- a parser that newly rejects a field that another edition's data uses;
- a new assumption about which data is mounted.

Older code is not a finding. Do not ask for broad cleanup. Name the edition that breaks and the field or file that breaks it.

When there are no findings, make your entire final response exactly `All clear`.
