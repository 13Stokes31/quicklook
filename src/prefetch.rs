//! Precarga asíncrona de imágenes vecinas.
//!
//! Un hilo worker decodifica en segundo plano las imágenes que se le pidan y las
//! guarda en un caché compartido por ruta. Así, al navegar con las flechas, el
//! vecino ya suele estar decodificado y el cambio es instantáneo. El archivo
//! inicial se carga de forma síncrona en el hilo principal (es el que el usuario
//! quiere ver ya); el worker solo adelanta trabajo para los vecinos.

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::mpsc::{self, Sender};
use std::sync::{Arc, Mutex};
use std::thread;

use crate::render::LoadedImage;

/// Caché ruta -> imagen decodificada, compartido entre worker y UI.
type Cache = Arc<Mutex<HashMap<PathBuf, Arc<LoadedImage>>>>;

pub struct Prefetcher {
    cache: Cache,
    tx: Sender<PathBuf>,
}

impl Prefetcher {
    /// Arranca el hilo worker. El caché empieza vacío.
    pub fn new() -> Self {
        let cache: Cache = Arc::new(Mutex::new(HashMap::new()));
        let (tx, rx) = mpsc::channel::<PathBuf>();

        let worker_cache = cache.clone();
        thread::spawn(move || {
            for path in rx {
                // Si ya está cacheada, no la recargamos.
                if worker_cache.lock().unwrap().contains_key(&path) {
                    continue;
                }
                if let Ok(img) = LoadedImage::load(&path) {
                    worker_cache
                        .lock()
                        .unwrap()
                        .insert(path, Arc::new(img));
                }
            }
        });

        Self { cache, tx }
    }

    /// Pide al worker que decodifique `path` en segundo plano (ignora si falla
    /// el envío, p. ej. si el worker ya no existe).
    pub fn request(&self, path: PathBuf) {
        let _ = self.tx.send(path);
    }

    /// Devuelve la imagen ya decodificada si está en caché.
    pub fn get(&self, path: &PathBuf) -> Option<Arc<LoadedImage>> {
        self.cache.lock().unwrap().get(path).cloned()
    }

    /// Inserta una imagen ya cargada en el hilo principal (p. ej. la inicial),
    /// para no volver a decodificarla si se vuelve a ella.
    pub fn insert(&self, path: PathBuf, img: Arc<LoadedImage>) {
        self.cache.lock().unwrap().insert(path, img);
    }
}
