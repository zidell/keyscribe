use image::{ImageFormat, ImageReader};
use std::{env, path::PathBuf};

fn main() {
    let source = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/keyscribe-menu.png");
    println!("cargo:rerun-if-changed={}", source.display());
    let mut icon = ImageReader::open(&source)
        .expect("open KeyScribe icon")
        .decode()
        .expect("decode KeyScribe icon")
        .to_rgba8();
    for pixel in icon.pixels_mut() {
        pixel.0[0] = 14;
        pixel.0[1] = 165;
        pixel.0[2] = 233;
    }
    let output = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR")).join("keyscribe-tray.png");
    icon.save_with_format(output, ImageFormat::Png)
        .expect("write Windows tray icon");
}
