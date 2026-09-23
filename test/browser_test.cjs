// Integration check: run the supplied preview server first. Requires Playwright.
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
(async()=>{
 const browser=await chromium.launch({headless:true,...(process.env.CHROMIUM_EXECUTABLE ? {executablePath:process.env.CHROMIUM_EXECUTABLE} : {}),args:['--enable-webgl','--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 try {
 const page=await browser.newPage({viewport:{width:1440,height:900},deviceScaleFactor:2});
 // CDP changes deviceScaleFactor without dispatching MediaQueryList change events.
 // Retain the app's resolution query so the test can deliver the OS notification.
 await page.addInitScript(()=>{
   const original=window.matchMedia.bind(window);
   window.chartroomResolutionQueries=[];
   window.matchMedia=query=>{const result=original(query);if(query.includes('resolution:'))window.chartroomResolutionQueries.push(result);return result;};
 });
 const errors=[];page.on('pageerror', e=>errors.push(e.message));page.on('console', m=>{if(m.type()==='error')console.log('browser:',m.text());});
 await page.goto(process.env.CHARTROOM_URL || 'http://localhost:8765/chartroom.html');
 await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/workspace.json').exists,{},{timeout:60000});
 await page.waitForFunction(()=>FS.analyzePath('/data/cache/535059.1d.json').exists,{},{timeout:60000});
 await page.waitForTimeout(1600);
 const state=()=>page.evaluate(()=>JSON.parse(FS.readFile('/data/workspace.json',{encoding:'utf8'})));
 const initialCanvas=await page.locator('canvas').evaluate(c=>[c.width,c.height,c.clientWidth,c.clientHeight]);console.log('Retina canvas',initialCanvas);if(initialCanvas.join(',')!=='2880,1800,1440,900')throw Error('Retina backing size mismatch');
 const original=await state();console.log('loaded',original.charts.length,'chart',original.charts[0].symbol);
 await page.mouse.move(800,400);await page.mouse.wheel(0,-180);await page.waitForTimeout(1500);
 const zoomed=await state();if(!(zoomed.charts[0].view.count<original.charts[0].view.count))throw Error('Browser zoom did not reduce visible count');
 await page.mouse.click(35,70);await page.waitForTimeout(1500);
 let multiple=await state();if(multiple.charts.length!==2)throw Error('Add Chart failed');
 if(process.env.CHARTROOM_SCREENSHOT) await page.screenshot({path:process.env.CHARTROOM_SCREENSHOT});
 await page.reload();await page.waitForFunction(()=>typeof FS!=='undefined'&&FS.analyzePath('/data/workspace.json').exists);await page.waitForTimeout(2000);
 const restored=await state();if(restored.charts.length!==2)throw Error('Browser persistence failed');if(restored.charts[0].view.count!==zoomed.charts[0].view.count)throw Error('Browser zoom persistence failed');
 await page.setViewportSize({width:1100,height:750});await page.waitForTimeout(500);
 const canvas=await page.locator('canvas').evaluate(c=>[c.width,c.height]);console.log('canvas after resize',canvas);
 if(canvas[0]!==2200||canvas[1]!==1500)throw Error('Browser resize failed');
 const cdp=await page.context().newCDPSession(page);
 for(const ratio of [1.5,1,2]){await cdp.send('Emulation.setDeviceMetricsOverride',{width:1100,height:750,deviceScaleFactor:ratio,mobile:false});await page.evaluate(()=>window.chartroomResolutionQueries.at(-1).dispatchEvent(new Event('change')));await page.waitForTimeout(400);const dimensions=await page.locator('canvas').evaluate(c=>[c.width,c.height,c.clientWidth,c.clientHeight]);console.log('DPR',ratio,dimensions);if(dimensions[0]!==Math.round(1100*ratio)||dimensions[1]!==Math.round(750*ratio))throw Error('Dynamic DPR failed');}
 if(errors.length)throw Error(errors.join('\n'));console.log('WebAssembly real SPY, Retina scaling, mouse zoom, multiple charts, IndexedDB restart, resize and dynamic DPR passed');} finally { await browser.close(); }
})().catch(e=>{console.error(e);process.exit(1);});
