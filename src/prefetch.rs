//! Un único trabajador carga la selección y adelanta como máximo dos vecinos.

use std::collections::VecDeque;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Condvar, Mutex};
use std::thread;

use anyhow::Result;

use crate::render::{image_rect, ImageRect, LoadedImage};

const CACHE_BYTES: usize = 256 * 1024 * 1024;
const CACHE_ENTRIES: usize = 3;
const FRAME_BYTES: usize = 64 * 1024 * 1024;

#[derive(Clone, Copy, PartialEq, Eq)]
struct Viewport {
    width: u32,
    height: u32,
    background: u32,
}

#[derive(Debug)]
pub struct QtFrame {
    pub pixels: Vec<u32>,
    #[allow(dead_code)] // Geometría consumida por la interfaz Qt, no por el visor original.
    pub rect: ImageRect,
}

#[derive(Default)]
struct QtFrames {
    viewport: Option<Viewport>,
    entries: VecDeque<(PathBuf, Arc<QtFrame>)>,
    bytes: usize,
}

impl QtFrames {
    fn get(&mut self, path: &Path) -> Option<Arc<QtFrame>> {
        let index = self.entries.iter().position(|(p, _)| p == path)?;
        let entry = self.entries.remove(index)?;
        let frame = entry.1.clone();
        self.entries.push_back(entry);
        Some(frame)
    }

    fn insert(&mut self, path: PathBuf, frame: Arc<QtFrame>) {
        let bytes = frame.pixels.len() * 4;
        if bytes > FRAME_BYTES {
            return;
        }
        if let Some(index) = self.entries.iter().position(|(p, _)| *p == path) {
            let (_, old) = self.entries.remove(index).unwrap();
            self.bytes -= old.pixels.len() * 4;
        }
        while self.bytes + bytes > FRAME_BYTES || self.entries.len() >= CACHE_ENTRIES {
            let (_, old) = self.entries.pop_front().unwrap();
            self.bytes -= old.pixels.len() * 4;
        }
        self.bytes += bytes;
        self.entries.push_back((path, frame));
    }
}

#[derive(Debug)]
pub struct ImageEvent {
    pub id: u64,
    pub path: PathBuf,
    pub result: Result<Arc<LoadedImage>, String>,
    #[allow(dead_code)] // El ejecutable original consume únicamente la imagen decodificada.
    pub qt_frame: Option<Arc<QtFrame>>,
}

struct ImageCache {
    entries: VecDeque<(PathBuf, Arc<LoadedImage>)>,
    bytes: usize,
    limit: usize,
}

impl ImageCache {
    fn new(limit: usize) -> Self {
        Self {
            entries: VecDeque::new(),
            bytes: 0,
            limit,
        }
    }

    fn get(&mut self, path: &Path) -> Option<Arc<LoadedImage>> {
        let index = self.entries.iter().position(|(p, _)| p == path)?;
        let entry = self.entries.remove(index)?;
        let image = entry.1.clone();
        self.entries.push_back(entry);
        Some(image)
    }

    fn insert(&mut self, path: PathBuf, image: Arc<LoadedImage>) {
        if let Some(index) = self.entries.iter().position(|(p, _)| *p == path) {
            let (_, old) = self.entries.remove(index).unwrap();
            self.bytes -= old.memory_bytes();
        }
        let size = image.memory_bytes();
        if size > self.limit {
            return;
        }
        while self.bytes + size > self.limit || self.entries.len() >= CACHE_ENTRIES {
            let (_, old) = self.entries.pop_front().unwrap();
            self.bytes -= old.memory_bytes();
        }
        self.bytes += size;
        self.entries.push_back((path, image));
    }
}

#[derive(Default)]
struct PendingWork {
    selected: Option<(u64, PathBuf)>,
    neighbors: VecDeque<PathBuf>,
    latest_id: u64,
    closed: bool,
}

impl PendingWork {
    fn replace(&mut self, id: u64, path: PathBuf, neighbors: impl IntoIterator<Item = PathBuf>) {
        self.latest_id = id;
        self.neighbors.clear();
        for neighbor in neighbors {
            if neighbor != path && !self.neighbors.contains(&neighbor) {
                self.neighbors.push_back(neighbor);
                if self.neighbors.len() == 2 {
                    break;
                }
            }
        }
        self.selected = Some((id, path));
    }
}

pub struct Prefetcher {
    work: Arc<(Mutex<PendingWork>, Condvar)>,
    qt: Option<Arc<Mutex<QtFrames>>>,
}

impl Prefetcher {
    #[allow(dead_code)] // Constructor del visor original, sin caché de escalado Qt.
    pub fn new(on_loaded: impl Fn(ImageEvent) + Send + 'static) -> Result<Self> {
        Self::spawn(on_loaded, None)
    }

    // La interfaz Qt comparte el mismo trabajador, sin añadir hilos de escalado.
    #[allow(dead_code)]
    pub fn new_qt(on_loaded: impl Fn(ImageEvent) + Send + 'static) -> Result<Self> {
        Self::spawn(on_loaded, Some(Arc::new(Mutex::new(QtFrames::default()))))
    }

