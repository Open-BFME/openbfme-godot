# AI-assisted development

OpenBFME began as an experiment in whether frontier AI models could do sustained,
cross-disciplinary game-engine work rather than produce a disposable prototype.
The codebase demonstrates substantial AI-assisted output, but it does not by
itself establish a controlled benchmark result or prove model-level authorship.

## What AI has done

The project owner reports that models including Fable 5, ChatGPT Sol, and Kimi
K3 contributed to:

- Python importer and conversion code;
- Godot runtime, UI, simulation, and presentation code;
- source-format and behavior investigations;
- focused tests and malicious-input cases;
- performance and reliability instrumentation;
- documentation and engineering review; and
- debugging across large dependency graphs.

According to the owner, Kimi K3 and Sol were especially important during the
high-volume implementation pushes. The current Git history records the human
repository author but not a reliable model identity per change. Capability and
test claims therefore come from current code and runners in `STATUS.md`; model
credits remain owner testimony.

## What the human owner does

AI does not own the product or decide what counts as correct. Human direction is
responsible for:

- choosing scope and priorities;
- supplying and operating the lawfully owned original game;
- deciding whether observed behavior is acceptable;
- approving audiovisual comparisons;
- controlling private content and publication;
- rejecting invented or unsupported behavior; and
- deciding what is ready to show or release.

## Why the project uses strict evidence

AI can write plausible code that compiles, passes a narrow assertion, or appears
correct while misunderstanding the source game. OpenBFME therefore separates
implementation from proof.

A compatibility claim may require several forms of evidence:

- effective retail source and dependency evidence;
- deterministic importer output and provenance;
- focused regression or malicious-input tests;
- runtime behavior;
- comparison with the original game;
- human audiovisual review; and
- bounded reliability and performance measurement.

Parsed configuration, an imported asset, a passing helper test, or a convincing
AI explanation is not parity by itself.

## Current limitations of the experiment

- The repository has accumulated more internal documentation than a normal
  newcomer can navigate.
- Model-written scripts contain maintainer-machine assumptions that must be
  removed before public onboarding is credible.
- Large generated changes still need conventional review, clean commits, CI,
  security scanning, and clean-machine reproduction.
- Original-game visual judgment remains human work.
- The existence of broad playable systems does not prove skirmish parity or
  completion; current faction and presentation suites still contain failures.

The point of being open about AI is not to market the project as magic. It is to
make the experiment inspectable: what models can build, where they fail, and what
engineering controls are needed to turn their output into trustworthy software.
