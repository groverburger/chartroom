// Daily rows omit yesterday's OHLC. Quotes must still update from intraday metadata.
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
(async () => {
  const browser=await chromium.launch({headless:true,
    ...(process.env.CHROMIUM_EXECUTABLE?{executablePath:process.env.CHROMIUM_EXECUTABLE}:{}),
    args:['--enable-webgl','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
  try {
    const page=await browser.newPage({viewport:{width:1440,height:900}});
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    const clock=Date.parse('2026-09-23T16:30:00Z')/1000, today=Date.parse('2026-09-23T13:30:00Z')/1000;
    let phase=0, hold=false, held, chartQuote=false, requests=0;
    const base=s=>s==='AAPL'?200:100;
    const price=s=>base(s)+5*(phase+1);
    const metadata=(s,p=phase)=>({symbol:s,regularMarketPrice:base(s)+5*(p+1),
      regularMarketTime:clock+p*60,previousClose:base(s),chartPreviousClose:50});
    const snapshot=(s,p=phase)=>({chart:{error:null,result:[{meta:metadata(s,p),timestamp:null,indicators:{quote:[{}]}}]}});
    await page.clock.install({time:new Date(clock*1000)});
 await page.route('**/nasdaq/**', route=>route.fulfill({status:503,body:'Provider unavailable in Yahoo regression fixture'}));
    await page.route('**/yahoo/**',async route=>{
      const url=new URL(route.request().url()), s=decodeURIComponent(url.pathname.split('/').pop());
      const interval=url.searchParams.get('interval');
      if(interval==='1m') {
        requests++;
        if(hold && s==='SPY') {held=route;return;}
        return route.fulfill({json:snapshot(s)});
      }
      const meta={...metadata(s),dataGranularity:interval};
      if(!(chartQuote && s==='SPY')) delete meta.previousClose;
      const b=base(s), last=price(s);
      return route.fulfill({json:{chart:{error:null,result:[{meta,
        timestamp:[today-2*86400,today-86400,today],
        indicators:{quote:[{open:[b-2,null,b+1],high:[b,null,last+2],low:[b-3,null,b],
          close:[b-1,null,last],volume:[1000,null,1200]}]}}]}}});
    });
    await page.goto(process.env.CHARTROOM_URL || 'http://localhost:8765/chartroom.html');
    const cache=s=>'/data/cache/'+Buffer.from(s).toString('hex')+'.quote.json';
    const quote=s=>page.evaluate(p=>JSON.parse(FS.readFile(p,{encoding:'utf8'})),cache(s));
    const waitQuote=(s,value)=>page.waitForFunction(({p,value})=>typeof FS!=='undefined' && FS.analyzePath(p).exists &&
      JSON.parse(FS.readFile(p,{encoding:'utf8'})).price===value,{p:cache(s),value});
    await waitQuote('SPY',105);await waitQuote('AAPL',205);
    assert.equal((await quote('SPY')).snapshot,true);
    assert(Math.abs((await quote('SPY')).change-5)<1e-8);
    assert(Math.abs((await quote('AAPL')).change-2.5)<1e-8);
    const initial=requests;
    phase=1;
    await page.clock.fastForward(65000);
    await waitQuote('SPY',110);await waitQuote('AAPL',210);
    assert(requests>initial,'Minute timer refreshes visible sidebar symbols without input');
    assert(Math.abs((await quote('SPY')).change-10)<1e-8);
    // A fresh chart snapshot must win over a slower, older sidebar response.
    phase=2;chartQuote=true;hold=true;
    await page.mouse.click(425,102); // Refresh current chart.
    await waitQuote('SPY',115);
    assert(held,'Chart refresh also requested a sidebar snapshot');
    hold=false;
    await held.fulfill({json:snapshot('SPY',1)});
    await page.waitForTimeout(300);
    assert.equal((await quote('SPY')).price,115,'Late quote cannot roll back fresher chart data');
    assert(Math.abs((await quote('SPY')).change-15)<1e-8);
    assert.deepEqual(errors,[]);
    console.log('Sidebar metadata quotes, missing daily rows, minute refresh, chart synchronization and out-of-order responses passed');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
