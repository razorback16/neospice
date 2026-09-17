# JOSS manuscript working draft

`paper.md` is a reviewable working draft, **not a submission-ready paper**.
The supplied author, affiliation and ORCID are included. Pending author facts
are visibly marked; no adoption, funding declaration, human review or DOI has
been invented. The date is the preparation date, not a journal submission date.

The draft follows the required sections and YAML/BibTeX structure in the
[official JOSS format](https://joss.readthedocs.io/en/latest/paper.html), checked
September 10, 2026. The five bibliography entries point to official software documentation and
a Sandia application note. Add appropriate primary research references when the
final scholarly argument and actual workflow are established.

## Evidence and unfinished claims

| Draft material | Evidence / work remaining |
|---|---|
| Author identity and AI tools | [Author-supplied information](../docs/joss-author-information.md); historical scope and human confirmation pending. |
| Architecture and trade-offs | [Source-based comparison](../docs/neospice-vs-ngspice.md); attribution and full documentation audit still open. |
| Frozen fixture counts | [Experiment definition](../docs/kicad-experiment.md), checkpoint-11 manifest. These are input counts, not final accuracy results. |
| Current ngspice47 compatibility defects | [Progress](../docs/joss-progress.md), [reference 47](../docs/ngspice47-reference.md); supported correctness blockers remain. |
| Actual research workflow and impact | Required author evidence pending. Demonstrations must not be described as adoption. |
| Benchmarks, figures and final compatibility tables | Pending accuracy-qualified measurements and final candidate evidence. No speedup claim is included. |
| Eligibility and installation | [JOSS requirements](../docs/joss-requirements-checklist.md); public-history and independent human checks remain. |
| Rendering and release | Updated ngspice47-only three-page draft with five references compiled and visually inspected in checkpoint30; final candidate evidence, release manifest and author approval remain. |

The impact section currently explains limitations of the working candidate.
It needs actual research evidence before it can support submission. Removing
the pending markers alone cannot close the readiness goal.

## Rendering

The [paper.pdf](paper.pdf) is rendered from the current ngspice47-only source with the official
Inara image pinned below. All three pages of the ngspice47-only rendering were visually inspected;
author details, required sections, pending markers and five references render.
The manuscript remains a working draft with pending research evidence. YAML metadata, required headings
and citation-key resolution were checked. The prior checkpoint13 rendering is
preserved in historical evidence. This is a format check, not author approval
or substantive readiness.

From the repository root, with Docker available:

```sh
docker run --rm --network=none --cap-drop=ALL \
  --security-opt=no-new-privileges --volume "$PWD/paper:/data" \
  --user "$(id -u):$(id -g)" --env JOURNAL=joss \
  openjournals/inara@sha256:a0414b8b72fd8923917ede614d340d98dc7aa3102aabc2e955e9c02a6100fd62
```

The downloaded image used LuaHBTeX/TeX Live 2024 and produced a draft-watermarked
PDF. Its `10.xxxxxx/draft` DOI, 1970 submission date, editor/reviewer entries and
unassigned volume/page fields are renderer placeholders, not actual journal
metadata. No DOI, submission or editorial appointment exists for this draft.
The PDF is not tagged; recheck the current journal rendering/accessibility
pipeline for the final candidate. Intermediate JATS output is ignored.

The final release still needs an updated, inspected PDF whose substantive
claims trace to final measurements and confirmed human facts. The current
draft deliberately exposes missing evidence instead of filling it in.

## September 11 source and rendering update

The state-of-the-field text and bibliography now include ngspice's shared API
and Sandia's documented Xyce C/Python interface. Existing C++ modularity and
embedding capabilities must not be presented as unique to neospice. The current PDF includes this edit and was compiled and inspected after
benchmark sampling finished. The complete 34-workload timing report is linked
from [benchmark methods](../docs/benchmark-methods.md); final research-use and
performance discussion in the manuscript remain unfinished.
Public-history bounds are recorded in [the history audit](../docs/joss-public-history.md);
first-public date and research-use evidence remain pending.

The current source targets ngspice47 exclusively. Previous draft renderings and
documentation are preserved in the pre-cleanup evidence archive.
