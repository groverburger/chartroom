// Mount persistent browser storage before C++ main. Coalesce asynchronous saves.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  FS.mkdir('/data');
  FS.mount(IDBFS, {}, '/data');
  addRunDependency('chartroom-storage');
  FS.syncfs(true, function (error) {
    if (error) console.error('Chartroom: cannot restore browser storage', error);
    removeRunDependency('chartroom-storage');
  });
  var busy = false, dirty = false, reload = false;
  Module.chartroomReload = function () { reload = true; Module.chartroomSync(); };
  Module.chartroomSync = function () {
    dirty = true;
    if (busy) return;
    function flush() {
      busy = true; dirty = false;
      FS.syncfs(false, function (error) {
        if (error) console.error('Chartroom: cannot save browser storage', error);
        if (dirty) flush();
        else {
          busy = false;
          if (reload && !error) location.reload();
        }
      });
    }
    flush();
  };
});
