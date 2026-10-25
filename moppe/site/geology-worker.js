// Grows a world in moppe-site-geology.wasm off the page's thread and posts
// its heights after every geological step (moppe/site/geology.cc):
//
//   worker.postMessage({ lattice: 256, seed: 123, size: 0 })
//   worker.onmessage = ({ data }) => ...
//     { step, steps, years, lattice, heights: Float32Array,
//       rgba: Uint8ClampedArray or null, size }
//     { done: true, seconds }
//
// A size other than zero also paints each step as the map sheet's relief,
// that many pixels square, for a page without WebGPU.

importScripts("moppe-site-geology.js");

onmessage = async ({ data }) => {
  const started = performance.now();
  const geology = await createGeology({
    step(step, steps, years, heights, lattice, rgba, size) {
      const pixels = rgba && new Uint8ClampedArray(rgba.buffer);
      const transfer = [heights.buffer];
      if (rgba) transfer.push(rgba.buffer);
      postMessage(
        { step, steps, years, lattice, heights, rgba: pixels, size },
        transfer,
      );
    },
  });
  geology._moppe_geology_grow(data.lattice, data.seed, data.size);
  postMessage({ done: true, seconds: (performance.now() - started) / 1000 });
};
