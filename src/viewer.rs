//! Ventana frameless del visor y bucle de eventos (winit 0.30 + softbuffer).

use std::num::NonZeroU32;
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::Arc;

use anyhow::Result;
use winit::application::ApplicationHandler;
use winit::dpi::LogicalSize;
use winit::event::WindowEvent;
use winit::event_loop::{ActiveEventLoop, EventLoop};
use winit::keyboard::{Key, NamedKey};
use winit::platform::wayland::WindowAttributesExtWayland;
use winit::window::{Window, WindowId};

/// app_id estable de la ventana. KWin lo usa para identificarla y para que una
/// regla de ventana pueda ocultarla del gestor de tareas.
const APP_ID: &str = "org.quicklook.Viewer";

use crate::nav::Directory;
use crate::prefetch::Prefetcher;
use crate::render::LoadedImage;

/// Lado máximo (en píxeles lógicos) que ocupará la ventana en pantalla.
/// La ventana se escala manteniendo el aspect-ratio dentro de esta caja.
const MAX_SIDE: f64 = 900.0;

pub struct Viewer {
    image: Arc<LoadedImage>,
    dir: Directory,
    prefetch: Prefetcher,
    // softbuffer exige que la Surface no sobreviva a la Window: las atamos con Rc.
    window: Option<Rc<Window>>,
    surface: Option<softbuffer::Surface<Rc<Window>, Rc<Window>>>,
    context: Option<softbuffer::Context<Rc<Window>>>,
    // Buffer ya escalado (Lanczos3) cacheado: solo se recomputa al cambiar de
    // imagen o de tamaño de superficie.
    scaled: Option<(u32, u32, Vec<u32>)>,
}

impl Viewer {
    pub fn new(path: PathBuf) -> Result<Self> {
        let dir = Directory::new(path);
        let image = Arc::new(LoadedImage::load(dir.current())?);

        let prefetch = Prefetcher::new();
        prefetch.insert(dir.current().to_path_buf(), image.clone());

        let viewer = Self {
            image,
            dir,
            prefetch,
            window: None,
            surface: None,
            context: None,
            scaled: None,
        };
        viewer.request_neighbours();
        Ok(viewer)
    }

    /// Pide al worker que adelante la decodificación de los vecinos.
    fn request_neighbours(&self) {
        if let Some(p) = self.dir.peek_next() {
            self.prefetch.request(p);
        }
        if let Some(p) = self.dir.peek_prev() {
            self.prefetch.request(p);
        }
    }

    /// Carga la imagen actual (desde caché si está, si no del disco), reajusta la
    /// ventana al nuevo aspect-ratio, invalida el escalado y repinta.
    fn show_current(&mut self) {
        let path = self.dir.current().to_path_buf();
        self.image = match self.prefetch.get(&path) {
            Some(img) => img,
            None => match LoadedImage::load(&path) {
                Ok(img) => {
                    let img = Arc::new(img);
                    self.prefetch.insert(path, img.clone());
                    img
                }
                Err(e) => {
                    eprintln!("no se pudo cargar {}: {e}", self.dir.current().display());
                    return;
                }
            },
        };
        self.scaled = None; // invalidar el escalado cacheado de la imagen anterior
        if let Some(window) = self.window.as_ref() {
            let _ = window.request_inner_size(self.window_size());
            window.request_redraw();
        }
        self.request_neighbours();
    }

    pub fn run(self) -> Result<()> {
        let event_loop = EventLoop::new()?;
        let mut app = self;
        event_loop.run_app(&mut app)?;
        Ok(())
    }

    /// Tamaño lógico de ventana que respeta el aspect-ratio dentro de MAX_SIDE.
    fn window_size(&self) -> LogicalSize<f64> {
        let ar = self.image.aspect_ratio();
        let (w, h) = if ar >= 1.0 {
            (MAX_SIDE, MAX_SIDE / ar)
        } else {
            (MAX_SIDE * ar, MAX_SIDE)
        };
        LogicalSize::new(w, h)
    }

    fn redraw(&mut self) {
        let (Some(window), Some(surface)) = (self.window.as_ref(), self.surface.as_mut()) else {
            return;
        };
        let size = window.inner_size();
        let (Some(w), Some(h)) = (NonZeroU32::new(size.width), NonZeroU32::new(size.height)) else {
            return;
        };
        if surface.resize(w, h).is_err() {
            return;
        }

        // Escalar solo si no hay caché o cambió el tamaño de la superficie.
        let needs_scale = match &self.scaled {
            Some((cw, ch, _)) => *cw != w.get() || *ch != h.get(),
            None => true,
        };
        if needs_scale {
            match self.image.to_softbuffer(w.get(), h.get()) {
                Ok(p) => self.scaled = Some((w.get(), h.get(), p)),
                Err(e) => {
                    eprintln!("error al escalar: {e}");
                    return;
                }
            }
        }

        let Some((_, _, pixels)) = &self.scaled else {
            return;
        };
        if let Ok(mut buf) = surface.buffer_mut() {
            let n = buf.len().min(pixels.len());
            buf[..n].copy_from_slice(&pixels[..n]);
            let _ = buf.present();
        }
    }
}

impl ApplicationHandler for Viewer {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_some() {
            return;
        }
        // with_name(general, instance) fija el app_id de Wayland (general),
        // que KWin usa para identificar la ventana y aplicarle reglas.
        let attrs = Window::default_attributes()
            .with_name(APP_ID, "viewer")
            .with_title("Quick Look")
            .with_decorations(false)
            .with_resizable(false)
            .with_inner_size(self.window_size());

        let window = match event_loop.create_window(attrs) {
            Ok(w) => Rc::new(w),
            Err(e) => {
                eprintln!("no se pudo crear la ventana: {e}");
                event_loop.exit();
                return;
            }
        };

        let context = softbuffer::Context::new(window.clone()).expect("contexto softbuffer");
        let surface =
            softbuffer::Surface::new(&context, window.clone()).expect("surface softbuffer");

        self.context = Some(context);
        self.surface = Some(surface);
        self.window = Some(window);
    }

    fn window_event(
        &mut self,
        event_loop: &ActiveEventLoop,
        _id: WindowId,
        event: WindowEvent,
    ) {
        match event {
            WindowEvent::CloseRequested => event_loop.exit(),

            WindowEvent::RedrawRequested => self.redraw(),

            WindowEvent::KeyboardInput { event, .. } if event.state.is_pressed() => {
                match event.logical_key {
                    Key::Named(NamedKey::Escape) => event_loop.exit(),
                    Key::Named(NamedKey::ArrowRight) | Key::Named(NamedKey::ArrowDown) => {
                        if self.dir.next() {
                            self.show_current();
                        }
                    }
                    Key::Named(NamedKey::ArrowLeft) | Key::Named(NamedKey::ArrowUp) => {
                        if self.dir.prev() {
                            self.show_current();
                        }
                    }
                    _ => {}
                }
            }

            // Cierre al perder la activación (aproximación al "clic fuera" de macOS).
            // En Wayland esto equivale a que la toplevel deja de estar activa.
            WindowEvent::Focused(false) => event_loop.exit(),

            _ => {}
        }
    }
}
