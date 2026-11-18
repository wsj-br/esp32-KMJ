use chrono::Local;

fn main() {
    // Same local stamp as firmware/tools/build_stamp.py: YYYY-MM-DD HH:MM:SS.
    let stamp = Local::now().format("%Y-%m-%d %H:%M:%S");
    println!("cargo:rustc-env=KMJ_BUILD_TIMESTAMP={stamp}");
    println!("cargo:rerun-if-changed=src");
    tauri_build::build()
}
