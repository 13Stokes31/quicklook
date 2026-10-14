//! Quick Look nativo para KDE Plasma 6 / Wayland.
//!
//! Fase 1 (MVP): recibe la ruta de una imagen, abre una ventana frameless
//! ajustada a su aspect-ratio, la muestra reescalada con calidad y cierra con
//! Escape o al perder el foco.

mod nav;
mod prefetch;
mod render;
mod theme;
mod viewer;

#[cfg(test)]
mod test_support;

use std::path::PathBuf;
use std::process::ExitCode;

use viewer::Viewer;

fn main() -> ExitCode {
    let mut args = std::env::args_os().skip(1);
    let Some(mut arg) = args.next() else {
        eprintln!("uso: quicklook <ruta-de-imagen>");
        return ExitCode::FAILURE;
    };

    if arg == "--help" || arg == "-h" {
        println!("uso: quicklook [--] <ruta-de-imagen>\nFlechas: navegar. Escape/Espacio o perder el foco: cerrar.");
        return ExitCode::SUCCESS;
    }
    if arg == "--" {
        let Some(path) = args.next() else {
            eprintln!("falta la ruta de la imagen");
            return ExitCode::FAILURE;
        };
        arg = path;
    }
    if args.next().is_some() {
        eprintln!("quicklook admite un único archivo inicial");
        return ExitCode::FAILURE;
    }

    let path = PathBuf::from(arg);
    if !path.is_file() {
        eprintln!("no es un archivo: {}", path.display());
        return ExitCode::FAILURE;
    }

    match Viewer::run(path) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e:#}");
            ExitCode::FAILURE
        }
    }
}
