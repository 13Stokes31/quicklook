//! Navegación por el directorio contenedor.
//!
//! Lista los archivos de imagen de la carpeta del archivo inicial, en el mismo
//! orden natural que usa Dolphin (`foto2` antes que `foto10`), y mantiene el
//! índice del que se muestra. Listar nombres es barato; no decodifica nada.

use std::path::{Path, PathBuf};

fn is_image(path: &Path) -> bool {
    path.extension()
        .and_then(image::ImageFormat::from_extension)
        // image 0.25.10 anuncia AVIF con la feature de escritura, pero leerlo
        // exige avif-native, que este proyecto no habilita.
        .map(|format| format != image::ImageFormat::Avif && format.reading_enabled())
        .unwrap_or(false)
}

/// Comparación natural de nombres: trata las secuencias de dígitos como números,
/// de modo que "img2" < "img10". Insensible a mayúsculas para el resto.
fn natural_cmp(a: &str, b: &str) -> std::cmp::Ordering {
    use std::cmp::Ordering;
    let mut ai = a.chars().peekable();
    let mut bi = b.chars().peekable();
    let order = loop {
        match (ai.peek().copied(), bi.peek().copied()) {
            (None, None) => break Ordering::Equal,
            (None, Some(_)) => break Ordering::Less,
            (Some(_), None) => break Ordering::Greater,
            (Some(ca), Some(cb)) => {
                if ca.is_ascii_digit() && cb.is_ascii_digit() {
                    // Comparar bloques numéricos completos por valor.
                    let na: String =
                        std::iter::from_fn(|| ai.next_if(|c| c.is_ascii_digit())).collect();
                    let nb: String =
                        std::iter::from_fn(|| bi.next_if(|c| c.is_ascii_digit())).collect();
                    // Comparar sin ceros a la izquierda: primero por longitud, luego léxico.
                    let ta = na.trim_start_matches('0');
                    let tb = nb.trim_start_matches('0');
                    let ord = ta.len().cmp(&tb.len()).then_with(|| ta.cmp(tb));
                    if ord != Ordering::Equal {
                        break ord;
                    }
                } else {
                    let ord = ca.to_ascii_lowercase().cmp(&cb.to_ascii_lowercase());
                    if ord != Ordering::Equal {
                        break ord;
                    }
                    ai.next();
                    bi.next();
                }
            }
        }
    };
    // Desempate estable para mayúsculas y ceros a la izquierda.
    order.then_with(|| a.cmp(b))
}

/// Lista de imágenes de una carpeta con cursor sobre la actual.
pub struct Directory {
    files: Vec<PathBuf>,
    index: usize,
}

impl Directory {
    /// Construye la lista a partir del archivo inicial. Si la carpeta no se puede
    /// leer, cae a una lista de un solo elemento. Siempre incluye el inicial.
    pub fn new(initial: PathBuf) -> Self {
        // Normalizar a ruta absoluta: el service menu de Dolphin puede lanzarnos
        // con una ruta relativa (cwd = la carpeta), y entonces parent() quedaría
        // vacío y no encontraríamos los vecinos. canonicalize() resuelve esto.
        let initial = initial.canonicalize().unwrap_or(initial);
        let parent = initial.parent().map(Path::to_path_buf);
        let mut files: Vec<PathBuf> = parent
            .as_deref()
            .and_then(|p| std::fs::read_dir(p).ok())
            .map(|rd| {
                rd.filter_map(|e| e.ok().map(|e| e.path()))
                    .filter(|p| p.is_file() && is_image(p))
                    .collect()
            })
            .unwrap_or_default();

        // Nunca sustituir el archivo solicitado por otro de la carpeta.
        if !files
            .iter()
            .any(|p| p.canonicalize().is_ok_and(|p| p == initial))
        {
            files.push(initial.clone());
        }
        files.sort_by(|a, b| {
            let na = a.file_name().and_then(|n| n.to_str()).unwrap_or("");
            let nb = b.file_name().and_then(|n| n.to_str()).unwrap_or("");
            natural_cmp(na, nb)
        });

        // `initial` ya está canonicalizado arriba; comparamos contra él.
        let index = files
            .iter()
            .position(|p| p == &initial || p.canonicalize().is_ok_and(|c| c == initial))
            .unwrap_or(0);

        Self { files, index }
    }

