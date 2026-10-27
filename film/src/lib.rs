//! One day at the trailhead: a vertical film of moppe for phones.
//!
//! Each shot is a clip the game recorded of itself (film/record), trimmed
//! to the shot, at the hour of the day it shows; they follow each other
//! through a day, from morning over the birches to the same birches under
//! the stars. Over every shot a hairline arc of the sky carries the sun
//! across it by the hour, and the moon after dusk, with the hour under it,
//! so the cuts read as the day going by. The title comes up over the
//! morning and the address over the night.
use fframes::{
    Color, Duration, FFramesContext, FFramesSyncedVideoFrame, Frame, Overlap, Scene, Scenes, Svgr, SyncVideoFrameInput,
    Video, include_media_dir,
};

include_media_dir!(pub struct MoppeFilmMedia, "media");

pub const WIDTH: usize = 1080;
pub const HEIGHT: usize = 1920;

const FONT: &str = "Geomini";
const WEIGHT: u16 = 400;
const INK: &str = "#fbf7ee";
const SUN: &str = "#ffe2b0";
const MOON: &str = "#dfe7f2";

/// How long one shot's picture fades into the next, seconds.
const CROSS: f32 = 0.8;
/// The game's day passes in 48 minutes: an hour of it every two minutes.
const HOURS_PER_SECOND: f32 = 1.0 / 120.0;

/// One shot: its clip (footage/shots/NAME.mp4), how long it holds before
/// the next begins to fade in, and the hour its clip begins at, which is
/// the clock film/record recorded it at plus where the shot was cut from.
#[derive(Debug)]
pub struct Shot {
    name: &'static str,
    clip: &'static str,
    seconds: f32,
    hour: f32,
    /// How far the picture creeps in over the shot, as a scale.
    push: f32,
}

const fn shot(name: &'static str, clip: &'static str, seconds: f32, hour: f32) -> Shot {
    Shot { name, clip, seconds, hour, push: 0.05 }
}

pub const SHOTS: [Shot; 9] = [
    shot("Morning", "morning.mp4", 5.2, 7.60),
    shot("Ride", "ride.mp4", 4.0, 9.59),
    shot("Glide", "glide.mp4", 4.4, 11.09),
    shot("Helicopter", "helicopter.mp4", 4.4, 12.63),
    shot("River", "river.mp4", 4.4, 14.03),
    shot("Tractor", "tractor.mp4", 4.0, 15.59),
    shot("Caddy", "caddy.mp4", 4.0, 17.05),
    shot("Dusk", "dusk.mp4", 4.4, 17.70),
    shot("Night", "night.mp4", 5.6, 22.00),
];

impl Scene for Shot {
    fn duration(&self) -> Duration<'_> {
        Duration::Seconds(self.seconds)
    }

    fn overlap(&self) -> Overlap {
        if self.name == SHOTS[0].name { Overlap::None } else { Overlap::Previous(CROSS) }
    }

    fn name(&self) -> &'static str {
        self.name
    }

    fn render_frame<'a>(&'a self, frame: Frame, ctx: &FFramesContext<'a, '_>) -> Svgr<'a> {
        let input = SyncVideoFrameInput { start_from: 0.0, looping: false, editor_fallback_image: None };
        let Some(clip) = frame.get_synced_video_frame(ctx, self.clip, &input) else {
            return Svgr::empty();
        };
        let clip = clip.into_image();
        let first = self.name == SHOTS[0].name;
        let t = frame.seconds();
        let length = self.seconds + if first { 0.0 } else { CROSS };
        // The picture creeps in about a point a little above the middle,
        // where the horizon mostly lies, and fades up over the last shot.
        let scale = 1.0 + self.push * ease_in_out((t / length).clamp(0.0, 1.0));
        let (cx, cy) = (WIDTH as f32 / 2.0, HEIGHT as f32 * 0.42);
        let opacity = if first { 1.0 } else { ease_in_out((t / CROSS).clamp(0.0, 1.0)) };
        fframes::svgr!(
            <g opacity={opacity}>
                <g transform={format!(
                    "translate({} {}) scale({scale}) translate({} {})", cx, cy, -cx, -cy)}>
                    <image href={clip.href()} x="0" y="0" width={WIDTH} height={HEIGHT} />
                </g>
            </g>
        )
    }
}

