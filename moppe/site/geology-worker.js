// Grows a world in moppe-site-geology.wasm off the page's thread and posts
// a picture after every geological step (moppe/site/geology.cc):
//
//   worker.postMessage({ resolution: 256, seed: 123, size: 512 })
//   worker.onmessage = ({ data }) => ...
//     { step, steps, years, size, rgba: Uint8ClampedArray }
//     { done: true, seconds }

importScripts("moppe-site-geology.js");

onmessage = async ({ data }) => {
  const started = performance.now();
  const geology = await createGeology({
    frame(step, steps, years, rgba, size) {
      const pixels = new Uint8ClampedArray(rgba.buffer);
      postMessage({ step, steps, years, size, rgba: pixels }, [rgba.buffer]);
    },
  });
  geology._moppe_geology_grow(data.resolution, data.seed, data.size);
  postMessage({ done: true, seconds: (performance.now() - started) / 1000 });
};
