//! Adaptador C para Qt. Solo el hilo de la interfaz accede a estos manejadores.
use std::ffi::{c_char, CString, OsString};
use std::os::unix::ffi::OsStringExt;
use std::path::PathBuf;
use std::sync::{mpsc, Arc};

use crate::nav::Directory;
use crate::prefetch::{ImageEvent, Prefetcher, QtFrame};
use crate::render::ImageRect;
use crate::theme::window_background;

enum Preview {
    Loading,
    Ready(Arc<QtFrame>),
    Failed(String),
}

pub struct Backend {
    dir: Directory,
    worker: Prefetcher,
    received: mpsc::Receiver<ImageEvent>,
    request_id: u64,
    preview: Preview,
    title: CString,
    background: u32,
}

impl Backend {
    fn new(path: PathBuf) -> anyhow::Result<Self> {
        anyhow::ensure!(path.is_file(), "no es un archivo");
        let (sent, received) = mpsc::channel();
        let worker = Prefetcher::new_qt(move |event| {
            let _ = sent.send(event);
        })?;
        let mut result = Self {
            dir: Directory::new(path),
            worker,
            received,
            request_id: 0,
            preview: Preview::Loading,
            title: CString::default(),
            background: window_background(),
        };
        result.request();
        Ok(result)
    }

    fn request(&mut self) {
        self.request_id = self.request_id.wrapping_add(1);
        self.preview = self
            .worker
            .cached_qt(self.dir.current())
            .map_or(Preview::Loading, Preview::Ready);
        self.worker.request(
            self.request_id,
            self.dir.current().to_path_buf(),
            [self.dir.peek_next(), self.dir.peek_prev()]
                .into_iter()
                .flatten(),
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
            Preview::Ready(_) => format!("{name} — Quick Look"),
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
        Some(Preview::Ready(_)) => 1,
        _ => 2,
    }
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
