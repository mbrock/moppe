// Runs in the page before the program starts (Emscripten's --pre-js),
// after pre.js.
//
// A phone's controls, drawn by the page over the canvas and shown once a
// finger touches it (or at once where fingers are all there is): a thumb
// put down on the left half finds a stick under it that walks and steers,
// and pushed well past its rim it runs; dragging on the right looks
// around; and the buttons are whatever the game says can be done just now
// (platform::offer_actions, from the HUD's prompts): what Space does under
// the right thumb (Jump, Boost, Brake, Flare, Begin), and the rest in
// words above it (Ride, Pick the chanterelle, Light a fire, Get off).
// They reach the game through the host's moppe_touch_* functions
// (moppe/platform/sdl/host.cc). The layout is shell.html's.

{
  const send = (name, ...values) => {
    const entry = Module['_moppe_touch_' + name];
    if (typeof entry === 'function')
      entry(...values);
  };

  // The notch and home bar, for the HUD (platform::safe_insets).
  const probe = document.createElement('div');
  probe.style.cssText =
    'position:fixed;visibility:hidden;pointer-events:none;' +
    'padding:env(safe-area-inset-top) env(safe-area-inset-right) ' +
    'env(safe-area-inset-bottom) env(safe-area-inset-left)';
  const measureInsets = () => {
    const style = getComputedStyle(probe);
    Module['moppeInsets'] =
      ['paddingLeft', 'paddingTop', 'paddingRight', 'paddingBottom']
        .map((side) => parseFloat(style[side]) || 0);
  };

  // A displacement past the dead zone, rescaled so the zone's edge reads
  // zero and full travel one.
  const axis = (displacement, deadZone, travel) => {
    const magnitude = Math.abs(displacement);
    if (magnitude <= deadZone)
      return 0;
    const value = Math.min(1, (magnitude - deadZone) / (travel - deadZone));
    return displacement < 0 ? -value : value;
  };

  const element = (className, parent, text) => {
    const made = document.createElement('div');
    made.className = className;
    if (text)
      made.textContent = text;
    parent.appendChild(made);
    return made;
  };

  // Follows one finger from where it lands on `zone` until it lifts.
  const follow = (zone, began, moved, ended) => {
    let finger = null;
    zone.addEventListener('pointerdown', (event) => {
      if (event.pointerType === 'mouse' || finger !== null)
        return;
      event.preventDefault();
      finger = event.pointerId;
      zone.setPointerCapture(finger);
      began(event);
    });
    zone.addEventListener('pointermove', (event) => {
      if (event.pointerId === finger)
        moved(event);
    });
    const lift = (event) => {
      if (event.pointerId !== finger)
        return;
      finger = null;
      ended(event);
    };
    // An interrupted touch lets go like a lifted one, so nothing sticks.
    for (const type of ['pointerup', 'pointercancel', 'lostpointercapture'])
      zone.addEventListener(type, lift);
  };

  const build = () => {
    // SDL's window takes the body for the canvas alone and puts away what
    // else is in it, so the controls stand beside the body.
    const page = document.documentElement;
    const controls = element('', page);
    controls.id = 'touch';
    controls.hidden = true;
    page.appendChild(probe);
    measureInsets();
    addEventListener('resize', measureInsets);
    addEventListener('orientationchange', measureInsets);

    // The stick: its base where the thumb landed, its knob under the thumb.
    const travel = 56, deadZone = 7, runAt = 1.5 * travel;
    const stickZone = element('zone stick', controls);
    const base = element('stick-base', controls);
    const knob = element('stick-knob', controls);
    let centre = { x: 0, y: 0 }, running = false;
    const place = (part, x, y) => {
      part.style.transform = `translate(${x}px, ${y}px)`;
    };
    const run = (wanted) => {
      if (wanted !== running)
        send('key', '^'.charCodeAt(0), wanted ? 1 : 0);
      running = wanted;
    };
    const push = (event) => {
      const dx = event.clientX - centre.x, dy = centre.y - event.clientY;
      const steer = axis(dx, deadZone, travel), turn = Math.abs(steer);
      // Small deflections steer finely, as on the iPhone.
      send('stick', Math.sign(steer) * turn * (0.35 + 0.65 * turn),
           axis(dy, deadZone, travel), 1);
      const length = Math.hypot(dx, dy);
      const scale = length > travel ? travel / length : 1;
      place(knob, centre.x + dx * scale, centre.y - dy * scale);
      run(length > runAt);
    };
    follow(stickZone,
      (event) => {
        centre = { x: event.clientX, y: event.clientY };
        place(base, centre.x, centre.y);
        base.classList.add('shown');
        knob.classList.add('shown');
        push(event);
      },
      push,
      () => {
        base.classList.remove('shown');
        knob.classList.remove('shown');
        run(false);
        send('stick', 0, 0, 0);
      });

    // Dragging on the right turns the head, as the mouse does.
    let last = { x: 0, y: 0 };
    follow(element('zone look', controls),
      (event) => { last = { x: event.clientX, y: event.clientY }; },
      (event) => {
        send('look', event.clientX, event.clientY,
             1.4 * (event.clientX - last.x), 1.4 * (event.clientY - last.y));
        last = { x: event.clientX, y: event.clientY };
      },
      () => {});

    // A button for a key, held while a finger is on it.
    const key = (className, parent, character) => {
      const button = element(className, parent);
      const code = character.charCodeAt(0);
      button.release = () => {
        if (button.classList.contains('held'))
          send('key', code, 0);
        button.classList.remove('held');
      };
      follow(button,
        () => { button.classList.add('held'); send('key', code, 1); },
        () => {},
        button.release);
      return button;
    };

    // What the game offers, a line each: the key's character code and the
    // words. Space is the thumb's own button; the others stack above it,
    // the prompt's nearest the thumb. A button outlives a change of words,
    // and one no longer offered lets go of its key.
    const thumb = key('key thumb', controls, ' ');
    thumb.hidden = true;
    const offers = element('offers', controls);
    const offered = new Map();
    Module['moppeOffers'] = (lines) => {
      const wanted = new Map();
      for (const line of lines.split('\n')) {
        const gap = line.indexOf(' ');
        if (gap > 0)
          wanted.set(String.fromCharCode(Number(line.slice(0, gap))),
                     line.slice(gap + 1));
      }
      thumb.hidden = !wanted.has(' ');
      if (thumb.hidden)
        thumb.release();
      else
        thumb.textContent = wanted.get(' ');
      wanted.delete(' ');
      for (const [character, button] of offered)
        if (!wanted.has(character)) {
          button.release();
          button.remove();
          offered.delete(character);
        }
      let place = 0;
      for (const [character, words] of wanted) {
        if (!offered.has(character))
          offered.set(character, key('key offer', offers, character));
        const button = offered.get(character);
        button.textContent = words;
        button.style.order = place++;
      }
    };
    const view = key('key view', controls, '\t');
    view.textContent = 'View';

    // Where the browser can fill the screen, a button asks it to, and to
    // hold the phone's screen sideways if it will. An iPhone's browser
    // cannot, but a page on its home screen opens without the browser's
    // bars, so there the button says how.
    const enter = page.requestFullscreen || page.webkitRequestFullscreen;
    const leave = document.exitFullscreen || document.webkitExitFullscreen;
    const filling = () =>
      document.fullscreenElement || document.webkitFullscreenElement;
    const alone = navigator.standalone ||
                  matchMedia('(display-mode: standalone)').matches ||
                  matchMedia('(display-mode: fullscreen)').matches;
    if ((document.fullscreenEnabled || document.webkitFullscreenEnabled) &&
        enter && leave) {
      const full = element('key full', controls, 'Full');
      full.addEventListener('click', () => {
        if (filling())
          leave.call(document);
        else
          Promise.resolve(enter.call(page, { navigationUI: 'hide' }))
            .then(() => screen.orientation.lock('landscape'))
            .catch(() => {});
      });
      for (const type of ['fullscreenchange', 'webkitfullscreenchange'])
        document.addEventListener(type, () => {
          full.textContent = filling() ? 'Exit' : 'Full';
        });
    } else if (!alone && navigator.standalone === false) {
      const full = element('key full', controls, 'Full');
      const how = element('how', controls,
        'For the whole screen: Share, then Add to Home Screen, ' +
        'and open Moppe from there.');
      how.hidden = true;
      full.addEventListener('click', () => { how.hidden = !how.hidden; });
      how.addEventListener('click', () => { how.hidden = true; });
    }

    for (const type of ['contextmenu', 'gesturestart', 'dblclick'])
      controls.addEventListener(type, (event) => event.preventDefault());
    // A mouse takes the page back for itself; the next finger returns them.
    // The game leaves its keyed prompts to the buttons while they show.
    const show = (shown) => {
      controls.hidden = !shown;
      Module['moppeTouching'] = shown;
    };
    controls.addEventListener('pointermove', (event) => {
      if (event.pointerType === 'mouse')
        show(false);
    });
    addEventListener('touchstart', () => show(true),
                     { capture: true, passive: true });
    show(matchMedia('(pointer: coarse)').matches);
  };

  build();
}
