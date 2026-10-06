import assert from 'node:assert/strict';
const {chromium} = await import(process.env.PLAYWRIGHT_MODULE ?? 'playwright');
const browser = await chromium.launch({headless: true,
  ...(process.env.CHROME_BINARY ? {executablePath: process.env.CHROME_BINARY} : {})});
try {
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(process.argv[2] ?? 'http://127.0.0.1:8765/');
  await page.locator('#run').click();
  await page.waitForFunction(() => document.querySelector('#state').textContent.startsWith('Completed'));
  assert.match(await page.locator('#reading').textContent(), /0\.9090909/);
  await page.locator('#slider').evaluate(element => {element.value = '2000';element.dispatchEvent(new Event('input'));});
  await page.waitForFunction(() => document.querySelector('#reading').textContent.includes('0.8333333'));
  await page.selectOption('#mode', 'ac');
  await page.locator('#run').click();
  await page.waitForFunction(() => document.querySelector('#reading').textContent.includes('magnitude'));
  if (process.env.SCREENSHOT_PATH) await page.screenshot({path: process.env.SCREENSHOT_PATH, fullPage: true});
  await page.selectOption('#mode', 'transient');
  await page.locator('#run').click();
  await page.waitForFunction(() => document.querySelector('#reading').textContent.includes('voltage (V)'));
  await page.locator('#netlist').fill('Invalid include\n.include absent.lib\n.end\n');
  await page.locator('#run').click();
  await page.waitForFunction(() => document.querySelector('#state').className === 'error');
  assert.match(await page.locator('#state').textContent(), /inline/);
  await page.locator('#cancel').click();
  assert.match(await page.locator('#state').textContent(), /Worker reset/);
  assert.deepEqual(errors, []);
  console.log('Browser worker, DC/AC/transient, slider, errors and reset passed');
} finally { await browser.close(); }
