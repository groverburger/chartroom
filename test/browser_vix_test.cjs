// Reproduce the Yahoo VIX placeholder and an older cached zero wick in a fresh browser context.
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path');
const daily = JSON.parse(fs.readFileSync(path.join(__dirname,'fixtures/vix-zero-1d.json')));
const hourly = JSON.parse(fs.readFileSync(path.join(__dirname,'fixtures/vix-zero-1h.json')));
(async () => {
  const browser = await chromium.launch({headless:true,
    ...(process.env.CHROMIUM_EXECUTABLE ? {executablePath:process.env.CHROMIUM_EXECUTABLE} : {}),
    args:['--enable-webgl','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
  try {
    const page = await browser.newPage({viewport:{width:1440,height:900}});
    const errors=[]; page.on('pageerror',e=>errors.push(e.message));
    let offline=true, hourlyRequests=0;
    await page.clock.install({time:new Date('2026-09-22T20:16:01Z')});
    await page.route('**/chartroom.html',async route=>{
      const response=await route.fetch();
      let html=await response.text();
      if(offline) html=html.replace('var Module={','var Module={arguments:["--offline"],');
      await route.fulfill({response,body:html});
    });
 await page.route('**/nasdaq/**', route=>route.fulfill({status:503,body:'Provider unavailable in Yahoo regression fixture'}));
    await page.route('**/yahoo/**',async route=>{
      const url=new URL(route.request().url());
      const symbol=decodeURIComponent(url.pathname.split('/').pop());
      const interval=url.searchParams.get('interval');
      if(symbol==='^VIX' && interval==='1h') { hourlyRequests++; return route.fulfill({json:hourly}); }
      if(symbol==='^VIX' && interval==='1d') return route.fulfill({json:daily});
      await route.fulfill({status:503,body:'Other symbols are outside this fixture'});
    });
    await page.goto(process.env.CHARTROOM_URL || 'http://localhost:8765/chartroom.html');
    await page.waitForFunction(()=>typeof FS!=='undefined' && FS.analyzePath('/data/workspace.json').exists);
    const r=daily.chart.result[0], q=r.indicators.quote[0];
    const bars=r.timestamp.map((t,i)=>[t,...['open','high','low','close','volume'].map(k=>q[k][i])]);
    bars[bars.length-1]=[1790060400,0,14.21,0,14.21,0];
    const legacy={version:1,symbol:'^VIX',interval:'1d',currency:'USD',exchange:'WCB',
      fetched_at:'2026-09-22T20:16:00',skipped:0,meta:{...r.meta,chartroomRecoveredTimes:[1790060400]},bars};
    await page.evaluate(async cache=>{
      const doc=JSON.parse(FS.readFile('/data/workspace.json',{encoding:'utf8'}));
      doc.charts[0].symbol='^VIX';
      doc.charts[0].view={first:0,count:3,length:3,anchor:cache.bars[0][0],follow:true};
      FS.writeFile('/data/workspace.json',JSON.stringify(doc));
      FS.mkdirTree('/data/cache');
      FS.writeFile('/data/cache/5e564958.1d.json',JSON.stringify(cache));
      await new Promise((resolve,reject)=>FS.syncfs(false,e=>e?reject(e):resolve()));
    },legacy);
    await page.reload();
    await page.waitForFunction(()=>typeof FS!=='undefined' && FS.analyzePath('/data/workspace.json').exists && JSON.parse(FS.readFile('/data/workspace.json',{encoding:'utf8'})).charts[0].view.length===2);
    assert.equal(hourlyRequests,0,'Offline cache cleanup does not need a network request');
    offline=false;
    await page.reload();
    await page.waitForFunction(()=>typeof FS!=='undefined' && FS.analyzePath('/data/cache/5e564958.1d.json').exists && JSON.parse(FS.readFile('/data/cache/5e564958.1d.json',{encoding:'utf8'})).meta.chartroomHourlyRecoveredTimes?.length===1);
    const cache=await page.evaluate(()=>JSON.parse(FS.readFile('/data/cache/5e564958.1d.json',{encoding:'utf8'})));
    const last=cache.bars.at(-1);
    assert.equal(last[0],1790060400);
    assert(Math.abs(last[1]-14.64)<1e-5 && Math.abs(last[2]-14.95)<1e-5);
    assert(Math.abs(last[3]-14.19)<1e-5 && Math.abs(last[4]-14.21)<1e-5);
    assert(cache.bars.every(b=>b.slice(1,5).every(x=>x>0)));
    assert(hourlyRequests>0,'Daily placeholder triggers hourly recovery');
    await page.waitForTimeout(1400);
    await page.screenshot({path:path.join(require('node:os').tmpdir(),'chartroom-vix-recovered.png')});
    offline=true;
    await page.reload();
    await page.waitForFunction(()=>typeof FS!=='undefined' && FS.analyzePath('/data/workspace.json').exists && JSON.parse(FS.readFile('/data/workspace.json',{encoding:'utf8'})).charts[0].view.length===3);
    const restored=await page.evaluate(()=>JSON.parse(FS.readFile('/data/cache/5e564958.1d.json',{encoding:'utf8'})));
    assert.deepEqual(restored.bars,cache.bars,'Recovered candle survives offline restart');
    assert.deepEqual(errors,[]);
    console.log('Browser VIX legacy cache cleanup, hourly repair, positive OHLC and offline restart passed');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
