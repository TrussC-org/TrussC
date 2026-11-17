// Test pages accept repeated ?arg=... entries, including the combined test name.
Module['arguments'] = new URLSearchParams(location.search).getAll('arg');
Module['onAbort'] = function(reason) {
    window.tcCoreTestVerdict = 'FAIL runtime abort: ' + reason;
};
