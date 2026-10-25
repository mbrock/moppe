// Runs in the page before the program starts (Emscripten's --pre-js).
//
// The program's WebGPU device comes from here: acquiring one is
// asynchronous, and the SDL host wants it when it makes its window, so the
// program waits for Module.preinitializedWebGPUDevice, which NHAL takes up
// (moppe/nhal/webgpu/webgpu_device.cc).
//
// The page's address carries the command line and the development
// switches: ?args=--seed+5+--graphics-quality+low&MOPPE_WEATHER=mist.

{
  const query = new URLSearchParams(location.search);
  Module['arguments'] =
    (query.get('args') || '').split(/\s+/).filter((word) => word.length);

  // Shows `message` over the canvas, for failures the player should see.
  Module['moppeFail'] = (message) => {
    console.error(message);
    let note = document.getElementById('moppe-failure');
    if (!note) {
      note = document.createElement('div');
      note.id = 'moppe-failure';
      note.style.cssText =
        'position:fixed;left:1rem;bottom:1rem;right:1rem;z-index:10;' +
        'color:#ffd9c2;font:13px ui-monospace,monospace;white-space:pre-wrap';
      document.documentElement.appendChild(note);
    }
    note.textContent = message;
  };

  // Covers the page, its touch controls too, to say plainly that this
  // browser cannot draw the game and where it can be played instead.
  const unsupported = (reason) => {
    console.error(reason);
    for (const id of ['touch', 'status']) {
      const element = document.getElementById(id);
      if (element)
        element.style.display = 'none';
    }
    const card = document.createElement('div');
    card.id = 'moppe-unsupported';
    card.style.cssText =
      'position:fixed;inset:0;z-index:1000;display:flex;' +
      'align-items:center;justify-content:center;padding:1.5rem;' +
      'background:#10170f;color:#dcebdd;' +
      'font:16px/1.5 system-ui,sans-serif;text-align:left';
    const releases = 'https://github.com/mbrock/moppe/releases/latest';
    card.innerHTML =
      '<div style="max-width:32rem">' +
      '<h1 style="font-size:1.4rem;margin:0 0 .75rem">' +
      'Moppe needs WebGPU</h1>' +
      '<p style="margin:0 0 .75rem;color:#ffd9c2"></p>' +
      '<p style="margin:0 0 .75rem">It draws with WebGPU, which this ' +
      'browser or its graphics chip does not offer. It runs in Chrome or ' +
      'Edge on a laptop or desktop, in Safari 26 on a recent iPhone, ' +
      'iPad, or Mac, and in Chrome on Android 12 or newer with a recent ' +
      'phone.</p>' +
      '<p style="margin:0">Or download the game for Windows, macOS, or ' +
      'Linux: <a style="color:#9fd8a0" href="' + releases + '">' +
      releases.replace('https://', '') + '</a></p></div>';
    card.querySelector('p').textContent = reason;
    document.documentElement.appendChild(card);
  };

  const acquireDevice = async () => {
    if (!navigator.gpu)
      throw new Error('This browser has no WebGPU.');
    const adapter = await navigator.gpu.requestAdapter(
      { powerPreference: 'high-performance' });
    if (!adapter)
      throw new Error('WebGPU found no graphics adapter.');
    if (!adapter.features.has('float32-filterable'))
      throw new Error('This graphics adapter cannot filter 32-bit float ' +
                      'textures, which the terrain needs.');
    // Filtering the terrain's float textures is required; pass timings
    // are taken where offered.
    const features = ['float32-filterable', 'timestamp-query']
      .filter((feature) => adapter.features.has(feature));
    // The renderer's programs and buffers are sized for desktop GPUs, past
    // WebGPU's default limits; take what the adapter has.
    const limits = {};
    for (const limit of ['maxBufferSize', 'maxStorageBufferBindingSize',
                         'maxStorageBuffersPerShaderStage',
                         'maxSampledTexturesPerShaderStage',
                         'maxUniformBuffersPerShaderStage',
                         'maxDynamicUniformBuffersPerPipelineLayout',
                         'maxDynamicStorageBuffersPerPipelineLayout',
                         'maxTextureDimension2D'])
      limits[limit] = adapter.limits[limit];
    const device = await adapter.requestDevice(
      { requiredFeatures: features, requiredLimits: limits });
    // Validation errors are the renderer's bugs; the first few say enough.
    let reported = 0;
    device.addEventListener('uncapturederror', (event) => {
      if (reported++ < 16)
        console.error('WebGPU: ' + event.error.message);
    });
    device.lost.then((loss) => {
      if (loss.reason !== 'destroyed')
        Module['moppeFail']('The graphics device was lost: ' + loss.message);
    });
    return device;
  };

  Module['preRun'] = Module['preRun'] || [];
  Module['preRun'].push(() => {
    // A phone -- fingers and nothing to hover with -- has a screen of
    // three pixels a point, more than its GPU fills at ease or the eye
    // tells from two, so its drawable is two (docs/web.md); the address
    // can say otherwise.
    if (matchMedia('(pointer: coarse) and (hover: none)').matches)
      ENV['MOPPE_PIXELDENSITY'] = '2';
    for (const [name, value] of query)
      if (name.startsWith('MOPPE_'))
        ENV[name] = value;
    addRunDependency('webgpu');
    acquireDevice().then(
      (device) => {
        Module['preinitializedWebGPUDevice'] = device;
        removeRunDependency('webgpu');
      },
      (error) => unsupported(error.message));
  });
}
