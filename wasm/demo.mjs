const $ = id => document.getElementById(id);
let worker, nextId = 0, activeId = 0, timer;
function reset() {
  clearTimeout(timer);
  worker?.terminate();
  worker = new Worker(new URL('./worker.mjs', import.meta.url), {type: 'module'});
  worker.onmessage = receive;
  worker.onerror = event => fail(event.message || 'Worker failed to load');
  activeId = ++nextId;
  $('run').disabled = false;
  $('slider').disabled = true;
}
function fail(message) {
  $('state').textContent = message;
  $('state').className = 'error';
  $('run').disabled = false;
  $('slider').disabled = true;
}
function simulate(action = 'run') {
  activeId = ++nextId;
  $('state').className = '';
  $('state').textContent = 'Simulating…';
  $('run').disabled = true;
  worker.postMessage({id: activeId, action, netlist: $('netlist').value,
    mode: $('mode').value, parameter: $('parameter').value, value: Number($('slider').value)});
}
function receive({data}) {
  if (data.id !== activeId) return;
  $('run').disabled = false;
  if (data.error) return fail(data.error);
  const {result} = data;
  if (!result.status.converged) return fail('Simulation did not converge');
  const key = `v(${$('signal').value.trim().toLowerCase()})`;
  const y = result.voltages[key];
  if (y === undefined) return fail(`No output signal ${key}`);
  $('slider').disabled = false;
  if (typeof y === 'number') {
    $('reading').textContent = `${key} = ${y.toPrecision(7)} V`;
    plot([0, 1], [y, y], 'DC operating point', false);
  } else if (result.frequency) {
    const magnitude = y.map(v => 20 * Math.log10(Math.max(1e-30, Math.hypot(v.real, v.imag))));
    $('reading').textContent = `${key} · magnitude (dBV)`;
    plot(result.frequency, magnitude, 'Frequency (Hz, logarithmic)', true);
  } else {
    $('reading').textContent = `${key} · voltage (V)`;
    plot(result.time, y, 'Time (seconds)', false);
  }
  $('state').textContent = `Completed in ${(result.status.elapsedSeconds * 1000).toFixed(2)} ms · ${typeof y === 'number' ? '1 point' : `${y.length} points`}`;
}
function plot(x, y, label, logarithmic) {
  const canvas = $('plot'), ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  const xx = logarithmic ? x.map(Math.log10) : x;
  const xmin = xx[0], xmax = xx.at(-1);
  let ymin = Infinity, ymax = -Infinity;
  for (const value of y) { ymin = Math.min(ymin, value); ymax = Math.max(ymax, value); }
  if (ymin === ymax) { ymin -= Math.max(0.1, Math.abs(ymin) * 0.1); ymax += Math.max(0.1, Math.abs(ymax) * 0.1); }
  ctx.strokeStyle = '#344155'; ctx.fillStyle = '#a9b8cb'; ctx.font = '13px system-ui';
  for (let i = 0; i <= 4; ++i) {
    const py = 25 + i * 62;
    ctx.beginPath(); ctx.moveTo(70, py); ctx.lineTo(620, py); ctx.stroke();
    ctx.fillText((ymax - i * (ymax - ymin) / 4).toPrecision(3), 4, py + 4);
  }
  ctx.strokeStyle = '#54d9bb'; ctx.lineWidth = 2; ctx.beginPath();
  xx.forEach((v, i) => {
    const px = 70 + 550 * (v - xmin) / (xmax - xmin || 1);
    const py = 273 - 248 * (y[i] - ymin) / (ymax - ymin);
    if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
  });
  ctx.stroke(); ctx.fillText(label, 220, 320);
  ctx.fillText(x[0].toPrecision(3), 70, 297); ctx.fillText(x.at(-1).toPrecision(3), 560, 297);
}
$('run').onclick = () => simulate();
$('cancel').onclick = () => { reset(); $('state').className = ''; $('state').textContent = 'Worker reset. Ready to run.'; };
$('slider').oninput = () => {
  $('parameter-value').textContent = `${$('slider').value} Ω`;
  clearTimeout(timer); timer = setTimeout(() => simulate('tune'), 80);
};
reset();