fn ease_in_out(t: f32) -> f32 {
    t * t * (3.0 - 2.0 * t)
}

/// Where each shot's own time runs in the film: (start, end of its hold).
/// A shot starts as the one before begins to fade out.
fn spans() -> [(f32, f32); 9] {
    let mut spans = [(0.0, 0.0); 9];
    let mut end = 0.0;
    for (i, shot) in SHOTS.iter().enumerate() {
        let start = if i == 0 { 0.0 } else { end - CROSS };
        end += shot.seconds;
        spans[i] = (start, end);
    }
    spans
}

/// The hour shown at `t` seconds into the film: each shot's own clock as
/// it runs, and across a cross-fade a glide from one to the next.
fn hour_at(t: f32) -> f32 {
    let spans = spans();
    let own = |i: usize| SHOTS[i].hour + (t - spans[i].0).max(0.0) * HOURS_PER_SECOND;
    for i in 0..SHOTS.len() {
        let next = i + 1;
        if next < SHOTS.len() && t >= spans[next].0 && t < spans[i].1 {
            let k = ease_in_out((t - spans[next].0) / CROSS);
            return own(i) + (own(next) - own(i)) * k;
        }
        if t < spans[i].1 || next == SHOTS.len() {
            return own(i);
        }
    }
    own(SHOTS.len() - 1)
}

/// A point on the sky's arc, from the east at 0 to the west at 1.
fn on_arc(p: f32) -> (f32, f32) {
    let angle = std::f32::consts::PI * p;
    (ARC_X - ARC_RX * angle.cos(), ARC_Y - ARC_RY * angle.sin())
}

const ARC_X: f32 = 540.0;
const ARC_Y: f32 = 560.0;
const ARC_RX: f32 = 380.0;
const ARC_RY: f32 = 210.0;

pub struct MoppeFilmVideo<'a> {
    pub media: &'a MoppeFilmMedia,
    /// Where each letter of the title is centred, measured once.
    letters: std::sync::OnceLock<[f32; 5]>,
}

const NAME: &str = "moppe";
const TITLE_SIZE: usize = 176;

impl<'a> MoppeFilmVideo<'a> {
    pub fn new(media: &'a MoppeFilmMedia) -> Self {
        Self { media, letters: std::sync::OnceLock::new() }
    }

    /// The sky's arc, the sun or the moon on it, and the hour under it.
    fn day<'b>(&self, t: f32) -> Svgr<'b> {
        let hour = hour_at(t);
        // The sun from six to six; after it is down, the waxing moon,
        // which crossed the sky behind it and sets at half past two.
        let sun = 1.0 - smooth(17.8, 18.3, hour);
        let moon = smooth(18.4, 19.0, hour);
        let (sx, sy) = on_arc(((hour - 6.0) / 12.0).clamp(-0.05, 1.05));
        let (mx, my) = on_arc(((hour - 14.5) / 12.0).clamp(0.0, 1.0));
        let shown = hour.rem_euclid(24.0);
        let clock = format!("{:02}:{:02}", shown as u32, ((shown.fract()) * 60.0) as u32);
        let (dx, dy, dot) = if moon > sun { (mx, my, MOON) } else { (sx, sy, SUN) };
        let start = on_arc(0.0);
        let end = on_arc(1.0);
        let arc = format!(
            "M {} {} A {ARC_RX} {ARC_RY} 0 0 1 {} {}",
            start.0, start.1, end.0, end.1
        );
        fframes::svgr!(
            <g>
                <path d={arc} fill="none" stroke={INK} stroke-width="2" stroke-opacity="0.45" />
                <line x1={start.0 - 34.0} y1={start.1} x2={start.0 + 14.0} y2={start.1}
                    stroke={INK} stroke-width="2" stroke-opacity="0.45" />
                <line x1={end.0 - 14.0} y1={end.1} x2={end.0 + 34.0} y2={end.1}
                    stroke={INK} stroke-width="2" stroke-opacity="0.45" />
                <circle cx={sx} cy={sy} r="34" fill="url(#halo)" opacity={sun} />
                <circle cx={sx} cy={sy} r="11" fill={SUN} opacity={sun} />
                <circle cx={mx} cy={my} r="26" fill="url(#moonhalo)" opacity={moon} />
                <circle cx={mx} cy={my} r="9" fill={MOON} opacity={moon} />
                <text x={dx} y={dy + 62.0} font-family={FONT} font-size="34"
                    font-weight={WEIGHT} fill={dot} text-anchor="middle" letter-spacing="2">
                    {clock}
                </text>
            </g>
        )
    }

