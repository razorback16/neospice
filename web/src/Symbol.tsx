import type { Kind } from "./model";
export function Symbol({ kind }: { kind: Kind }) {
  const lineProps = {
    fill: "none",
    stroke: "currentColor",
    strokeWidth: 2,
    strokeLinecap: "round" as const,
    strokeLinejoin: "round" as const,
  };
  return (
    <g {...lineProps}>
      {kind === "R" && (
        <path d="M-40 0H-25L-20 -9 -12 9 -4 -9 4 9 12 -9 20 9 25 0H40" />
      )}
      {kind === "C" && <path d="M-40 0H-7M-7 -17V17M7 -17V17M7 0H40" />}
      {kind === "L" && (
        <path d="M-40 0H-26c0-22 13-22 13 0c0-22 13-22 13 0c0-22 13-22 13 0c0-22 13-22 13 0H40" />
      )}
      {(kind === "V" || kind === "I") && (
        <>
          <path d="M0 -40V-23M0 23V40" />
          <circle r="23" />
          {kind === "V" ? (
            <path d="M-6 -8H6M0 -14V-2M-6 10H6" />
          ) : (
            <path d="M0 14V-14M-5 -7L0 -14 5 -7" />
          )}
        </>
      )}
      {kind === "D" && (
        <>
          <path d="M-40 0H-15M15 0H40M15 -17V17M-15 -17L15 0 -15 17Z" />
        </>
      )}
      {(kind === "QN" || kind === "QP") && (
        <>
          <path d="M-40 0H-12M-12 -20V20M-12 -12L20 -30V-40M-12 12L20 30V40" />
          {kind === "QN" ? (
            <path d="M9 20L19 29 6 27Z" fill="currentColor" />
          ) : (
            <path d="M3 16L9 28 15 19Z" fill="currentColor" />
          )}
        </>
      )}
      {(kind === "MN" || kind === "MP") && (
        <>
          <path d="M-40 0H-16M-16 -24V24M-8 -24V-10M-8 -6V6M-8 10V24M-8 -20H20V-40M-8 20H20V40M-8 0H40" />
          {kind === "MN" ? (
            <path d="M7 -5L-2 0 7 5" />
          ) : (
            <path d="M-1 -5L8 0 -1 5" />
          )}
        </>
      )}
      {(kind === "OP" || kind === "CMP") && (
        <>
          <path d="M-24 -30L28 0 -24 30ZM-40 -20H-24M-40 20H-24M28 0H40M-18 -17H-10M-18 17H-10M-14 13V21" />
        </>
      )}
      {kind === "CMP" && <path d="M0 -40V-16M0 16V40M-8 5H-2V-5H5" />}
      {kind === "G" && <path d="M0 0V12M-16 12H16M-10 19H10M-4 26H4" />}
    </g>
  );
}
export function MiniSymbol({ kind }: { kind: Kind }) {
  return (
    <svg viewBox="-50 -50 100 100" aria-hidden="true">
      <Symbol kind={kind} />
    </svg>
  );
}
