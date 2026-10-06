import { useEffect, useRef } from "react";
import { PARTS, snap, type Kind } from "./model";
import { Symbol } from "./Symbol";

// A view-only preview. The canvas owns the actual placement and undo entry.
export function PlacementCursor({
  kind,
  origin,
}: {
  kind: Kind;
  origin: { x: number; y: number; touch: boolean };
}) {
  const preview = useRef<HTMLDivElement>(null);
  useEffect(() => {
    let last = { x: origin.x, y: origin.y, touch: origin.touch };
    const draw = () => {
      const element = preview.current;
      if (!element) return;
      if (last.touch) {
        element.style.visibility = "hidden";
        return;
      }
      let x = last.x,
        y = last.y,
        size = 80;
      const target = document.elementFromPoint(x, y);
      const canvas = target?.closest<SVGSVGElement>(".schematic");
      if (canvas) {
        const matrix = canvas.getScreenCTM();
        if (matrix) {
          const local = new DOMPoint(x, y).matrixTransform(matrix.inverse());
          const placed = new DOMPoint(
            snap(local.x),
            snap(local.y),
          ).matrixTransform(matrix);
          x = placed.x;
          y = placed.y;
          size = 100 * Math.hypot(matrix.a, matrix.b);
        }
      }
      element.style.left = `${x}px`;
      element.style.top = `${y}px`;
      element.style.width = `${size}px`;
      element.style.height = `${size}px`;
      element.style.visibility = "visible";
    };
    const move = (event: PointerEvent) => {
      last = {
        x: event.clientX,
        y: event.clientY,
        touch: event.pointerType === "touch",
      };
      draw();
    };
    const leave = () => {
      if (preview.current) preview.current.style.visibility = "hidden";
    };
    const out = (event: PointerEvent) => {
      if (!event.relatedTarget) leave();
    };
    draw();
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerout", out);
    window.addEventListener("blur", leave);
    window.addEventListener("resize", draw);
    // Wheel zoom updates the SVG transform on the following frame.
    const wheel = () => requestAnimationFrame(draw);
    window.addEventListener("wheel", wheel, { passive: true });
    return () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerout", out);
      window.removeEventListener("blur", leave);
      window.removeEventListener("resize", draw);
      window.removeEventListener("wheel", wheel);
    };
  }, [kind, origin]);
  return (
    <div
      ref={preview}
      className="placement-cursor"
      data-testid="placement-preview"
      data-kind={kind}
      aria-hidden="true"
    >
      <svg viewBox="-50 -50 100 100">
        <rect
          className="placement-outline"
          x="-47"
          y="-47"
          width="94"
          height="94"
          rx="9"
        />
        <Symbol kind={kind} />
        {PARTS[kind].pins.map((pin) => (
          <circle
            key={pin.id}
            cx={pin.x}
            cy={pin.y}
            r="3"
            className="placement-pin"
          />
        ))}
      </svg>
    </div>
  );
}
