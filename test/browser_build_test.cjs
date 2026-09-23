// Isolated browser context: update metadata is mocked, installed build files stay untouched.
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
(async () => {
  const browser = await chromium.launch({headless:true,
    ...(process.env.CHROMIUM_EXECUTABLE ? {executablePath:process.env.CHROMIUM_EXECUTABLE} : {}),
    args:['--enable-webgl','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
  try {
    const page = await browser.newPage({viewport:{width:1440,height:900}});
    const errors=[];
    page.on('pageerror', e=>errors.push(e.message));
    await page.route('**/chartroom.html', async route=>{
      const response=await route.fetch();
      const html=(await response.text()).replace('var Module={','var Module={arguments:["--offline"],');
      await route.fulfill({response,body:html});
    });
    let requests=0, manifest=null, newer=false;
    await page.route('**/build-info.json', async route=>{
      requests++;
      if(!manifest) manifest=await (await route.fetch()).json();
      await route.fulfill({json:{...manifest,build_number:manifest.build_number+(newer?1:0)}});
    });
    await page.clock.install();
    await page.goto(process.env.CHARTROOM_URL || 'http://localhost:8765/chartroom.html');
    await page.waitForFunction(()=>Module.chartroomAvailableBuild && Module.chartroomStats?.frames>=3);
    const running=await page.evaluate(()=>({number:Module.chartroomBuild,version:Module.chartroomVersion}));
    assert.equal(running.number,manifest.build_number);
    assert.equal(running.version,manifest.version);
    await page.mouse.click(482,76); // Persist an open tools window before using Reload.
    await page.waitForTimeout(300);
    await page.mouse.move(700,700);
    await page.clock.fastForward(6500);
    await page.waitForTimeout(1200);
    const state=()=>page.evaluate(()=>JSON.parse(FS.readFile('/data/workspace.json',{encoding:'utf8'})));
    assert.equal((await state()).drawing_tools_open,true);
    const frames=await page.evaluate(()=>Module.chartroomStats.frames);
    newer=true;
    await page.clock.fastForward(65000);
    await page.waitForFunction(()=>Module.chartroomAvailableBuild>Module.chartroomBuild);
    await page.waitForFunction(n=>Module.chartroomStats.frames>n,frames);
    assert(requests>=2,'The minute timer checks build metadata');
    assert.equal(await page.evaluate(()=>Module.chartroomBuild),running.number,'The header retains the running version until reload');
    await page.screenshot({path:require('node:path').join(require('node:os').tmpdir(),'chartroom-build-update.png')});
    // Version text is followed by Reload. Clicking must sync IndexedDB before navigation.
    const navigated=page.waitForEvent('framenavigated', f=>f===page.mainFrame());
    await page.mouse.click(89,42);
    await navigated;
    await page.waitForFunction(()=>typeof FS!=='undefined' && FS.analyzePath('/data/workspace.json').exists && Module.chartroomStats?.frames>=3);
    assert.equal((await state()).drawing_tools_open,true,'Reload preserves the saved workspace');
    assert.deepEqual(errors,[]);
    console.log('Build metadata matches the running version; minute update notice and save-before-reload passed');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
