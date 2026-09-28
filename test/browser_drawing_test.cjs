// Run against scripts/serve_web.py. Uses a fresh browser context, leaving personal workspaces untouched.
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const artifact = name => require('node:path').join(require('node:os').tmpdir(), name);
(async () => {
  const browser = await chromium.launch({headless: true,
    ...(process.env.CHROMIUM_EXECUTABLE ? {executablePath: process.env.CHROMIUM_EXECUTABLE} : {}),
    args: ['--enable-webgl', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader']});
  try {
    const page = await browser.newPage({viewport: {width: 1440, height: 900}, deviceScaleFactor: 2});
    const errors = [];
    page.on('pageerror', e => errors.push(e.message));
    await page.goto(process.env.CHARTROOM_URL || 'http://localhost:8765/chartroom.html');
    await page.waitForFunction(() => typeof FS !== 'undefined' && FS.analyzePath('/data/cache/535059.1d.json').exists, {}, {timeout: 60000});
    const state = () => page.evaluate(() => JSON.parse(FS.readFile('/data/workspace.json', {encoding: 'utf8'})));
    const settle = () => page.waitForTimeout(1200);
    await settle();
    const before = await state();
    await page.mouse.move(800, 400);
    await page.mouse.wheel(160, 12);
    await page.waitForTimeout(40);
    await page.mouse.wheel(1, 3); // Momentum tail has a larger vertical component.
    await settle();
    const panned = await state();
    assert.equal(panned.charts[0].view.count, before.charts[0].view.count);
    assert(panned.charts[0].view.first > before.charts[0].view.first);
    await page.mouse.wheel(-160, -10);
    await settle();
    assert((await state()).charts[0].view.first < panned.charts[0].view.first);
    const unchanged = (await state()).charts[0].view;
    await page.mouse.click(482, 102); // Open the persistent drawing tools window.
    await page.waitForTimeout(200);
    await page.screenshot({path: artifact('chartroom-draw-menu.png')});
    await page.mouse.click(1140, 256); // Trend line in the floating tool window.
    await page.mouse.move(600, 340);
    await page.mouse.down();
    await page.mouse.move(1000, 480, {steps: 8});
    await page.mouse.up();
    await settle();
    let doc = await state();
    assert.equal(doc.drawings.SPY?.length, 1, 'Trend drawing was created');
    assert.equal(doc.drawings.SPY[0].kind, 'trend');
    assert.equal(doc.charts[0].view.first, unchanged.first, 'Drawing must not pan');
    const original = doc.drawings.SPY[0];
    await page.keyboard.press('Control+z');
    await settle();
    assert.equal((await state()).drawings.SPY?.length || 0, 0);
    await page.keyboard.press('Control+Shift+z');
    await settle();
    assert.deepEqual((await state()).drawings.SPY[0], original);
    // Right-click the line body to inspect and edit it.
    await page.mouse.click(800, 410, {button: 'right'});
    await page.waitForTimeout(250);
    await page.screenshot({path: artifact('chartroom-drawing-settings.png')});
    await page.mouse.click(900, 548); // Start price.
    await page.waitForTimeout(150); // Let the canvas consume the focus click before keyboard shortcuts.
    await page.keyboard.press('Control+a');
    await page.waitForTimeout(50);
    await page.keyboard.type('750.25');
    await page.waitForTimeout(150);
    await page.mouse.click(832, 609); // Apply.
    await settle();
    assert.equal((await state()).drawings.SPY[0].a[1], 750.25);
    await page.keyboard.press('Control+z');
    await settle();
    assert.deepEqual((await state()).drawings.SPY[0], original);
    // Measurement neither moves the view nor changes saved drawings.
    await page.keyboard.down('Shift');
    await page.mouse.move(650, 550); await page.mouse.down();
    await page.mouse.move(1050, 380, {steps: 8}); await page.mouse.up();
    await page.keyboard.up('Shift');
    await settle();
    doc = await state();
    assert.deepEqual(doc.drawings.SPY[0], original);
    assert.equal(doc.charts[0].view.first, unchanged.first);
    await page.screenshot({path: process.env.CHARTROOM_SCREENSHOT || artifact('chartroom-measurement.png')});
    await page.keyboard.press('Escape');
    // Place and edit a text note through the actual popup controls.
    await page.mouse.click(482, 102);
    await page.waitForTimeout(150);
    await page.mouse.click(1140, 325); // Text note in the floating tool window.
    await page.waitForTimeout(200);
    await page.mouse.click(600, 250);
    await page.waitForTimeout(200);
    await page.mouse.click(650, 395); // Note text area.
    await page.waitForTimeout(150); // Let the canvas consume the focus click before keyboard shortcuts.
    await page.keyboard.press('Control+a');
    await page.waitForTimeout(50);
    await page.keyboard.type('Support zone');
    await page.waitForTimeout(150);
    await page.screenshot({path: artifact('chartroom-text-note.png')});
    await page.mouse.click(629, 508); // Apply.
    await settle();
    let notes = (await state()).drawings.SPY;
    assert.equal(notes.length, 2);
    assert.equal(notes[1].kind, 'text');
    assert.equal(notes[1].text, 'Support zone');
    // Two-click Fibonacci placement leaves the first click as a preview only.
    await page.mouse.click(1140, 371);
    await page.mouse.click(620, 550);
    await settle();
    assert.equal((await state()).drawings.SPY.length, 2);
    await page.mouse.move(1000, 260, {steps: 8});
    await page.mouse.click(1000, 260);
    await settle();
    notes = (await state()).drawings.SPY;
    assert.equal(notes.length, 3);
    assert.equal(notes[2].kind, 'fib');
    assert(notes[2].a[1] < notes[2].b[1]);
    assert.deepEqual(notes[2].fib.levels.filter(x => x.enabled).map(x => x.ratio), [0,.236,.382,.5,.618,.786,1]);
    assert.equal((await state()).charts[0].view.first, unchanged.first);
    await page.screenshot({path: artifact('chartroom-fibonacci.png')});
    // Edit the Fibonacci-specific controls through the palette's settings action.
    await page.mouse.click(1140, 466);
    await page.waitForTimeout(200);
    await page.mouse.click(710, 429); // Reverse.
    await page.mouse.click(811, 461); // Extend right.
    await page.mouse.click(787, 491); // Percentages.
    await page.mouse.click(711, 553); // No background.
    await page.mouse.click(789, 647); // Customize 0.236 -> 0.25.
    await page.waitForTimeout(150); // Let the canvas consume the focus click before keyboard shortcuts.
    await page.keyboard.press('Control+a');
    await page.waitForTimeout(50);
    await page.keyboard.type('0.25');
    await page.waitForTimeout(150);
    await page.mouse.click(721, 877); // Apply.
    await settle();
    notes = (await state()).drawings.SPY;
    assert.equal(notes[2].fib.reverse, true);
    assert.equal(notes[2].fib.extend_right, true);
    assert.equal(notes[2].fib.percentages, true);
    assert.equal(notes[2].fib.background, false);
    assert.equal(notes[2].fib.levels[1].ratio, .25);
    // Move the palette, then allow ImGui's layout-save timer to persist its position.
    await page.mouse.move(1220, 147); await page.mouse.down();
    await page.mouse.move(1120, 227, {steps: 8}); await page.mouse.up();
    await page.mouse.move(700, 700);
    await page.waitForTimeout(6500);
    const paletteState = await state();
    assert.equal(paletteState.drawing_tools_open, true);
    assert(paletteState.ini.includes('[Window][Drawing tools]'));
    await page.reload();
    await page.waitForFunction(() => typeof FS !== 'undefined' && FS.analyzePath('/data/workspace.json').exists);
    await settle();
    assert.deepEqual((await state()).drawings.SPY, notes, 'Drawings survive IndexedDB reload');
    assert.equal((await state()).drawing_tools_open, true);
    assert.equal((await state()).ini, paletteState.ini, 'Floating window layout survives reload');
    assert.deepEqual(errors, []);
    console.log('Browser swipes, floating palette, Fibonacci placement/settings, undo/redo, measurement and restart passed');
  } finally { await browser.close(); }
})().catch(e => {console.error(e); process.exit(1);});