    /// The name over the morning: its letters come up one after another.
    fn title<'b>(&self, frame: &mut Frame, ctx: &FFramesContext<'b, '_>) -> Svgr<'b> {
        let t = frame.seconds();
        let font = || fframes::FontQuery {
            family: FONT,
            size: TITLE_SIZE,
            weight: WEIGHT,
            ..Default::default()
        };
        let centres = match self.letters.get() {
            Some(centres) => *centres,
            None => {
                let mut width = |text: &'static str| {
                    frame.text_width(ctx, font(), text).map(|w| w as f32)
                };
                let measured = (|| {
                    let whole = width(NAME)?;
                    let mut centres = [0.0; 5];
                    for (i, centre) in centres.iter_mut().enumerate() {
                        let before = if i == 0 { 0.0 } else { width(&NAME[..i])? };
                        let letter = width(&NAME[i..i + 1])?;
                        *centre = 540.0 - whole / 2.0 + before + letter / 2.0;
                    }
                    Some(centres)
                })();
                match measured {
                    Some(centres) => *self.letters.get_or_init(|| centres),
                    None => [540.0 - 192.0, 540.0 - 96.0, 540.0, 540.0 + 96.0, 540.0 + 192.0],
                }
            }
        };
        let out = 1.0 - smooth(4.0, 4.7, t);
        let letters: Vec<Svgr> = NAME
            .chars()
            .enumerate()
            .map(|(i, letter)| {
                let at = 0.5 + i as f32 * 0.09;
                let k = spring((t - at).max(0.0));
                let x = centres[i];
                let y = 1010.0 + (1.0 - k) * 36.0;
                let o = smooth(at, at + 0.35, t);
                fframes::svgr!(
                    <text x={x} y={y} font-family={FONT} font-size={TITLE_SIZE} font-weight={WEIGHT}
                        fill={INK} text-anchor="middle" opacity={o}>
                        {letter.to_string()}
                    </text>
                )
            })
            .collect();
        let tagline = smooth(1.5, 2.1, t);
        fframes::svgr!(
            <g opacity={out}>
                {letters}
                <text x="540" y={1092.0 + (1.0 - tagline) * 14.0} font-family={FONT} font-size="44"
                    font-weight={WEIGHT} fill={INK} text-anchor="middle" opacity={tagline}>
                    "a small game about being outdoors"
                </text>
            </g>
        )
    }

    /// The end card over the night birches.
    fn end<'b>(&self, t: f32) -> Svgr<'b> {
        let name = smooth(0.6, 1.3, t);
        let invite = smooth(1.3, 1.9, t);
        let address = smooth(1.6, 2.3, t);
        fframes::svgr!(
            <g>
                <text x="540" y={1010.0 + (1.0 - name) * 30.0} font-family={FONT} font-size="150"
                    font-weight={WEIGHT} fill={INK} text-anchor="middle" opacity={name}>
                    "moppe"
                </text>
                <text x="540" y={1086.0 + (1.0 - invite) * 14.0} font-family={FONT} font-size="40"
                    font-weight={WEIGHT} fill={INK} text-anchor="middle" opacity={invite * 0.85}>
                    "play it in your browser"
                </text>
                <text x="540" y={1166.0 + (1.0 - address) * 14.0} font-family={FONT} font-size="54"
                    font-weight={WEIGHT} fill={SUN} text-anchor="middle" opacity={address}
                    letter-spacing="1">
                    "moppe.swa.sh"
                </text>
            </g>
        )
    }
}

