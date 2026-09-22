use image::{imageops, ImageFormat, ImageReader};
use std::{env, path::PathBuf};

fn main() {
    println!("cargo:rerun-if-changed=keyscribe.rc");
    println!("cargo:rerun-if-changed=../../assets/keyscribe.ico");
    embed_resource::compile("keyscribe.rc", embed_resource::NONE)
        .manifest_required()
        .expect("embed KeyScribe executable icon");

    let source = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/keyscribe-menu.png");
    println!("cargo:rerun-if-changed={}", source.display());
    let source_icon = ImageReader::open(&source)
        .expect("open KeyScribe icon")
        .decode()
        .expect("decode KeyScribe icon")
        .to_rgba8();
    // The macOS menu asset has padding; fill the smaller Windows notification slot.
    let icon = imageops::resize(
        &imageops::crop_imm(&source_icon, 5, 5, 54, 54).to_image(),
        64,
        64,
        imageops::FilterType::Lanczos3,
    );
    // 트레이 아이콘은 상태마다 색만 다르다. 평소에는 하늘색, 녹음 중에는 빨강,
    // 변환 중에는 주황 — 위젯을 꺼 둔 사용자가 색만 보고 상태를 알 수 있게 한다.
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR"));
    for (name, [red, green, blue]) in [
        ("keyscribe-tray.png", [14u8, 165, 233]),
        ("keyscribe-tray-recording.png", [232u8, 52, 52]),
        ("keyscribe-tray-transcribing.png", [240u8, 158, 32]),
    ] {
        let mut tinted = icon.clone();
        for pixel in tinted.pixels_mut() {
            pixel.0[0] = red;
            pixel.0[1] = green;
            pixel.0[2] = blue;
        }
        tinted
            .save_with_format(out_dir.join(name), ImageFormat::Png)
            .expect("write Windows tray icon");
    }
}
