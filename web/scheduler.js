// Schedule work only for input, background completions or the next C++ deadline.
// This is installed after GLFW/ImGui so RAF runs after their event handlers.
Module.chartroomInstallScheduler = function () {
  var raf = 0, timer = 0, inputPending = true;
  function run() {
    raf = timer = 0;
    var input = inputPending;
    inputPending = false;
    var delay = Module._chartroom_pump(input ? 1 : 0);
    if (delay >= 0) {
      if (!document.hidden && delay <= 20) raf = requestAnimationFrame(run);
      else timer = setTimeout(run, Math.max(1, delay));
    }
  }
  Module.chartroomRequest = function (input) {
    inputPending = inputPending || !!input;
    if (timer) { clearTimeout(timer); timer = 0; }
    if (raf) {
      if (!document.hidden) return;
      cancelAnimationFrame(raf); raf = 0;
    }
    if (document.hidden) timer = setTimeout(run, 0);
    else raf = requestAnimationFrame(run);
  };
  var request = function () { Module.chartroomRequest(true); };
  ['pointermove', 'pointerdown', 'pointerup', 'pointercancel', 'pointerleave', 'wheel',
   'keydown', 'keyup', 'input', 'compositionend'].forEach(function (event) {
    window.addEventListener(event, request, {capture: true, passive: true});
  });
  ['resize', 'focus', 'blur', 'pageshow'].forEach(function (event) {
    window.addEventListener(event, request);
  });
  document.addEventListener('visibilitychange', request);
  var observer = new ResizeObserver(request);
  // A display-density change may leave CSS dimensions unchanged. Observe the
  // device-pixel box as well as listening for resolution media-query changes.
  try { observer.observe(Module.canvas, {box: 'device-pixel-content-box'}); }
  catch (_) { observer.observe(Module.canvas); }
  if (window.visualViewport) window.visualViewport.addEventListener('resize', request);
  var densityQuery;
  function densityChanged() { watchDensity(); request(); }
  function watchDensity() {
    if (densityQuery) densityQuery.removeEventListener('change', densityChanged);
    densityQuery = matchMedia('(resolution: ' + devicePixelRatio + 'dppx)');
    densityQuery.addEventListener('change', densityChanged);
  }
  watchDensity();
  async function checkBuild() {
    try {
      var response = await fetch('build-info.json', {cache: 'no-store'});
      if (!response.ok) throw new Error('Build metadata unavailable');
      var info = await response.json();
      if (info.platform === 'web' && Number.isSafeInteger(info.build_number) && info.build_number > 0) {
        var previous = Module.chartroomAvailableBuild || 0;
        Module.chartroomAvailableBuild = info.build_number;
        if (previous !== info.build_number &&
            (info.build_number > Module.chartroomBuild || previous > Module.chartroomBuild))
          Module.chartroomRequest(false);
      }
    } catch (_) { /* No update claim is made when metadata is unavailable. */ }
    setTimeout(checkBuild, 60000);
  }
  checkBuild();
  Module.chartroomRequest(true);
};
