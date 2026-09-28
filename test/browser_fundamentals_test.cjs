const {chromium}=require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const fixture=name=>JSON.parse(fs.readFileSync(path.join(__dirname,'fixtures',`nasdaq-${name}.json`),'utf8'));
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_EXECUTABLE?{executablePath:process.env.CHROMIUM_EXECUTABLE}:{}),args:['--enable-webgl','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 try {
  const page=await browser.newPage({viewport:{width:1440,height:900},timezoneId:'America/Los_Angeles'});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  let phase=1;const calls=[];
  await page.route('**/nasdaq/**',route=>{
   const url=new URL(route.request().url()); const endpoint=url.pathname.split('/').at(-1);
   const names={'summary':'summary','company-profile':'profile','earnings-surprise':'earnings','earnings-date':'earnings-date'};
   if(!url.pathname.includes('/AAPL/')||!names[endpoint])return route.fulfill({status:503,body:'Use Yahoo fixture'});
   calls.push(endpoint);
   const data=fixture(names[endpoint]);
   if(endpoint==='earnings-surprise')data.data.earningsSurpriseTable.rows.splice(phase===1?3:2,1);
   return route.fulfill({json:data});
  });
  await page.route('**/yahoo/**',route=>{
   const url=new URL(route.request().url()),symbol=decodeURIComponent(url.pathname.split('/').at(-1));
   const interval=url.searchParams.get('interval');
   const timestamps=[];for(let t=Date.parse('2025-08-01T13:30:00Z');t<=Date.parse('2026-09-24T13:30:00Z');t+=86400000){const d=new Date(t).getUTCDay();if(d!==0&&d!==6)timestamps.push(t/1000);}
   const close=timestamps.map((_,i)=>200+i*.2+Math.sin(i*.1)*12);
   return route.fulfill({json:{chart:{error:null,result:[{meta:{symbol,currency:'USD',dataGranularity:interval,regularMarketPrice:close.at(-1),previousClose:close.at(-2),regularMarketTime:timestamps.at(-1)},timestamp:timestamps,indicators:{quote:[{open:close.map(x=>x-1),high:close.map(x=>x+2),low:close.map(x=>x-3),close,volume:close.map(()=>1000)}]}}]}}});
  });
  const settle=()=>page.waitForTimeout(1500);
  const state=()=>page.evaluate(()=>JSON.parse(FS.readFile('/data/workspace.json',{encoding:'utf8'})));
  const fund=()=>page.evaluate(()=>JSON.parse(FS.readFile('/data/cache/4141504c.fundamentals.json',{encoding:'utf8'})));
  await page.goto(process.env.CHARTROOM_URL||'http://localhost:8767/chartroom.html');
  await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/workspace.json').exists);await settle();
  // Seed an ordinary saved chart, then exercise the same restoration path as a restart.
  await page.evaluate(async()=>{
   const p='/data/workspace.json',d=JSON.parse(FS.readFile(p,{encoding:'utf8'}));
   d.charts[0].symbol='AAPL';d.charts[0].volume=false;d.charts[0].indicators=[];
   d.charts[0].view={first:0,count:400,follow:true};d.charts[0].eps=true;d.charts[0].earnings=true;
   d.fundamentals={open:false,follow:true,symbol:'AAPL'};
   FS.writeFile(p,JSON.stringify(d));await new Promise((resolve,reject)=>FS.syncfs(false,e=>e?reject(e):resolve()));
  });
  await page.reload();await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/cache/4141504c.fundamentals.json').exists);await settle();
  assert.equal((await fund()).parts.earnings.data.earningsSurpriseTable.rows.length,3);
  assert.equal(calls.length,4);
  await page.mouse.move(1000,400);await settle();await page.screenshot({path:'/tmp/chartroom-earnings-eps.png'});
  // Force a new snapshot after restart; a report dropped by the rolling feed survives locally.
  phase=2;
  await page.evaluate(async()=>{const p='/data/cache/4141504c.fundamentals.json',d=JSON.parse(FS.readFile(p,{encoding:'utf8'}));d.fetched=0;FS.writeFile(p,JSON.stringify(d));await new Promise(r=>FS.syncfs(false,r));});
  await page.reload();await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/cache/4141504c.fundamentals.json').exists);await settle();
  assert.equal((await fund()).parts.earnings.data.earningsSurpriseTable.rows.length,4);
  assert.equal(calls.length,8);
  await page.mouse.move(623,823);await settle();
  await page.screenshot({path:"/tmp/chartroom-earnings-tooltip.png"});
  await page.mouse.click(623,823);await settle();
  await page.screenshot({path:'/tmp/chartroom-fundamentals.png'});
  assert((await state()).fundamentals.open,'Earnings marker opens company fundamentals');
  // The same window is reachable from Facts; EPS display choices persist per chart.
  await page.mouse.click(574,72);await settle();
  assert(!(await state()).fundamentals.open);
  await page.mouse.click(533,102);await settle();
  assert((await state()).fundamentals.open);
  await page.mouse.click(80,550);await settle();
  assert((await state()).charts[0].eps_ttm);
  const before=calls.length;
  await page.reload();await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/workspace.json').exists);await settle();
  assert.equal(calls.length,before,'Fresh cached fundamentals should not refetch on restart');
  const d=await state();assert(d.charts[0].eps&&d.charts[0].eps_ttm&&d.charts[0].earnings&&d.fundamentals.open);
  assert.deepEqual(errors,[]);
  console.log('Fundamentals, EPS, rolling report cache, lazy refresh and persistence passed');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
