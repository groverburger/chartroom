// Session quotes stay separate from candles, including refresh/restart and Yahoo fallback.
const {chromium}=require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_EXECUTABLE?{executablePath:process.env.CHROMIUM_EXECUTABLE}:{}),args:['--enable-webgl','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 try {
  const page=await browser.newPage({viewport:{width:1440,height:900}});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  const clock=Date.parse('2026-09-25T09:12:00Z')/1000, regular=Date.parse('2026-09-24T20:00:00Z')/1000;
  await page.clock.install({time:new Date(clock*1000)});
  let fallback=false;
  const pre=JSON.parse(fs.readFileSync(path.join(__dirname,'fixtures/nasdaq-info-premarket.json')));
  await page.route('**/nasdaq/**',route=>{
   const u=new URL(route.request().url());
   if(u.pathname.endsWith('/info')&&!fallback) {
    const j=structuredClone(pre);j.data.symbol=u.pathname.split('/').at(-2);
    return route.fulfill({json:j});
   }
   return route.fulfill({status:503,body:'Unavailable'});
  });
  const times=[];
  for(let t=Date.parse('2025-01-02T13:30:00Z')/1000;t<=Date.parse('2026-09-24T13:30:00Z')/1000;t+=86400){
   const day=new Date(t*1000).getUTCDay();if(day!==0&&day!==6)times.push(t);
  }
  const prices=times.map((_,i)=>680+Math.sin(i*.12)*12+i*.2);prices[prices.length-1]=767.18;prices[prices.length-2]=767.81;
  await page.route('**/yahoo/**',route=>{
   const u=new URL(route.request().url()), symbol=decodeURIComponent(u.pathname.split('/').pop()), minute=u.searchParams.get('interval')==='1m';
   const meta={symbol,regularMarketPrice:767.18,regularMarketTime:regular,previousClose:767.81,regularMarketChangePercent:-.08,hasPrePostMarketData:true,
    currentTradingPeriod:{pre:{start:clock-4320,end:clock+15480},post:{start:regular,end:regular+14400}}};
   if(minute){meta.regularMarketTime=regular-60;assert.equal(u.searchParams.get('includePrePost'),'true');return route.fulfill({json:{chart:{error:null,result:[{meta,timestamp:[clock],indicators:{quote:[{close:[771.25]}]}}]}}});}
   assert.equal(u.searchParams.get('includePrePost'),'false');
   return route.fulfill({json:{chart:{error:null,result:[{meta,timestamp:times,indicators:{quote:[{open:prices.map(x=>x-2),high:prices.map(x=>x+4),low:prices.map(x=>x-4),close:prices,volume:prices.map(()=>100000)}]}}]}}});
  });
  const read=p=>page.evaluate(p=>JSON.parse(FS.readFile(p,{encoding:'utf8'})),p);
  const quote=()=>read('/data/cache/535059.quote.json');
  const state=()=>read('/data/workspace.json');
  const waitPre=()=>page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/cache/535059.quote.json').exists&&JSON.parse(FS.readFile('/data/cache/535059.quote.json',{encoding:'utf8'})).extended?.session==='Pre');
  await page.goto(process.env.CHARTROOM_URL||'http://localhost:8765/chartroom.html');await waitPre();await page.waitForTimeout(1200);
  assert.equal((await quote()).price,767.18);assert.equal((await quote()).change,-.08);
  assert.equal((await quote()).extended.price,770.0705);
  // History refresh with the same regular timestamp must preserve the separate live quote.
  await page.mouse.click(425,102);await page.waitForTimeout(1500);assert.equal((await quote()).extended.price,770.0705);
  const bars=(await read('/data/cache/535059.1d.json')).bars;
  assert.equal(bars.at(-1)[4],767.18);
  // Pan until half the window is future, then inspect calendar boundaries and live tag.
  const before=await state();
  await page.mouse.move(900,400);await page.waitForTimeout(150);await page.mouse.down();await page.waitForTimeout(150);await page.mouse.move(400,400,{steps:20});await page.waitForTimeout(150);await page.mouse.up();
  await page.mouse.move(30,127);await page.waitForTimeout(1500);
  const panned=await state();await page.screenshot({path:path.join(os.tmpdir(),'chartroom-calendar-premarket.png')});assert(panned.charts[0].view.first>before.charts[0].view.first);
  await page.screenshot({path:path.join(os.tmpdir(),'chartroom-calendar-premarket.png')});
  // Zoomed weekly ticks and a future-only view should remain readable.
  await page.mouse.move(850,400);await page.mouse.wheel(0,-1200);await page.waitForTimeout(800);
  await page.mouse.move(30,600);await page.screenshot({path:path.join(os.tmpdir(),'chartroom-calendar-weeks.png')});
  await page.reload();await waitPre();await page.waitForTimeout(1200);
  assert.equal((await quote()).change,-.08);assert.equal((await quote()).extended.price,770.0705);
  fallback=true;await page.clock.fastForward(65000);await page.waitForFunction(()=>JSON.parse(FS.readFile('/data/cache/535059.quote.json',{encoding:'utf8'})).extended?.price===771.25);
  assert.equal((await quote()).change,-.08);assert.equal((await read('/data/cache/535059.1d.json')).bars.at(-1)[4],767.18);
  await page.mouse.move(20,127);await page.waitForTimeout(100);await page.screenshot({path:path.join(os.tmpdir(),'chartroom-yahoo-premarket.png')});
  assert.deepEqual(errors,[]);
  console.log('Regular-session percentages, separate premarket overlay, unchanged candles, quote merge/restart, Yahoo extended fallback and calendar views passed');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