fn smooth(from: f32, to: f32, t: f32) -> f32 {
    ease_in_out(((t - from) / (to - from)).clamp(0.0, 1.0))
}

/// A soft spring from 0 to 1, settling within about half a second.
fn spring(t: f32) -> f32 {
    1.0 - (-9.0 * t).exp() * (9.0 * t + 1.0)
}

impl std::fmt::Debug for MoppeFilmVideo<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("MoppeFilmVideo").finish()
    }
}

impl Video for MoppeFilmVideo<'_> {
    const FPS: usize = 30;
    const WIDTH: usize = WIDTH;
    const HEIGHT: usize = HEIGHT;
    const BACKGROUND_COLOR: Color = Color::BLACK;

    fn duration(&self) -> Duration<'_> {
        Duration::Auto
    }

    fn audio(&self) -> fframes::AudioMap<'_> {
        fframes::AudioMap::none()
    }

    fn define_scenes(&self) -> Scenes<'_> {
        Scenes::from(SHOTS.iter().map(|s| s as &dyn Scene).collect::<Vec<_>>())
    }

    fn render_frame<'a>(&'a self, mut frame: Frame, ctx: &FFramesContext<'a, '_>) -> Svgr<'a> {
        let t = frame.seconds();
        let spans = spans();
        let last = spans[SHOTS.len() - 1];
        let total = last.1;
        let titled = t < spans[1].0 + CROSS;
        let ending = t >= last.0 + CROSS * 0.5;
        let title = if titled { self.title(&mut frame, ctx) } else { Svgr::empty() };
        let end = if ending { self.end(t - last.0) } else { Svgr::empty() };
        // A soft shade behind the words, and the film fades out at its end.
        let pool = if titled { 1.0 - smooth(4.0, 4.7, t) } else if ending { smooth(0.4, 1.3, t - last.0) } else { 0.0 };
        let fade = 1.0 - smooth(total - 0.6, total, t);
        fframes::svgr!(
            <svg xmlns="http://www.w3.org/2000/svg" width={WIDTH} height={HEIGHT}
                viewBox={format!("0 0 {WIDTH} {HEIGHT}")}>
                <defs>
                    <radialGradient id="halo" cx="0.5" cy="0.5" r="0.5">
                        <stop offset="0" stop-color={SUN} stop-opacity="0.55" />
                        <stop offset="1" stop-color={SUN} stop-opacity="0" />
                    </radialGradient>
                    <radialGradient id="moonhalo" cx="0.5" cy="0.5" r="0.5">
                        <stop offset="0" stop-color={MOON} stop-opacity="0.45" />
                        <stop offset="1" stop-color={MOON} stop-opacity="0" />
                    </radialGradient>
                    <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
                        <stop offset="0" stop-color="#000" stop-opacity="0.38" />
                        <stop offset="1" stop-color="#000" stop-opacity="0" />
                    </linearGradient>
                    <radialGradient id="pool" cx="0.5" cy="0.5" r="0.5">
                        <stop offset="0" stop-color="#000" stop-opacity="0.32" />
                        <stop offset="1" stop-color="#000" stop-opacity="0" />
                    </radialGradient>
                </defs>
                <g opacity={fade}>
                    {ctx.render_scenes(&frame)}
                    <rect x="0" y="0" width={WIDTH} height="900" fill="url(#sky)" />
                    <ellipse cx="540" cy="1060" rx="620" ry="260" fill="url(#pool)"
                        opacity={pool} />
                    {self.day(t)}
                    {title}
                    {end}
                </g>
            </svg>
        )
    }
}
