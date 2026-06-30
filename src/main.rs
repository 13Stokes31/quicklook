//! Quick Look nativo para KDE Plasma 6 / Wayland.
//!
//! Fase 1 (MVP): recibe la ruta de una imagen, abre una ventana frameless
//! ajustada a su aspect-ratio, la muestra reescalada con calidad y cierra con
//! Escape o al perder el foco.

mod nav;
mod prefetch;
mod render;
mod viewer;

use std::path::PathBuf;
use std::process::ExitCode;

use viewer::Viewer;

fn main() -> ExitCode {
    let Some(arg) = std::env::args().nth(1) else {
        eprintln!("uso: quicklook <ruta-de-imagen>");
        return ExitCode::FAILURE;
    };

    let path = PathBuf::from(arg);
    if !path.is_file() {
        eprintln!("no es un archivo: {}", path.display());
        return ExitCode::FAILURE;
    }

    match Viewer::new(path).and_then(Viewer::run) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e:#}");
            ExitCode::FAILURE
        }
    }
}
