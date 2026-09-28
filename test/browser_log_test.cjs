// A geometric price series should appear evenly spaced on a logarithmic price axis.
const {chromium}=require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert=require('node:assert/strict');
const path=require('node:path'), os=require('node:os');
(async()=>{
 const browser=await chromium.launch({headless:true,
  ...(process.env.CHROMIUM_EXECUTABLE?{executablePath:process.env.CHROMIUM_EXECUTABLE}:{}),
  args:['--enable-webgl','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 try {
  const page=await browser.newPage({viewport:{width:1440,height:900}});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.route('**/nasdaq/**', route=>route.fulfill({status:503,body:'Provider unavailable in Yahoo regression fixture'}));
 await page.route('**/yahoo/**',async route=>{
   const url=new URL(route.request().url()), symbol=decodeURIComponent(url.pathname.split('/').pop());
   const interval=url.searchParams.get('interval'), start=1736121600;
   const values=Array.from({length:200},(_,i)=>10*Math.pow(1000,i/199));
   await route.fulfill({json:{chart:{error:null,result:[{
    meta:{symbol,dataGranularity:interval,regularMarketPrice:10000,regularMarketTime:start+199*86400,previousClose:values[198]},
    timestamp:values.map((_,i)=>start+i*86400),
    indicators:{quote:[{open:values.map(v=>v*.99),high:values.map(v=>v*1.02),low:values.map(v=>v*.98),close:values,volume:values.map(()=>1000)}]}
   }]}}});
  });
  await page.goto(process.env.CHARTROOM_URL || 'http://localhost:8765/chartroom.html');
  await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/cache/535059.1d.json').exists&&FS.analyzePath('/data/workspace.json').exists);
  const state=()=>page.evaluate(()=>JSON.parse(FS.readFile('/data/workspace.json',{encoding:'utf8'})));
  await page.mouse.click(367,102);await page.waitForTimeout(150);
  await page.mouse.click(500,154);await page.waitForTimeout(150);
  await page.keyboard.press('End');await page.keyboard.press('Enter');
  await page.waitForTimeout(150);await page.keyboard.press('Escape');
  await page.waitForTimeout(1200);
  assert.equal((await state()).charts[0].logarithmic,true);
  await page.mouse.move(30,450);await page.waitForTimeout(150);
  await page.screenshot({path:path.join(os.tmpdir(),'chartroom-log-scale.png')});
  const view=(await state()).charts[0].view;
  await page.mouse.move(800,400);await page.mouse.wheel(0,-160);
  await page.waitForTimeout(1200);
  const zoomed=(await state()).charts[0];
  assert(zoomed.view.count<view.count);
  assert.equal(zoomed.logarithmic,true);
  await page.mouse.wheel(100,0);await page.waitForTimeout(1200);
  assert.equal((await state()).charts[0].logarithmic,true);
  await page.reload();
  await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/workspace.json').exists&&Module.chartroomStats?.frames>=3);
  assert.equal((await state()).charts[0].logarithmic,true);
  assert.deepEqual(errors,[]);
  console.log('Browser logarithmic menu, geometric price range, navigation and restart passed');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1)});
