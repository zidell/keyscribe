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
    let mut icon = imageops::resize(
        &imageops::crop_imm(&source_icon, 5, 5, 54, 54).to_image(),
        64,
        64,
        imageops::FilterType::Lanczos3,
    );
    for pixel in icon.pixels_mut() {
        pixel.0[0] = 14;
        pixel.0[1] = 165;
        pixel.0[2] = 233;
    }
    let output = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR")).join("keyscribe-tray.png");
    icon.save_with_format(output, ImageFormat::Png)
        .expect("write Windows tray icon");
}
