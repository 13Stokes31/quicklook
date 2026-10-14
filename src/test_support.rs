//! Archivos de prueba aislados dentro de target/.

use std::path::PathBuf;
use std::sync::atomic::{AtomicUsize, Ordering};

static NEXT_DIR: AtomicUsize = AtomicUsize::new(0);

pub struct TestDir {
    pub path: PathBuf,
}

impl TestDir {
    pub fn new() -> Self {
        let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("target/quicklook-tests")
            .join(format!(
                "{}-{}",
                std::process::id(),
                NEXT_DIR.fetch_add(1, Ordering::Relaxed)
            ));
        std::fs::create_dir_all(&path).unwrap();
        Self { path }
    }

    pub fn image(&self, name: &str, width: u32, height: u32) -> PathBuf {
        let path = self.path.join(name);
        image::RgbaImage::from_pixel(width, height, image::Rgba([255, 0, 0, 255]))
            .save(&path)
            .unwrap();
        path
    }
}

impl Drop for TestDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.path);
    }
}
