# JOSS requirements and evidence

Official submission and paper-format guidance rechecked September 11, 2026;
review criteria and linked AI policy checked September 10. Recheck at handoff.
This is an author preparation checklist, not an editorial decision. Engineering
status is tracked in [JOSS progress](joss-progress.md).

## Submission gates

The submission guidance requires more than six months of public, active
development; actual research use (the developers' own use can qualify); an
iterative history; and working open-source practices. The repository must allow
public inspection, cloning and participation. A new package should have been
tried by a colleague. Tagged archival release and DOI creation follow successful
review. Funding, sponsor involvement and conflicts must be disclosed.
[Source: submission guidance](https://joss.readthedocs.io/en/latest/submitting.html).

| Gate | Local evidence and remaining work |
|---|---|
| Public-development duration | Local first commit April15; GitHub creation April18; PyPI uploads start June17. Actual first-public transition and six-month eligibility remain unproven. See [public history](joss-public-history.md). |
| Research use | User has not yet supplied an actual research workflow. Demonstrations must be labeled as demonstrations. |
| Iteration and open practice | Local history, tests, CI and CONTRIBUTING exist. Public PyPI release metadata is captured; engagement and sustained public history still need interpretation. |
| Accessible repository | Public GitHub API metadata for `razorback16/neospice` was independently fetched September11. Clean-clone/participation workflow checks remain. |
| Installation by a new user | Fresh local wheel passes Python tests. A colleague's installation/workflow check remains pending. |
| Funding, conflicts and authorship | See [author information](joss-author-information.md); contributions and disclosures remain pending. |

## Software review

Reviewers assess licensing, installation, functionality, documentation, tests,
contribution/support pathways, research significance and sustained development.
Single-author software can supply community evidence outside repository PRs.
Existing solutions do not preclude publication, but relevant prior work must
be cited. The review criteria permit demonstrated near-term significance in
the impact section; this does not erase the submission page's actual-use gate.
[Source: review criteria](https://joss.readthedocs.io/en/latest/review_criteria.html).

| Area | Evidence required to close |
|---|---|
| License and attribution | Review LICENSE, NOTICE, CREDITS and distributed dependencies against actual code. |
| Supported functionality | Finish device/analysis matrix, numerical fixes and explicit unsupported-operation handling. |
| Verification | Required suites and reproducible compatibility experiments; resolve current supported failures. |
| Reuse and maintenance | Verify installation, core API examples, extension guidance, issue reporting and support instructions. |
| Scholarly significance | Document actual research needs and evidence; compare relevant alternatives without unsupported novelty claims. |

## Manuscript format

Use `paper.md` with YAML metadata and a BibTeX bibliography, committed alongside
the software. Target 750–1,750 words. Required sections cover Summary, Statement
of need, State of the field, Software design, Research impact statement and
AI usage disclosure. Include authors/affiliations, acknowledgments and references.
Explain why building this package was appropriate given existing alternatives.
Keep API instructions in the software documentation. Compile and inspect the PDF.
[Source: paper format](https://joss.readthedocs.io/en/latest/paper.html).

The [paper guide](../paper/README.md) provides a pinned Inara build command
for the working manuscript and bibliography. Regenerate the PDF from current sources.
Actual research evidence, final measurements/figures, completed disclosures,
final rendering and author approval remain pending. Every quantitative claim
must trace to the final candidate's saved experiment evidence; a compiled draft
does not establish submission readiness.

## AI disclosure and human responsibilities

Disclose tools/models and versions, where they were used, the kinds of assistance,
and verification methods. Human authors must confirm their own review and core
design decisions. Editorial/reviewer conversation must remain human-authored
except for permitted translation assistance.
[Source: JOSS AI policy](https://joss.theoj.org/about#ai-usage-policy).

Codex and Claude Code are confirmed by the user. Historical versions and scope,
and the human review assertion, remain pending. Automated tests cannot supply
that assertion. This goal prepares local artifacts; external submission,
correspondence, release publication and archival DOI creation remain user actions.