    fn spawn(
        on_loaded: impl Fn(ImageEvent) + Send + 'static,
        qt: Option<Arc<Mutex<QtFrames>>>,
    ) -> Result<Self> {
        let work = Arc::new((Mutex::new(PendingWork::default()), Condvar::new()));
        let worker_work = work.clone();
        let worker_qt = qt.clone();
        thread::Builder::new()
            .name("quicklook-loader".into())
            .spawn(move || {
                let mut cache = ImageCache::new(CACHE_BYTES);
                let (lock, wake) = &*worker_work;
                loop {
                    let (id, path) = {
                        let mut work = lock.lock().unwrap();
                        while !work.closed && work.selected.is_none() && work.neighbors.is_empty() {
                            work = wake.wait(work).unwrap();
                        }
                        if work.closed {
                            return;
                        }
                        if let Some((id, path)) = work.selected.take() {
                            (Some(id), path)
                        } else {
                            (None, work.neighbors.pop_front().unwrap())
                        }
                    };
                    // Nunca se mantiene el mutex durante la decodificación.
                    let mut result = match cache.get(&path) {
                        Some(image) => Ok(image),
                        None => LoadedImage::load(&path)
                            .map(Arc::new)
                            .inspect(|image| {
                                cache.insert(path.clone(), image.clone());
                            })
                            .map_err(|error| format!("{error:#}")),
                    };
                    let mut qt_frame = None;
                    if let (Some(qt), Ok(image)) = (&worker_qt, &result) {
                        let current = {
                            let work = lock.lock().unwrap();
                            !work.closed
                                && match id {
                                    Some(id) => work.latest_id == id,
                                    None => work.selected.is_none(),
                                }
                        };
                        if !current {
                            continue;
                        }
                        let (viewport, cached) = {
                            let mut frames = qt.lock().unwrap();
                            (frames.viewport, frames.get(&path))
                        };
                        if viewport.is_none() {
                            // Antes de conocer la pantalla, adelantar un vecino podría
                            // retrasar el escalado de la primera imagen ya decodificada.
                            let mut work = lock.lock().unwrap();
                            if id == Some(work.latest_id) {
                                work.neighbors.clear();
                            }
                        }
                        if let Some(viewport) = viewport {
                            qt_frame = cached;
                            if qt_frame.is_none() {
                                match image.to_qt_buffer(
                                    viewport.width,
                                    viewport.height,
                                    viewport.background,
                                ) {
                                    Ok(pixels) => {
                                        let frame = Arc::new(QtFrame {
                                            pixels,
                                            rect: image_rect(
                                                viewport.width,
                                                viewport.height,
                                                image.width,
                                                image.height,
                                            ),
                                        });
                                        let mut frames = qt.lock().unwrap();
                                        if frames.viewport == Some(viewport) {
                                            frames.insert(path.clone(), frame.clone());
                                            qt_frame = Some(frame);
                                        }
                                    }
                                    Err(error) => result = Err(format!("{error:#}")),
                                }
                            }
                        }
                    }
                    if let Some(id) = id {
                        let current = {
                            let work = lock.lock().unwrap();
                            !work.closed && work.latest_id == id
                        };
                        if current {
                            on_loaded(ImageEvent {
                                id,
                                path,
                                result,
                                qt_frame,
                            });
                        }
                    }
                }
            })?;
        Ok(Self { work, qt })
    }

    #[allow(dead_code)]
    pub fn configure_qt(&self, width: u32, height: u32, background: u32) -> bool {
        let Some(qt) = &self.qt else {
            return false;
        };
        let mut frames = qt.lock().unwrap();
        let viewport = Viewport {
            width,
            height,
            background,
        };
        if frames.viewport == Some(viewport) {
            return false;
        }
        frames.viewport = Some(viewport);
        frames.entries.clear();
        frames.bytes = 0;
        true
    }

    #[allow(dead_code)]
    pub fn cached_qt(&self, path: &Path) -> Option<Arc<QtFrame>> {
        self.qt.as_ref()?.lock().unwrap().get(path)
    }

    pub fn request(&self, id: u64, path: PathBuf, neighbors: impl IntoIterator<Item = PathBuf>) {
        let (lock, wake) = &*self.work;
        lock.lock().unwrap().replace(id, path, neighbors);
        wake.notify_one();
    }

    /// Descarta la cola al mostrar un documento; una decodificación ya iniciada
    /// puede terminar, pero no publicará resultados de la selección anterior.
    #[allow(dead_code)] // Solo Qt muestra documentos.
    pub fn cancel_pending(&self, id: u64) {
        let mut work = self.work.0.lock().unwrap();
        work.latest_id = id;
        work.selected = None;
        work.neighbors.clear();
    }
}

