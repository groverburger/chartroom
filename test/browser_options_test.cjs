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
  const chainCalls=()=>optionCalls.filter(c=>c.from&&c.from===c.to);
  const initialCalls=chainCalls().length;
  await page.mouse.click(532,242); // Puts toggle.
  await page.waitForTimeout(1200);
  assert.equal((await state()).options.puts,true);
  assert.equal(chainCalls().length,initialCalls,'Changing side reuses the same expiry snapshot');
  await page.screenshot({path:'/private/tmp/chartroom-options-puts.png'});
  await page.mouse.click(298,242); // Single expiry picker.
  await page.waitForFunction(()=>JSON.parse(FS.readFile('/data/cache/535059.option-dates.json',{encoding:'utf8'})).complete);
  await page.keyboard.press('ArrowDown');await page.keyboard.press('Enter');
  await exists('/data/cache/535059.options-2026-10-16.json');
  await page.waitForTimeout(1200);
  assert.equal((await state()).options.expiry,'2026-10-16');
  assert.equal(chainCalls().length,initialCalls+2,'Only the selected expiry is paginated');
  const beforeBack=optionCalls.length;
  await page.mouse.click(298,242);await page.keyboard.press('Home');await page.keyboard.press('Enter');
  await page.waitForTimeout(1200);
  assert.equal((await state()).options.expiry,'2026-09-23');
  assert.equal(optionCalls.length,beforeBack,'Returning to a fresh cached expiry makes no request');
  await page.reload();await exists('/data/workspace.json');await page.waitForTimeout(1000);
  assert.equal((await state()).options.puts,true);
  assert.equal((await state()).options.expiry,'2026-09-23');
  assert.equal(optionCalls.length,beforeBack,'Fresh chains and date catalog survive reload');
  assert.deepEqual(errors,[]);
  console.log('Lazy single-expiry requests, calls/puts toggle without fetching, expiry switching, cache reuse and persistence passed');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
