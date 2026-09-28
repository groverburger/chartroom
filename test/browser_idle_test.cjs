// Deterministic data fixture: verify idle sleep, wakeups and minute polling without Yahoo availability.
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
(async () => {
  const browser = await chromium.launch({headless:true,
    ...(process.env.CHROMIUM_EXECUTABLE ? {executablePath:process.env.CHROMIUM_EXECUTABLE} : {}),
    args:['--enable-webgl','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
  try {
    const page = await browser.newPage({viewport:{width:1440,height:900},deviceScaleFactor:2});
    const errors=[];
    page.on('pageerror', e=>errors.push(e.message));
    let offline=false, requests=0;
    await page.route('**/chartroom.html',async route=>{
      const response=await route.fetch();
      let html=await response.text();
      if(offline) html=html.replace('var Module={','var Module={arguments:["--offline"],');
      await route.fulfill({response,body:html});
    });
 await page.route('**/nasdaq/**', route=>route.fulfill({status:503,body:'Provider unavailable in Yahoo regression fixture'}));
    await page.route('**/yahoo/**',async route=>{
      requests++;
      const url=new URL(route.request().url());
      const symbol=decodeURIComponent(url.pathname.split('/').pop());
      const interval=url.searchParams.get('interval');
      const start=1736121600, n=200;
      const values=Array.from({length:n},(_,i)=>100+i*.1);
      await route.fulfill({json:{chart:{error:null,result:[{
        meta:{symbol,currency:'USD',exchangeName:'TEST',dataGranularity:interval},
        timestamp:values.map((_,i)=>start+i*86400),
        indicators:{quote:[{open:values,close:values.map(x=>x+1),high:values.map(x=>x+3),low:values.map(x=>x-2),volume:values.map(()=>1000)}]}
      }]}}});
    });
    const url=process.env.CHARTROOM_URL || 'http://localhost:8765/chartroom.html';
    const stats=()=>page.evaluate(()=>Module.chartroomStats);
    await page.goto(url);
    await page.waitForFunction(()=>typeof FS!=='undefined' && FS.analyzePath('/data/cache/535059.1d.json').exists);
    await page.waitForTimeout(1700); // Let IndexedDB finish the initial cache save.
    offline=true;
    await page.reload();
    await page.waitForFunction(()=>Module.chartroomStats?.frames>=3);
    await page.evaluate(()=>{
      window.wakeTrace=[];
      const pump=Module._chartroom_pump;
      Module._chartroom_pump=input=>{const delay=pump(input);wakeTrace.push({time:performance.now(),delay,input,...Module.chartroomStats});return delay;};
    });
    await page.mouse.move(800,400);
    await page.waitForTimeout(6200); // Includes ImGui's delayed layout save.
    let before=await stats();
    await page.waitForTimeout(3000);
    let after=await stats();
    console.log('Idle browser over 3s:',{frames:after.frames-before.frames,wakes:after.wakes-before.wakes});
    if(after.frames!==before.frames) console.log(await page.evaluate(()=>wakeTrace));
    assert.equal(after.frames,before.frames,'No rendering while idle');
    assert.equal(after.wakes,before.wakes,'No animation-frame polling while idle');
    await page.mouse.move(850,420);
    await page.waitForTimeout(150);
    assert((await stats()).frames>after.frames,'Mouse movement wakes rendering');
    // Open a text field: cursor blinks at timer deadlines, not at display refresh rate.
    await page.mouse.click(90,165);
    await page.keyboard.type('QQQ');
    await page.waitForTimeout(400);
    before=await stats();
    await page.waitForTimeout(2500);
    after=await stats();
    console.log('Focused text over 2.5s:',{frames:after.frames-before.frames});
    assert(after.frames>before.frames && after.frames-before.frames<12,'Caret uses bounded timed frames');
    await page.keyboard.press('Escape');
    await page.mouse.click(820,430);
    await page.waitForTimeout(6200);
    before=await stats();
    await page.waitForTimeout(2000);
    assert.equal((await stats()).frames,before.frames,'Input settles back to idle');
    // Hidden pages can run maintenance, but must not issue render calls.
    await page.evaluate(()=>{
      Object.defineProperty(document,'hidden',{configurable:true,get:()=>true});
      document.dispatchEvent(new Event('visibilitychange'));
    });
    await page.waitForTimeout(200);
    before=await stats();
    await page.evaluate(()=>Module.chartroomRequest(true));
    await page.waitForTimeout(300);
    assert.equal((await stats()).frames,before.frames,'Hidden tab does not render');
    await page.evaluate(()=>{delete document.hidden;document.dispatchEvent(new Event('visibilitychange'));});
    await page.waitForTimeout(200);
    assert((await stats()).frames>before.frames,'Showing tab redraws');
    // A fresh online page verifies the actual refresh timer under a virtual clock.
    const live=await browser.newPage({viewport:{width:1440,height:900}});
    let spyHistory=0;
    await live.route('**/nasdaq/**', route=>route.fulfill({status:503,body:'Provider unavailable in Yahoo regression fixture'}));
    await live.route('**/yahoo/**',async route=>{
      const u=new URL(route.request().url());
      const interval=u.searchParams.get('interval'), symbol=decodeURIComponent(u.pathname.split('/').pop());
      if(symbol==='SPY' && interval==='1d') spyHistory++;
      const closes=[100,101,102+spyHistory];
      await route.fulfill({json:{chart:{error:null,result:[{meta:{symbol,dataGranularity:interval},
        timestamp:[1736121600,1736208000,1736294400],
        indicators:{quote:[{open:[99,100,101],close:closes,high:[110,110,110+spyHistory],low:[98,99,100],volume:[1000,1000,1000]}]}}]}}});
    });
    await live.clock.install();
    await live.goto(url);
    await live.waitForFunction(()=>typeof FS!=='undefined' && FS.analyzePath('/data/cache/535059.1d.json').exists);
    const initial=spyHistory;
    const initialFetched=await live.evaluate(()=>JSON.parse(FS.readFile('/data/cache/535059.1d.json',{encoding:'utf8'})).fetched_at);
    const initialFrames=await live.evaluate(()=>Module.chartroomStats.frames);
    await live.clock.fastForward(65000);
    await live.waitForFunction(()=>Module.chartroomStats.frames>3);
    await live.waitForTimeout(300);
    assert(spyHistory>initial,'Minute timer fetches history without user input');
    const refreshed=await live.evaluate(()=>JSON.parse(FS.readFile('/data/cache/535059.1d.json',{encoding:'utf8'})).fetched_at);
    assert(Date.parse(refreshed+'Z')-Date.parse(initialFetched+'Z')>=60000,'History cache receives a new scheduled refresh');
    assert((await live.evaluate(()=>Module.chartroomStats.frames))>initialFrames,'Response wakes rendering');
    console.log('Minute refresh:',{initial,after:spyHistory});
    assert.deepEqual(errors,[]);
    console.log('Idle rendering, input wakeups, caret timers, visibility and minute polling passed');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
