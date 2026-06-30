//! Navegación por el directorio contenedor.
//!
//! Lista los archivos de imagen de la carpeta del archivo inicial, en el mismo
//! orden natural que usa Dolphin (`foto2` antes que `foto10`), y mantiene el
//! índice del que se muestra. Listar nombres es barato; no decodifica nada.

use std::path::{Path, PathBuf};

/// Extensiones de imagen soportadas por el visor (Fase 1/2).
const IMAGE_EXTS: &[&str] = &[
    "jpg", "jpeg", "png", "webp", "gif", "bmp", "tiff", "tif", "avif", "svg",
];

fn is_image(path: &Path) -> bool {
    path.extension()
        .and_then(|e| e.to_str())
        .map(|e| IMAGE_EXTS.contains(&e.to_ascii_lowercase().as_str()))
        .unwrap_or(false)
}

/// Comparación natural de nombres: trata las secuencias de dígitos como números,
/// de modo que "img2" < "img10". Insensible a mayúsculas para el resto.
fn natural_cmp(a: &str, b: &str) -> std::cmp::Ordering {
    use std::cmp::Ordering;
    let mut ai = a.chars().peekable();
    let mut bi = b.chars().peekable();
    loop {
        match (ai.peek().copied(), bi.peek().copied()) {
            (None, None) => return Ordering::Equal,
            (None, Some(_)) => return Ordering::Less,
            (Some(_), None) => return Ordering::Greater,
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
                        return ord;
                    }
                } else {
                    let ord = ca
                        .to_ascii_lowercase()
                        .cmp(&cb.to_ascii_lowercase())
                        .then(ca.cmp(&cb));
                    if ord != Ordering::Equal {
                        return ord;
                    }
                    ai.next();
                    bi.next();
                }
            }
        }
    }
}

/// Lista de imágenes de una carpeta con cursor sobre la actual.
pub struct Directory {
    files: Vec<PathBuf>,
    index: usize,
}

impl Directory {
    /// Construye la lista a partir del archivo inicial. Si la carpeta no se puede
    /// leer o el archivo no aparece, cae a una lista de un solo elemento.
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

        if files.is_empty() {
            files.push(initial.clone());
        } else {
            files.sort_by(|a, b| {
                let na = a.file_name().and_then(|n| n.to_str()).unwrap_or("");
                let nb = b.file_name().and_then(|n| n.to_str()).unwrap_or("");
                natural_cmp(na, nb)
            });
        }

        // `initial` ya está canonicalizado arriba; comparamos contra él.
        let index = files
            .iter()
            .position(|p| p.canonicalize().map(|c| c == initial).unwrap_or(false))
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
