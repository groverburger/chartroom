// Independent watchlist windows, editable favorites, lazy history and new studies.
const {chromium}=require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_EXECUTABLE?{executablePath:process.env.CHROMIUM_EXECUTABLE}:{}),args:['--enable-webgl','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 try {
  const page=await browser.newPage({viewport:{width:1440,height:900}});
  const errors=[]; page.on('pageerror',e=>errors.push(e.message));
  const histories=new Set(); let releaseHistory;
  const historyGate=new Promise(resolve=>releaseHistory=resolve);
  await page.route('**/nasdaq/**',route=>route.fulfill({status:503,body:'Fixture uses Yahoo'}));
  await page.route('**/yahoo/**',async route=>{
   const u=new URL(route.request().url()), symbol=decodeURIComponent(u.pathname.split('/').pop());
   const interval=u.searchParams.get('interval'), n=300;
   if(interval!=='1m') {histories.add(symbol); await historyGate;}
   const prices=Array.from({length:n},(_,i)=>100+i*.2+Math.sin(i*.2)*5);
   await route.fulfill({json:{chart:{error:null,result:[{meta:{symbol,currency:'USD',regularMarketPrice:prices.at(-1),previousClose:prices.at(-2),regularMarketTime:Math.floor(Date.now()/1000),dataGranularity:interval},timestamp:prices.map((_,i)=>1736121600+i*86400),indicators:{quote:[{open:prices.map(x=>x-1),high:prices.map(x=>x+2),low:prices.map(x=>x-3),close:prices,volume:prices.map(()=>1000)}]}}]}}});
  });
  const state=()=>page.evaluate(()=>JSON.parse(FS.readFile('/data/workspace.json',{encoding:'utf8'})));
  const settle=()=>page.waitForTimeout(1200);
  await page.goto(process.env.CHARTROOM_URL || 'http://localhost:8765/chartroom.html');
  await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/workspace.json').exists);
  await settle();
  await page.screenshot({path:'/tmp/chartroom-loading.png'});
  releaseHistory();
  await page.waitForFunction(()=>FS.analyzePath('/data/cache/535059.1d.json').exists);
  await settle();
  let doc=await state();
  assert.equal(doc.watchlist_windows.length,2);
  assert(doc.lists.length>=20);
  assert.equal(doc.lists[doc.watchlist_windows[0].list].name,'At a glance');
  assert.equal(doc.lists[doc.watchlist_windows[1].list].name,'Big names');
  assert.deepEqual([...histories],['SPY'],'Starter lists must not prefetch chart histories');
  // Editing favorites uses the same add-to-list control as any other list.
  await page.mouse.click(85,165);await page.keyboard.type('TLT');await page.keyboard.press('Enter');await settle();
  doc=await state();assert(doc.lists[doc.watchlist_windows[0].list].symbols.includes('TLT'));
  assert.equal(doc.charts[0].symbol,'TLT');
  // Each window can switch its own selection independently.
  await page.mouse.click(110,132);await page.waitForTimeout(200);
  await page.keyboard.press('Home'); await page.keyboard.press('ArrowDown');await page.keyboard.press('ArrowDown');await page.keyboard.press('ArrowDown');await page.keyboard.press('Enter');await settle();
  doc=await state();assert.equal(doc.lists[doc.watchlist_windows[0].list].name,'Crypto');
  assert.equal(doc.lists[doc.watchlist_windows[1].list].name,'Big names');
  // Open another window from the ordinary Add panel menu.
  await page.mouse.click(290,12);await page.mouse.move(310,110);await page.waitForTimeout(400);
  await page.screenshot({path:'/tmp/chartroom-watchlist-menu.png'});
  await page.mouse.click(430,203);await settle();
  doc=await state();assert.equal(doc.watchlist_windows.length,3);
  await page.reload();await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/workspace.json').exists);await settle();
  const restored=await state();assert.deepEqual(restored.watchlist_windows,doc.watchlist_windows);
  assert.deepEqual(restored.lists,doc.lists);
  // Close the new floating window before using the chart's indicator menu.
  await page.evaluate(()=>{
   const p='/data/workspace.json',j=JSON.parse(FS.readFile(p,{encoding:'utf8'}));
   j.watchlist_windows=j.watchlist_windows.slice(0,2);FS.writeFile(p,JSON.stringify(j));
   return new Promise((resolve,reject)=>FS.syncfs(false,e=>e?reject(e):resolve()));
  });
  await page.reload();await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/workspace.json').exists);await settle();
  await page.mouse.click(300,102);await page.waitForTimeout(200);
  await page.screenshot({path:'/tmp/chartroom-indicator-menu.png'});
  for (const [query,kind] of [['swing','PIVOTS'],['donchian','DONCHIAN'],['keltner','KC'],['stochastic','STOCH'],['rate of change','ROC'],['volume-weighted','VWMA'],['on-balance','OBV']]) {
   await page.mouse.click(450,145);await page.waitForTimeout(100);await page.keyboard.press('Home');await page.keyboard.press('Shift+End');await page.keyboard.type(query);await page.waitForTimeout(200);
   await page.mouse.click(450,181);await settle();
   const added=(await state()).charts[0].indicators.some(i=>i.kind===kind);
   if(!added) await page.screenshot({path:'/tmp/chartroom-picker-failure.png'});
   assert(added,`Add ${kind} through the picker`);
  }
  await page.keyboard.press('Escape');await page.mouse.move(850,400);await settle();
  await page.screenshot({path:'/tmp/chartroom-new-indicators.png'});
  const studies=(await state()).charts[0].indicators;
  await page.reload();await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/workspace.json').exists);await settle();
  assert.deepEqual((await state()).charts[0].indicators,studies);
  assert.deepEqual(errors,[]);
  console.log('Starter lists, editable favorites, independent windows, menu opening, new indicator picker, persistence and lazy history passed');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
