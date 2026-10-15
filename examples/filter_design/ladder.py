"""Doubly terminated Butterworth LC ladder low-pass netlists."""
import math

R0 = 50.0
FC = 1e6

def elements(order):
    """[(name, kind, value, node_a, node_b)] for an order-N ladder (odd k shunt C, even k series L)."""
    out = []
    node = "n0"
    for k in range(1, order + 1):
        g = 2 * math.sin((2 * k - 1) * math.pi / (2 * order))
        if k % 2:
            out.append((f"c{k}", "C", g / (2 * math.pi * FC * R0), node, "0"))
        else:
            nxt = f"n{k}"
            out.append((f"l{k}", "L", g * R0 / (2 * math.pi * FC), node, nxt))
            node = nxt
    # Name the output node "out".
    out = [(n, k, v, "out" if a == node else a, "out" if b == node else b)
           for n, k, v, a, b in out]
    return out, "out"

def netlist(order, values=None, analysis=".ac dec 20 1e4 1e8", params=False):
    """Netlist text. values overrides element values by name. params=True
    writes each value as a .param so studies can vary it by name."""
    elems, last = elements(order)
    lines = [f"Butterworth LC ladder order {order}"]
    if params:
        for name, kind, v, a, b in elems:
            v = values.get(name, v) if values else v
            lines.append(f".param p{name}={v!r}")
    lines += ["V1 in 0 DC 0 AC 1", f"RS in n0 {R0}"]
    for name, kind, v, a, b in elems:
        v = values.get(name, v) if values else v
        val = f"{{p{name}}}" if params else repr(v)
        lines.append(f"{name} {a} {b} {val}")
    lines.append(f"RLOAD {last} 0 {R0}")
    if analysis:
        lines.append(analysis)
    lines.append(".end")
    return "\n".join(lines) + "\n"

if __name__ == "__main__":
    print(netlist(5))
