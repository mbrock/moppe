use fframes::{
    CombinedMediaProvider, EncoderOptions, MediaDirectory, MediaProvider, RenderOptions,
    StaticMediaProvider, cli,
};
use fframes_skia_renderer::{
    SkiaFFramesRenderer, SkiaPipelineConcurrencyPolicy, SkiaPipelineConfig,
    vulkan::SkiaVulkanCtx,
};
use moppe_film::{HEIGHT, MoppeFilmMedia, MoppeFilmVideo, WIDTH};
use std::path::Path;
use std::process::ExitCode;

fn main() -> ExitCode {
    let args = cli::parse::<cli::NoArgs>();
    let media = MoppeFilmMedia::prepare().expect("media");
    // The shots film/record cut from the game's recordings, read at run time.
    let folder = Path::new(env!("CARGO_MANIFEST_DIR")).join("footage").join("shots");
    let shots = MediaDirectory::read_folder(&folder).expect("footage: run film/record first");
    let footage = shots.process_media_source().expect("footage");
    let all = CombinedMediaProvider::from([&media as &dyn MediaProvider, &footage]);
    let video = MoppeFilmVideo::new(&media);
    let gpu = SkiaVulkanCtx::new(WIDTH, HEIGHT).expect("GPU context");

    cli::new(
        &video,
        RenderOptions {
            media: Some(&all),
            video_encoder_options: EncoderOptions {
                preferred_encoder: Some("libx264"),
                codec_params: Some(&[("crf", "16"), ("preset", "slow"), ("tune", "film")]),
                ..Default::default()
            },
            ..Default::default()
        },
    )
    .args(args)
    .backend(
        SkiaFFramesRenderer::new_vulkan(
            &gpu,
            SkiaPipelineConfig {
                concurrency_policy: SkiaPipelineConcurrencyPolicy::MaxPerformance,
                ..Default::default()
            },
        )
        .expect("skia renderer"),
    )
    .preview(fframes_native_player::cli_preview)
    .run()
}
