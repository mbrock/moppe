// Runs in the page before the program starts (Emscripten's --pre-js).
//
// The program's WebGPU device comes from here: acquiring one is
// asynchronous, and the SDL host wants it when it makes its window, so the
// program waits for Module.preinitializedWebGPUDevice, which NHAL takes up
// (moppe/nhal/webgpu/webgpu_device.cc).
//
// The page's address carries the command line and the development
// switches: ?args=--seed+5+--graphics-quality+low&MOPPE_WEATHER=mist.

// Workers load this file too; only the page has a document.
if (typeof document !== 'undefined') {
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
    for (const [name, value] of query)
      if (name.startsWith('MOPPE_'))
        ENV[name] = value;
    addRunDependency('webgpu');
    acquireDevice().then(
      (device) => {
        Module['preinitializedWebGPUDevice'] = device;
        removeRunDependency('webgpu');
      },
      (error) => Module['moppeFail'](error.message));
  });
}
