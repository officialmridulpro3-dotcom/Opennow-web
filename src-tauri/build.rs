// Tauri build script. Also exports the compile target triple to the crate so
// the runtime can locate the portable ZIP's `opennow-server-<triple>` sidecar
// without hardcoding it. (The `-<triple>` suffix is only Tauri's build-time
// `externalBin` lookup convention — the bundler strips it, so an installed
// app ships the plain `opennow-server` name, which the runtime accepts too.)
fn main() {
    println!(
        "cargo:rustc-env=OPENNOW_TARGET_TRIPLE={}",
        std::env::var("TARGET").expect("cargo always sets TARGET for build scripts")
    );
    tauri_build::build()
}
