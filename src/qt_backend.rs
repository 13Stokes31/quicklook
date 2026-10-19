//! Adaptador C para Qt. Solo el hilo de la interfaz accede a estos manejadores.
use std::ffi::{c_char, CString, OsString};
use std::io::Read;
use std::os::unix::ffi::OsStrExt;
use std::os::unix::ffi::OsStringExt;
use std::path::PathBuf;
use std::sync::{mpsc, Arc};

use crate::nav::{is_image, is_office, Directory};
use crate::prefetch::{ImageEvent, Prefetcher, QtFrame};
use crate::render::ImageRect;
use crate::theme::window_background;

enum Preview {
    Loading,
    Ready(Arc<QtFrame>),
    Failed(String),
    Document,
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
enum Kind {
    Image = 0,
    Text = 1,
    Pdf = 2,
    Office = 3,
    Folder = 4,
}

fn document_kind(path: &std::path::Path) -> Kind {
    if path.is_dir() {
        return Kind::Folder;
    }
    if is_office(path) {
        return Kind::Office;
    }
    if is_image(path) {
        return Kind::Image;
    }
    if path
        .extension()
        .is_some_and(|ext| ext.eq_ignore_ascii_case("pdf"))
    {
        return Kind::Pdf;
    }
    let mut bytes = [0; 8192];
    let Ok(length) = std::fs::File::open(path).and_then(|mut file| file.read(&mut bytes)) else {
        return Kind::Image;
    };
    let bytes = &bytes[..length];
    if bytes.starts_with(b"%PDF-") {
        return Kind::Pdf;
    }
    if image::guess_format(bytes).is_ok() {
        return Kind::Image;
    }
    if bytes
        .iter()
        .any(|&byte| byte < 32 && !matches!(byte, b'\t' | b'\r' | b'\n' | 12))
    {
        Kind::Image
    } else {
        Kind::Text
    }
}

pub struct Backend {
    dir: Directory,
    worker: Prefetcher,
    received: mpsc::Receiver<ImageEvent>,
    request_id: u64,
    preview: Preview,
    title: CString,
    background: u32,
    kind: Kind,
    path: CString,
}

impl Backend {
    fn new(path: PathBuf) -> anyhow::Result<Self> {
        anyhow::ensure!(
            path.is_file() || path.is_dir(),
            "no es un archivo ni una carpeta"
        );
        let (sent, received) = mpsc::channel();
        let worker = Prefetcher::new_qt(move |event| {
            let _ = sent.send(event);
        })?;
        let mut result = Self {
            dir: Directory::with_documents(path),
            worker,
            received,
            request_id: 0,
            preview: Preview::Loading,
            title: CString::default(),
            background: window_background(),
            kind: Kind::Image,
            path: CString::default(),
        };
        result.request();
        Ok(result)
    }

    fn request(&mut self) {
        self.request_id = self.request_id.wrapping_add(1);
        self.path = CString::new(self.dir.current().as_os_str().as_bytes()).unwrap();
        self.kind = document_kind(self.dir.current());
        if self.kind != Kind::Image {
            self.worker.cancel_pending(self.request_id);
            self.preview = Preview::Document;
            self.update_title();
            return;
        }
        self.preview = self
            .worker
            .cached_qt(self.dir.current())
            .map_or(Preview::Loading, Preview::Ready);
        self.worker.request(
            self.request_id,
            self.dir.current().to_path_buf(),
            [self.dir.peek_next(), self.dir.peek_prev()]
                .into_iter()
                .flatten()
                .filter(|path| is_image(path)),
        );
        self.update_title();
    }

    fn update_title(&mut self) {
        let name = self
            .dir
            .current()
            .file_name()
            .unwrap_or_default()
            .to_string_lossy();
        let title = match &self.preview {
            Preview::Loading => format!("{name} — Cargando — Quick Look"),
            Preview::Ready(_) | Preview::Document => format!("{name} — Quick Look"),
            Preview::Failed(error) => format!("{name} — No se pudo abrir: {error} — Quick Look"),
        };
        self.title = CString::new(title.replace('\0', "�")).unwrap();
    }

    fn poll(&mut self) -> bool {
        let mut changed = false;
        while let Ok(event) = self.received.try_recv() {
            if event.id != self.request_id || event.path != self.dir.current() {
                continue;
            }
            self.preview = match event.result {
                Ok(_) => match event.qt_frame {
                    Some(frame) => Preview::Ready(frame),
                    None => continue,
                },
                Err(error) => {
                    eprintln!("no se pudo cargar {}: {error}", event.path.display());
                    Preview::Failed(error)
                }
            };
            changed = true;
        }
        if changed {
            self.update_title();
        }
        changed
    }