    /// Archivo mostrado actualmente.
    pub fn current(&self) -> &Path {
        &self.files[self.index]
    }

    /// Avanza al siguiente (con vuelta al principio). Devuelve true si cambió.
    pub fn next(&mut self) -> bool {
        if self.files.len() <= 1 {
            return false;
        }
        self.index = (self.index + 1) % self.files.len();
        true
    }

    /// Retrocede al anterior (con vuelta al final). Devuelve true si cambió.
    pub fn prev(&mut self) -> bool {
        if self.files.len() <= 1 {
            return false;
        }
        self.index = (self.index + self.files.len() - 1) % self.files.len();
        true
    }

    /// Ruta del vecino siguiente, para prefetch (None si solo hay uno).
    pub fn peek_next(&self) -> Option<PathBuf> {
        (self.files.len() > 1).then(|| self.files[(self.index + 1) % self.files.len()].clone())
    }

    /// Ruta del vecino anterior, para prefetch.
    pub fn peek_prev(&self) -> Option<PathBuf> {
        (self.files.len() > 1)
            .then(|| self.files[(self.index + self.files.len() - 1) % self.files.len()].clone())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::TestDir;
    use std::cmp::Ordering;

    #[test]
    fn filters_only_decodable_formats() {
        for name in ["image.PNG", "photo.jpg", "icon.ico", "image.qoi"] {
            assert!(is_image(Path::new(name)), "{name}");
        }
        for name in [
            "image.svg",
            "image.avif",
            "notes.txt",
            "image",
            "image.png.bak",
        ] {
            assert!(!is_image(Path::new(name)), "{name}");
        }
    }

    #[test]
    fn natural_order_has_deterministic_ties() {
        assert_eq!(natural_cmp("foto2.png", "foto10.png"), Ordering::Less);
        assert_eq!(natural_cmp("foto2.png", "Foto10.png"), Ordering::Less);
        assert_ne!(natural_cmp("foto01.png", "foto1.png"), Ordering::Equal);
        assert_ne!(natural_cmp("Foto1.png", "foto1.png"), Ordering::Equal);
    }

    #[test]
    fn keeps_explicit_file_and_wraps_navigation() {
        let dir = TestDir::new();
        let initial = dir.path.join("preview.unknown");
        std::fs::write(&initial, b"explicit file").unwrap();
        std::fs::write(dir.path.join("foto10.png"), b"").unwrap();
        std::fs::write(dir.path.join("foto2.png"), b"").unwrap();
        std::fs::write(dir.path.join("vector.svg"), b"").unwrap();
        let mut directory = Directory::new(initial.clone());
        assert_eq!(directory.current(), initial);
        assert!(directory.next());
        assert_eq!(directory.current().file_name().unwrap(), "foto2.png");
        assert!(directory.next());
        assert_eq!(directory.current().file_name().unwrap(), "foto10.png");
        assert!(directory.next());
        assert_eq!(directory.current(), initial);
        assert!(directory.prev());
        assert_eq!(directory.current().file_name().unwrap(), "foto10.png");
    }

    #[test]
    fn single_file_has_no_neighbors() {
        let dir = TestDir::new();
        let path = dir.path.join("only.png");
        std::fs::write(&path, b"").unwrap();
        let mut directory = Directory::new(path.clone());
        assert_eq!(directory.current(), path);
        assert!(!directory.next());
        assert!(!directory.prev());
        assert!(directory.peek_next().is_none());
        assert!(directory.peek_prev().is_none());
    }
}
