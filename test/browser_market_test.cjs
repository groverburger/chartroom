// Provider routing, paginated options, screener POST, workspace/cache persistence, and idle timers.
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const fixture=name=>JSON.parse(fs.readFileSync(path.join(__dirname,'fixtures',name),'utf8'));
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_EXECUTABLE?{executablePath:process.env.CHROMIUM_EXECUTABLE}:{}),args:['--enable-webgl','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 try {
  const page=await browser.newPage({viewport:{width:1440,height:900}});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  let scans=[], optionCalls=[], fail=false;
  await page.clock.install({time:new Date('2026-09-23T18:10:00Z')});
  await page.route('**/nasdaq/**',async route=>{
   const u=new URL(route.request().url()), parts=u.pathname.split('/'), endpoint=parts.at(-1), symbol=parts.at(-2);
   if(endpoint==='info') {const j=fixture('nasdaq-info.json');j.data.symbol=symbol;return route.fulfill({json:j});}
   if(endpoint==='chart') {const j=fixture('nasdaq-chart.json');j.data.symbol=symbol;return route.fulfill({json:j});}
   if(endpoint==='option-chain') {
    optionCalls.push({symbol,offset:Number(u.searchParams.get('offset')),from:u.searchParams.get('fromdate'),to:u.searchParams.get('todate')});
    if(fail) return route.fulfill({status:503,body:'Temporarily unavailable'});
    if(u.searchParams.get('limit')==='1') return route.fulfill({json:fixture('nasdaq-expiries.json')});
    const j=fixture('nasdaq-options.json');
    if(u.searchParams.get('money')==='at') {
     j.data.totalRecord=6;
     j.data.table.rows=u.searchParams.get('offset')==='0'?j.data.table.rows.slice(0,4):j.data.table.rows.slice(4);
    } else {
     assert.equal(u.searchParams.get('fromdate'),u.searchParams.get('todate'));
     const expiry=u.searchParams.get('fromdate'),stamp=expiry.replaceAll('-','').slice(2);
     const rows=[{expirygroup:'September 23, 2026',strike:null},...[750,760,770,780,790].map(strike=>({
      ...j.data.table.rows[1],expirygroup:'',strike:String(strike),drillDownURL:'/spy---'+stamp+'c'+String(strike*1000).padStart(8,'0'),
      c_Last:String(Math.max(1,768.4-strike)),p_Last:String(Math.max(1,strike-768.4)),c_Bid:'1.20',c_Ask:'1.25',p_Bid:'2.30',p_Ask:'2.35'}))];
     j.data.totalRecord=6;j.data.table.rows=u.searchParams.get('offset')==='0'?rows.slice(0,3):rows.slice(3);
    }
    return route.fulfill({json:j});
   }
   return route.fulfill({status:404});
  });
  await page.route('**/yahoo/**',route=>{
   const u=new URL(route.request().url()),s=decodeURIComponent(u.pathname.split('/').pop()),interval=u.searchParams.get('interval');
   const price=s==='^VIX'?20:768;
   return route.fulfill({json:{chart:{error:null,result:[{meta:{symbol:s,currency:'USD',regularMarketPrice:price,regularMarketTime:1790187000,previousClose:773.38},timestamp:[1790170200],indicators:{quote:[{open:[price+1],high:[price+2],low:[price-1],close:[price],volume:[100000]}]}}]}}});
  });
  await page.route('**/scanner/**',route=>{
   assert.equal(route.request().method(),'POST');
   const body=route.request().postDataJSON();scans.push(body);
   if(fail) return route.fulfill({status:503,body:'Temporarily unavailable'});
   return route.fulfill({json:fixture('scanner.json')});
  });
  const file=p=>page.evaluate(p=>JSON.parse(FS.readFile(p,{encoding:'utf8'})),p);
  const exists=p=>page.waitForFunction(p=>typeof FS!=='undefined'&&FS.analyzePath(p).exists,p);
  const state=()=>file('/data/workspace.json');
  await page.goto(process.env.CHARTROOM_URL || 'http://localhost:8765/chartroom.html');
  await exists('/data/cache/535059.1d.json');
  const h=await file('/data/cache/535059.1d.json');
  assert.equal(h.meta.chartroomSource,'Nasdaq + Yahoo latest session');
  assert.equal(h.bars.length,5);assert.equal(h.bars.at(-1)[4],768);
  await page.waitForTimeout(600);
  // Market -> Options, without disturbing the chart layout.
  await page.mouse.click(155,70);await page.waitForTimeout(100);
  await page.screenshot({path:'/private/tmp/chartroom-market-menu.png'});
  await page.mouse.click(176,96);
  await page.waitForTimeout(500);
  await page.screenshot({path:'/private/tmp/chartroom-market-click.png'});
  await exists('/data/cache/535059.options-2026-09-23.json');
  let options=await file('/data/cache/535059.options-2026-09-23.json');
  assert.equal(options.data.table.rows.length,5);
  assert.deepEqual(optionCalls.map(c=>c.offset),[0,0,3]);
  assert(optionCalls.slice(1).every(c=>c.from==='2026-09-23'&&c.to==='2026-09-23'));
  await page.waitForTimeout(1200);
  assert.equal((await state()).options.open,true);
  await page.screenshot({path:'/private/tmp/chartroom-options.png'});
  // Save both windows as open; reload exercises real restore and background scheduling.
  await page.evaluate(()=>{
   const p='/data/workspace.json',j=JSON.parse(FS.readFile(p,{encoding:'utf8'}));
   j.screener.open=true;j.screener.sort=2;j.screener.min_price=5;j.screener.min_cap=1;
   FS.writeFile(p,JSON.stringify(j));
  });
  await page.evaluate(()=>new Promise((resolve,reject)=>FS.syncfs(false,e=>e?reject(e):resolve())));
  await page.reload();
  await exists('/data/cache/screener.json');
  await page.waitForTimeout(700);
  assert.equal(scans.at(-1).sort.sortOrder,'asc');
  assert(scans.at(-1).filter.some(f=>f.left==='close'&&f.right===5));
  await page.screenshot({path:'/private/tmp/chartroom-screener.png'});
  await page.mouse.click(228,329); // First screener row -> current chart.
  await page.waitForTimeout(1200);
  assert.equal((await state()).charts[0].symbol,'NVDA');
  await page.keyboard.down('Control');await page.mouse.click(225,351);await page.keyboard.up('Control');
  await page.waitForTimeout(1200);
  assert.equal((await state()).charts.length,2);
  assert.equal((await state()).charts[1].symbol,'AAPL');
  await page.mouse.click(532,233); // Next results page.
  await page.waitForTimeout(300);
  assert.deepEqual(scans.at(-1).range,[100,200]);
  const screen=await file('/data/cache/screener.json');
  assert.equal(screen.totalCount,11095);
  const optionsFetched=(await file('/data/cache/535059.options-2026-09-23.json')).fetched;
  fail=true;
  const before=[optionCalls.length,scans.length];
  await page.clock.fastForward(65000);await page.waitForTimeout(400);
  assert(optionCalls.length>before[0]&&scans.length>before[1],'Open windows refresh without input');
  assert.equal((await file('/data/cache/535059.options-2026-09-23.json')).fetched,optionsFetched,'Failed refresh retains the snapshot');
  assert.equal((await file('/data/cache/screener.json')).fetched,screen.fetched);
  // Close each floating window. Once closed, neither endpoint should keep polling.
  await page.mouse.click(1229,138);await page.waitForTimeout(200);
  await page.mouse.click(1204,178);await page.waitForTimeout(1200);
  assert.equal((await state()).screener.open,false);
  assert.equal((await state()).options.open,false);
  const closed=[optionCalls.length,scans.length];
  await page.clock.fastForward(185000);await page.waitForTimeout(200);
  assert.deepEqual([optionCalls.length,scans.length],closed);
  assert.deepEqual(errors,[]);
  console.log('Nasdaq + latest-session history, options pagination, scanner POST/filters, persistence and failed-refresh retention passed');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