    fn render(&mut self, width: u32, height: u32) -> Option<(*const u32, ImageRect)> {
        if self.kind != Kind::Image {
            return None;
        }
        if self.worker.configure_qt(width, height, self.background) {
            self.request();
        }
        let Preview::Ready(frame) = &self.preview else {
            return None;
        };
        Some((frame.pixels.as_ptr(), frame.rect))
    }
}

// Contrato: los punteros de entrada válidos son propiedad del llamante. El
// manejador se libera una sola vez. Los punteros devueltos pertenecen al manejador
// y solo son válidos hasta su siguiente mutación; nunca se usan desde el worker.
#[no_mangle]
pub unsafe extern "C" fn ql_create(path: *const u8, length: usize) -> *mut Backend {
    if path.is_null() || length == 0 || length > 1024 * 1024 {
        return std::ptr::null_mut();
    }
    let bytes = unsafe { std::slice::from_raw_parts(path, length) };
    let path = PathBuf::from(OsString::from_vec(bytes.to_vec()));
    match Backend::new(path) {
        Ok(backend) => Box::into_raw(Box::new(backend)),
        Err(error) => {
            eprintln!("no se pudo iniciar Quick Look: {error:#}");
            std::ptr::null_mut()
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn ql_destroy(backend: *mut Backend) {
    if !backend.is_null() {
        drop(unsafe { Box::from_raw(backend) });
    }
}

#[no_mangle]
pub unsafe extern "C" fn ql_poll(backend: *mut Backend) -> bool {
    unsafe { backend.as_mut() }.is_some_and(Backend::poll)
}

#[no_mangle]
pub unsafe extern "C" fn ql_navigate(backend: *mut Backend, next: bool) -> bool {
    let Some(backend) = (unsafe { backend.as_mut() }) else {
        return false;
    };
    let changed = if next {
        backend.dir.next()
    } else {
        backend.dir.prev()
    };
    if changed {
        backend.request();
    }
    changed
}

#[no_mangle]
pub unsafe extern "C" fn ql_state(backend: *const Backend) -> u32 {
    match unsafe { backend.as_ref() }.map(|backend| &backend.preview) {
        Some(Preview::Loading) => 0,
        Some(Preview::Ready(_) | Preview::Document) => 1,
        _ => 2,
    }
}

#[no_mangle]
pub unsafe extern "C" fn ql_kind(backend: *const Backend) -> u32 {
    unsafe { backend.as_ref() }.map_or(0, |b| b.kind as u32)
}

#[no_mangle]
pub unsafe extern "C" fn ql_path(backend: *const Backend) -> *const c_char {
    unsafe { backend.as_ref() }.map_or(std::ptr::null(), |b| b.path.as_ptr())
}

#[no_mangle]
pub unsafe extern "C" fn ql_title(backend: *const Backend) -> *const c_char {
    unsafe { backend.as_ref() }.map_or(std::ptr::null(), |b| b.title.as_ptr())
}

#[no_mangle]
pub unsafe extern "C" fn ql_background(backend: *const Backend) -> u32 {
    unsafe { backend.as_ref() }.map_or(0xffffff, |b| b.background)
}

#[no_mangle]
pub unsafe extern "C" fn ql_render(
    backend: *mut Backend,
    width: u32,
    height: u32,
    rect: *mut ImageRect,
) -> *const u32 {
    let (Some(backend), Some(rect)) = (unsafe { backend.as_mut() }, unsafe { rect.as_mut() })
    else {
        return std::ptr::null();
    };
    let Some((pixels, image)) = backend.render(width, height) else {
        *rect = ImageRect::default();
        return std::ptr::null();
    };
    *rect = image;
    pixels
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::TestDir;
    use std::time::{Duration, Instant};

    #[test]
    fn documents_skip_image_decoding_and_ignore_late_image_results() {
        let dir = TestDir::new();
        let image = dir.image("1.png", 2, 2);
        let text = dir.path.join("2.txt");
        std::fs::write(&text, b"<b>literal text</b>").unwrap();
        let mut backend = Backend::new(image).unwrap();
        let old_id = backend.request_id;
        backend.dir.next();
        backend.request();
        assert_eq!(backend.kind as u32, Kind::Text as u32);
        assert!(matches!(backend.preview, Preview::Document));
        assert!(backend.render(200, 100).is_none());
        assert!(backend.path.as_bytes().ends_with(b"2.txt"));
        let (sent, received) = mpsc::channel();
        backend.received = received;
        sent.send(ImageEvent {
            id: old_id,
            path: text,
            result: Err("late".into()),
            qt_frame: None,
        })
        .unwrap();
        assert!(!backend.poll());
        assert!(matches!(backend.preview, Preview::Document));
        backend.dir.prev();
        backend.request();
        assert_eq!(backend.kind as u32, Kind::Image as u32);
    }

    #[test]
    fn content_detection_preserves_images_without_extension_and_rejects_binary_text() {
        let dir = TestDir::new();
        let original = dir.image("picture.png", 2, 2);
        let image = dir.path.join("picture");
        std::fs::rename(original, &image).unwrap();
        let pdf = dir.path.join("document");
        std::fs::write(&pdf, b"%PDF-1.4\n").unwrap();
        let binary = dir.path.join("binary.txt");
        std::fs::write(&binary, b"bad\0data").unwrap();
        assert_eq!(document_kind(&image) as u32, Kind::Image as u32);
        assert_eq!(document_kind(&pdf) as u32, Kind::Pdf as u32);
        assert_eq!(document_kind(&binary) as u32, Kind::Image as u32);
    }

    #[test]
    fn office_and_folder_do_not_enter_the_image_worker() {
        let dir = TestDir::new();
        let folder = dir.path.join("folder");
        std::fs::create_dir(&folder).unwrap();
        let office = dir.path.join("file.docx");
        std::fs::write(&office, b"PK\0office").unwrap();
        for (path, expected) in [(office, Kind::Office), (folder, Kind::Folder)] {
            let mut backend = Backend::new(path).unwrap();
            assert_eq!(backend.kind as u32, expected as u32);
            assert!(matches!(backend.preview, Preview::Document));
            assert!(backend.render(200, 100).is_none());
        }
    }

    fn loaded(backend: &mut Backend) {
        backend.render(180, 180);
        let deadline = Instant::now() + Duration::from_secs(5);
        while matches!(backend.preview, Preview::Loading) {
            backend.poll();
            assert!(Instant::now() < deadline);
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    #[test]
    fn navigation_errors_cached_render_and_late_results() {
        let dir = TestDir::new();
        let first = dir.image("1.png", 2, 2);
        let bad = dir.path.join("2.png");
        std::fs::write(&bad, b"invalid").unwrap();
        let mut backend = Backend::new(first).unwrap();
        loaded(&mut backend);
        let (pixels, rect) = backend.render(180, 180).unwrap();
        assert_eq!((rect.x, rect.y, rect.width, rect.height), (5, 5, 170, 170));
        assert_eq!(pixels, backend.render(180, 180).unwrap().0);
        assert_eq!(unsafe { *pixels } >> 24, 255);
        backend.dir.next();
        backend.request();
        assert!(backend.render(180, 180).is_none());
        loaded(&mut backend);
        assert!(matches!(backend.preview, Preview::Failed(_)));
        backend.dir.prev();
        backend.request();
        loaded(&mut backend);
        assert!(backend.render(180, 180).is_some());
    }

    #[test]
    fn c_interface_preserves_non_utf8_paths_and_null_guards() {
        let dir = TestDir::new();
        let original = dir.image("source.png", 2, 3);
        let path = dir
            .path
            .join(OsString::from_vec(b"image-\xff.png".to_vec()));
        std::fs::copy(original, &path).unwrap();
        use std::os::unix::ffi::OsStrExt;
        let bytes = path.as_os_str().as_bytes();
        unsafe {
            assert!(ql_create(std::ptr::null(), 1).is_null());
            assert!(!ql_poll(std::ptr::null_mut()));
            let backend = ql_create(bytes.as_ptr(), bytes.len());
            assert!(!backend.is_null());
            loaded(&mut *backend);
            assert_eq!(ql_state(backend), 1);
            assert!(!ql_title(backend).is_null());
            ql_destroy(backend);
        }
    }

    #[test]
    fn prefetched_navigation_is_ready_without_placeholder_and_resize_discards_old_frames() {
        let dir = TestDir::new();
        let first = dir.image("1.png", 2, 2);
        let second = dir.image("2.png", 3, 2);
        let mut backend = Backend::new(first).unwrap();
        loaded(&mut backend);
        let deadline = Instant::now() + Duration::from_secs(5);
        let frame = loop {
            if let Some(frame) = backend.worker.cached_qt(&second) {
                break frame;
            }
            assert!(Instant::now() < deadline);
            std::thread::sleep(Duration::from_millis(5));
        };
        backend.dir.next();
        backend.request();
        assert!(matches!(backend.preview, Preview::Ready(_)));
        assert_eq!(backend.render(180, 180).unwrap().0, frame.pixels.as_ptr());
        assert!(backend.render(200, 200).is_none());
        assert!(matches!(backend.preview, Preview::Loading));
        let deadline = Instant::now() + Duration::from_secs(5);
        while matches!(backend.preview, Preview::Loading) {
            backend.poll();
            assert!(Instant::now() < deadline);
            std::thread::sleep(Duration::from_millis(5));
        }
        assert_eq!(backend.render(200, 200).unwrap().1.width, 189);
    }
}