impl Drop for Prefetcher {
    fn drop(&mut self) {
        let (lock, wake) = &*self.work;
        lock.lock().unwrap().closed = true;
        wake.notify_one();
        // No bloquear el cierre esperando a una decodificación en curso.
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::TestDir;
    use std::sync::mpsc;
    use std::time::Duration;

    #[test]
    fn cache_evicts_least_recently_used_by_bytes() {
        let dir = TestDir::new();
        let paths: Vec<_> = (0..3)
            .map(|i| dir.image(&format!("{i}.png"), 2, 2))
            .collect();
        let mut cache = ImageCache::new(32);
        for path in &paths[..2] {
            cache.insert(path.clone(), Arc::new(LoadedImage::load(path).unwrap()));
        }
        assert!(cache.get(&paths[0]).is_some());
        cache.insert(
            paths[2].clone(),
            Arc::new(LoadedImage::load(&paths[2]).unwrap()),
        );
        assert!(cache.get(&paths[1]).is_none());
        assert!(cache.get(&paths[0]).is_some());
        assert!(cache.get(&paths[2]).is_some());
        assert_eq!(cache.bytes, 32);
        cache.insert(
            paths[2].clone(),
            Arc::new(LoadedImage::load(&paths[2]).unwrap()),
        );
        assert_eq!(cache.bytes, 32);
    }

    #[test]
    fn cache_bounds_entry_count_and_does_not_store_oversized_images() {
        let dir = TestDir::new();
        let mut cache = ImageCache::new(64);
        for i in 0..10 {
            let path = dir.image(&format!("{i}.png"), 1, 1);
            cache.insert(path.clone(), Arc::new(LoadedImage::load(&path).unwrap()));
        }
        assert_eq!(cache.entries.len(), CACHE_ENTRIES);
        assert_eq!(cache.bytes, 12);
        let path = dir.image("large.png", 5, 5);
        cache.insert(path.clone(), Arc::new(LoadedImage::load(&path).unwrap()));
        assert!(cache.get(&path).is_none());
        assert_eq!(cache.bytes, 12);
    }

    #[test]
    fn requests_replace_old_work_and_deduplicate_neighbors() {
        let mut work = PendingWork::default();
        for id in 0..1000 {
            work.replace(
                id,
                PathBuf::from("current.png"),
                [
                    PathBuf::from("next.png"),
                    PathBuf::from("next.png"),
                    PathBuf::from("current.png"),
                    PathBuf::from("previous.png"),
                    PathBuf::from("extra.png"),
                ],
            );
        }
        assert_eq!(work.selected.as_ref().unwrap().0, 999);
        assert_eq!(
            work.neighbors,
            VecDeque::from([PathBuf::from("next.png"), PathBuf::from("previous.png")])
        );
    }

    #[test]
    fn worker_loads_selection_and_recovers_after_bad_file() {
        let dir = TestDir::new();
        let path = dir.image("good.png", 2, 3);
        let bad = dir.path.join("bad.png");
        std::fs::write(&bad, b"not an image").unwrap();
        let (tx, rx) = mpsc::channel();
        let worker = Prefetcher::new(move |event| {
            let _ = tx.send(event);
        })
        .unwrap();
        worker.request(1, path.clone(), []);
        let event = rx.recv_timeout(Duration::from_secs(5)).unwrap();
        assert_eq!(event.path, path);
        let first = event.result.unwrap();
        assert_eq!((first.width, first.height), (2, 3));
        worker.request(2, bad, []);
        assert!(rx
            .recv_timeout(Duration::from_secs(5))
            .unwrap()
            .result
            .is_err());
        worker.request(3, path, []);
        let event = rx.recv_timeout(Duration::from_secs(5)).unwrap();
        assert_eq!(event.id, 3);
        assert!(Arc::ptr_eq(&first, &event.result.unwrap()));
    }

    #[test]
    fn qt_waits_for_viewport_before_prefetch_and_bounds_frame_cache() {
        let dir = TestDir::new();
        let first = dir.image("1.png", 2, 3);
        let second = dir.image("2.png", 3, 2);
        let (tx, rx) = mpsc::channel();
        let worker = Prefetcher::new_qt(move |event| {
            tx.send(event).unwrap();
        })
        .unwrap();
        worker.request(1, first.clone(), [second]);
        let event = rx.recv_timeout(Duration::from_secs(5)).unwrap();
        assert!(event.qt_frame.is_none());
        assert!(worker.work.0.lock().unwrap().neighbors.is_empty());
        worker.configure_qt(180, 180, 0xffffff);
        worker.request(2, first, []);
        assert!(rx
            .recv_timeout(Duration::from_secs(5))
            .unwrap()
            .qt_frame
            .is_some());
        let mut frames = QtFrames::default();
        for i in 0..4 {
            frames.insert(
                PathBuf::from(i.to_string()),
                Arc::new(QtFrame {
                    pixels: vec![0xff112233; 4],
                    rect: ImageRect::default(),
                }),
            );
        }
        assert_eq!(frames.entries.len(), 3);
        assert_eq!(frames.bytes, 48);
        assert!(frames.get(Path::new("0")).is_none());
        assert!(frames.get(Path::new("1")).is_some());
    }
}
